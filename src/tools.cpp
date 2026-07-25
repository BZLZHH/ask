#include "ask/tools.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <netdb.h>
#include <poll.h>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <curl/curl.h>

#include "ask/http.hpp"

namespace ask {
namespace {

std::string json_string(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

Json::Value parse_arguments(const std::string& input) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream stream(input.empty() ? "{}" : input);
  if (!Json::parseFromStream(builder, stream, &root, &errors) || !root.isObject()) {
    throw std::runtime_error("tool arguments are not a JSON object: " + errors);
  }
  return root;
}

Json::Value ok_result(const Json::Value& data = Json::Value(Json::objectValue)) {
  Json::Value root(Json::objectValue);
  root["ok"] = true;
  root["data"] = data;
  return root;
}

Json::Value error_result(const std::string& message) {
  Json::Value root(Json::objectValue);
  root["ok"] = false;
  root["error"] = message;
  return root;
}

Json::Value function_tool(const std::string& name,
                          const std::string& description,
                          const Json::Value& properties,
                          const std::vector<std::string>& required = {}) {
  Json::Value tool(Json::objectValue);
  tool["type"] = "function";
  tool["function"]["name"] = name;
  tool["function"]["description"] = description;
  tool["function"]["parameters"]["type"] = "object";
  tool["function"]["parameters"]["properties"] = properties;
  tool["function"]["parameters"]["additionalProperties"] = false;
  Json::Value required_json(Json::arrayValue);
  for (const auto& field : required) required_json.append(field);
  tool["function"]["parameters"]["required"] = required_json;
  return tool;
}

Json::Value string_property(const std::string& description) {
  Json::Value value(Json::objectValue);
  value["type"] = "string";
  value["description"] = description;
  return value;
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  auto root_it = root.begin();
  auto candidate_it = candidate.begin();
  for (; root_it != root.end(); ++root_it, ++candidate_it) {
    if (candidate_it == candidate.end() || *root_it != *candidate_it) return false;
  }
  return true;
}

bool is_public_address(const sockaddr* address) {
  if (address->sa_family == AF_INET) {
    auto value = ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr);
    const unsigned first = value >> 24;
    const unsigned second = (value >> 16) & 0xff;
    if (first == 0 || first == 10 || first == 127 || first >= 224) return false;
    if (first == 100 && second >= 64 && second <= 127) return false;
    if (first == 169 && second == 254) return false;
    if (first == 172 && second >= 16 && second <= 31) return false;
    if (first == 192 && second == 168) return false;
    if (first == 198 && (second == 18 || second == 19)) return false;
    return true;
  }
  if (address->sa_family == AF_INET6) {
    const auto& bytes = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr.s6_addr;
    bool all_zero = true;
    for (int i = 0; i < 16; ++i) all_zero = all_zero && bytes[i] == 0;
    if (all_zero) return false;
    if (IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr) ||
        IN6_IS_ADDR_LINKLOCAL(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr) ||
        IN6_IS_ADDR_MULTICAST(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr)) return false;
    if ((bytes[0] & 0xfe) == 0xfc) return false;
    return true;
  }
  return false;
}

struct SafeUrl {
  std::string url;
  std::string host;
  std::string port;
  std::vector<std::string> resolve;
};

SafeUrl inspect_url(const std::string& url) {
  CURLU* parsed = curl_url();
  if (!parsed) throw std::runtime_error("cannot parse URL");
  auto cleanup = [&] { curl_url_cleanup(parsed); };
  if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) {
    cleanup();
    throw std::runtime_error("invalid URL");
  }
  char* scheme_raw = nullptr;
  char* host_raw = nullptr;
  char* port_raw = nullptr;
  if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme_raw, 0) != CURLUE_OK ||
      curl_url_get(parsed, CURLUPART_HOST, &host_raw, 0) != CURLUE_OK) {
    if (scheme_raw) curl_free(scheme_raw);
    if (host_raw) curl_free(host_raw);
    cleanup();
    throw std::runtime_error("URL must include scheme and host");
  }
  std::string scheme(scheme_raw);
  std::string host(host_raw);
  curl_free(scheme_raw);
  curl_free(host_raw);
  if (scheme != "http" && scheme != "https") {
    cleanup();
    throw std::runtime_error("only http and https URLs are allowed");
  }
  if (curl_url_get(parsed, CURLUPART_PORT, &port_raw, CURLU_DEFAULT_PORT) != CURLUE_OK) {
    cleanup();
    throw std::runtime_error("cannot determine URL port");
  }
  std::string port(port_raw);
  curl_free(port_raw);
  cleanup();
  std::string lower = host;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  if (lower == "localhost" || lower.ends_with(".localhost") || lower.ends_with(".local")) {
    throw std::runtime_error("local network URLs are blocked");
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  const int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
  if (status != 0) throw std::runtime_error("DNS lookup failed: " + std::string(gai_strerror(status)));
  std::vector<std::string> pins;
  for (auto* entry = addresses; entry; entry = entry->ai_next) {
    if (!is_public_address(entry->ai_addr)) {
      freeaddrinfo(addresses);
      throw std::runtime_error("private, loopback, link-local and multicast addresses are blocked");
    }
    std::array<char, INET6_ADDRSTRLEN> buffer{};
    const void* source = entry->ai_family == AF_INET
                             ? static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr)
                             : static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(entry->ai_addr)->sin6_addr);
    if (inet_ntop(entry->ai_family, source, buffer.data(), buffer.size())) {
      std::string ip(buffer.data());
      if (entry->ai_family == AF_INET6) ip = "[" + ip + "]";
      pins.push_back(host + ":" + port + ":" + ip);
    }
  }
  freeaddrinfo(addresses);
  if (pins.empty()) throw std::runtime_error("host resolved to no usable address");
  return {url, host, port, pins};
}

std::string resolve_redirect(const std::string& base, const std::string& location) {
  CURLU* parsed = curl_url();
  if (!parsed) throw std::runtime_error("cannot resolve redirect");
  if (curl_url_set(parsed, CURLUPART_URL, base.c_str(), 0) != CURLUE_OK ||
      curl_url_set(parsed, CURLUPART_URL, location.c_str(), 0) != CURLUE_OK) {
    curl_url_cleanup(parsed);
    throw std::runtime_error("invalid redirect URL");
  }
  char* result = nullptr;
  if (curl_url_get(parsed, CURLUPART_URL, &result, 0) != CURLUE_OK) {
    curl_url_cleanup(parsed);
    throw std::runtime_error("invalid redirect URL");
  }
  std::string output(result);
  curl_free(result);
  curl_url_cleanup(parsed);
  return output;
}

std::string safe_fetch(const std::string& initial_url, int timeout, std::size_t max_bytes) {
  HttpClient http;
  std::string url = initial_url;
  for (int redirects = 0; redirects <= 5; ++redirects) {
    auto safe = inspect_url(url);
    auto response = http.request("GET", url, {"Accept: text/html, text/plain, application/json"},
                                 {}, timeout, max_bytes, false, safe.resolve);
    if (response.status >= 300 && response.status < 400) {
      auto location = response.headers.find("location");
      if (location == response.headers.end()) throw std::runtime_error("redirect without Location header");
      url = resolve_redirect(url, location->second);
      continue;
    }
    if (response.status < 200 || response.status >= 300) {
      throw std::runtime_error("HTTP " + std::to_string(response.status));
    }
    return response.body;
  }
  throw std::runtime_error("too many redirects");
}

std::string strip_html(const std::string& html) {
  std::string text = std::regex_replace(html, std::regex("<(script|style)[^>]*>[\\s\\S]*?</\\1>",
                                                       std::regex::icase), " ");
  text = std::regex_replace(text, std::regex("<[^>]+>"), " ");
  const std::pair<const char*, const char*> entities[] = {
      {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"},
      {"&nbsp;", " "}};
  for (const auto& [entity, replacement] : entities) {
    std::size_t position = 0;
    while ((position = text.find(entity, position)) != std::string::npos) {
      text.replace(position, std::strlen(entity), replacement);
      position += std::strlen(replacement);
    }
  }
  text = std::regex_replace(text, std::regex("[ \\t]+"), " ");
  text = std::regex_replace(text, std::regex("\\n[ \\t]+"), "\n");
  text = std::regex_replace(text, std::regex("\\n{3,}"), "\n\n");
  return text;
}

}  // namespace

ToolExecutor::ToolExecutor(std::filesystem::path root, Approval approval)
    : root_(std::filesystem::canonical(std::move(root))),
      approval_(approval ? std::move(approval) : terminal_approval) {
  if (!std::filesystem::is_directory(root_)) throw std::runtime_error("tool root is not a directory");
}

Json::Value ToolExecutor::schemas(Access access, bool allow_escalation) const {
  Json::Value tools(Json::arrayValue);
  Json::Value read(Json::objectValue);
  read["path"] = string_property("UTF-8 file path relative to the workspace");
  read["offset"]["type"] = "integer";
  read["offset"]["minimum"] = 0;
  read["limit"]["type"] = "integer";
  read["limit"]["minimum"] = 1;
  read["limit"]["maximum"] = 1048576;
  tools.append(function_tool("read_file", "Read part of a file inside the workspace.", read, {"path"}));

  Json::Value list(Json::objectValue);
  list["path"] = string_property("Directory relative to the workspace; defaults to .");
  list["recursive"]["type"] = "boolean";
  list["max_entries"]["type"] = "integer";
  list["max_entries"]["minimum"] = 1;
  list["max_entries"]["maximum"] = 2000;
  tools.append(function_tool("list_files", "List files and directories inside the workspace.", list));

  Json::Value text_search(Json::objectValue);
  text_search["query"] = string_property("Literal text to find");
  text_search["path"] = string_property("Directory relative to the workspace; defaults to .");
  text_search["max_results"]["type"] = "integer";
  text_search["max_results"]["minimum"] = 1;
  text_search["max_results"]["maximum"] = 500;
  tools.append(function_tool("search_text",
                             "Search regular workspace files for literal text without changing files.",
                             text_search, {"query"}));

  Json::Value readonly_command(Json::objectValue);
  readonly_command["command"]["type"] = "string";
  readonly_command["command"]["enum"] = Json::Value(Json::arrayValue);
  for (const auto* name : {"pwd", "ls", "rg", "stat", "file", "wc",
                           "head", "tail", "git", "nvidia-smi", "uname",
                           "lscpu", "free", "df", "uptime"}) {
    readonly_command["command"]["enum"].append(name);
  }
  readonly_command["arguments"]["type"] = "array";
  readonly_command["arguments"]["items"]["type"] = "string";
  readonly_command["timeout_seconds"]["type"] = "integer";
  readonly_command["timeout_seconds"]["minimum"] = 1;
  readonly_command["timeout_seconds"]["maximum"] = 60;
  tools.append(function_tool("run_readonly_command",
                             "Run one allowlisted read-only command in a read-only workspace sandbox.",
                             readonly_command, {"command"}));

  if (access == Access::read_only) {
    if (!allow_escalation) return tools;
    Json::Value request(Json::objectValue);
    request["reason"] = string_property("Why write or unrestricted command access is necessary");
    request["operation"] = string_property("Concrete operation that will be performed after approval");
    request["suggested_scope"]["type"] = "string";
    request["suggested_scope"]["enum"].append("once");
    request["suggested_scope"]["enum"].append("conversation");
    tools.append(function_tool(
        "request_do_mode",
        "Ask the user to grant do access. This request never grants access by itself.",
        request, {"reason", "operation", "suggested_scope"}));
    return tools;
  }

  Json::Value write(Json::objectValue);
  write["path"] = string_property("File path relative to the workspace");
  write["content"] = string_property("Exact content to write");
  write["append"]["type"] = "boolean";
  tools.append(function_tool("write_file", "Write or append a file inside the workspace.", write,
                             {"path", "content"}));

  Json::Value command(Json::objectValue);
  command["command"] = string_property("Shell command to execute");
  command["timeout_seconds"]["type"] = "integer";
  command["timeout_seconds"]["minimum"] = 1;
  command["timeout_seconds"]["maximum"] = 300;
  command["elevated"]["type"] = "boolean";
  command["elevated"]["description"] =
      "Request one-time user approval to run outside the workspace sandbox, still as the current user";
  command["reason"] = string_property("Why running outside the workspace sandbox is necessary");
  tools.append(function_tool("run_command", "Run a command. It is sandboxed unless elevated is approved.",
                             command, {"command"}));

  Json::Value fetch(Json::objectValue);
  fetch["url"] = string_property("Public http/https URL");
  fetch["max_bytes"]["type"] = "integer";
  fetch["max_bytes"]["minimum"] = 1;
  fetch["max_bytes"]["maximum"] = 2097152;
  tools.append(function_tool("fetch_http", "Fetch a public HTTP resource with SSRF and size protections.",
                             fetch, {"url"}));
  tools.append(function_tool("browse_page", "Open a public web page and return readable text.",
                             fetch, {"url"}));

  Json::Value search(Json::objectValue);
  search["query"] = string_property("Web search query");
  tools.append(function_tool("web_search", "Search the public web and return text results.", search,
                             {"query"}));
  return tools;
}

std::filesystem::path ToolExecutor::checked_path(const std::string& input, bool for_create) const {
  if (input.empty()) throw std::runtime_error("path cannot be empty");
  std::filesystem::path relative(input);
  if (relative.is_absolute()) throw std::runtime_error("absolute paths are not allowed");
  std::filesystem::path candidate;
  if (for_create && !std::filesystem::exists(root_ / relative)) {
    auto parent = std::filesystem::weakly_canonical((root_ / relative).parent_path());
    candidate = parent / relative.filename();
  } else {
    candidate = std::filesystem::weakly_canonical(root_ / relative);
  }
  if (!path_is_within(root_, candidate)) throw std::runtime_error("path escapes the workspace");
  return candidate;
}

std::string ToolExecutor::execute(const std::string& name, const std::string& arguments,
                                  Access access) {
  try {
    const auto args = parse_arguments(arguments);
    if (name == "read_file") return read_file(args);
    if (name == "list_files") return list_files(args);
    if (name == "search_text") return search_text(args);
    if (name == "run_readonly_command") return run_readonly_command(args);
    if (access == Access::read_only) {
      return json_string(error_result("tool requires do mode: " + name));
    }
    if (name == "write_file") return write_file(args);
    if (name == "run_command") return run_command(args);
    if (name == "fetch_http") return fetch_http(args);
    if (name == "browse_page") return browse_page(args);
    if (name == "web_search") return web_search(args);
    return json_string(error_result("unknown tool: " + name));
  } catch (const std::exception& error) {
    return json_string(error_result(error.what()));
  }
}

std::string ToolExecutor::read_file(const Json::Value& args) {
  auto path = checked_path(args.get("path", "").asString(), false);
  if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) {
    throw std::runtime_error("path is not a regular file");
  }
  const auto offset = std::max<Json::Int64>(0, args.get("offset", 0).asInt64());
  const auto limit = std::clamp<Json::Int64>(args.get("limit", 262144).asInt64(), 1, 1048576);
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open file");
  input.seekg(offset);
  std::string content(static_cast<std::size_t>(limit), '\0');
  input.read(content.data(), static_cast<std::streamsize>(limit));
  content.resize(static_cast<std::size_t>(input.gcount()));
  Json::Value data(Json::objectValue);
  data["path"] = std::filesystem::relative(path, root_).string();
  data["offset"] = static_cast<Json::Int64>(offset);
  data["content"] = content;
  data["truncated"] = !input.eof();
  return json_string(ok_result(data));
}

std::string ToolExecutor::write_file(const Json::Value& args) {
  auto relative = args.get("path", "").asString();
  auto path = checked_path(relative, true);
  auto content = args.get("content", "").asString();
  if (content.size() > 4ULL * 1024 * 1024) throw std::runtime_error("write exceeds 4 MiB limit");
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) throw std::runtime_error("cannot create parent directory: " + ec.message());
  path = checked_path(relative, true);
  if (std::filesystem::exists(path) && std::filesystem::is_symlink(path)) {
    throw std::runtime_error("refusing to write through a symlink");
  }
  const bool append = args.get("append", false).asBool();
  const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | (append ? O_APPEND : O_TRUNC);
  int fd = ::open(path.c_str(), flags, 0600);
  if (fd < 0) throw std::runtime_error("cannot open file: " + std::string(std::strerror(errno)));
  std::size_t written = 0;
  while (written < content.size()) {
    auto count = ::write(fd, content.data() + written, content.size() - written);
    if (count < 0) {
      auto message = std::string(std::strerror(errno));
      ::close(fd);
      throw std::runtime_error("write failed: " + message);
    }
    written += static_cast<std::size_t>(count);
  }
  ::fsync(fd);
  ::close(fd);
  Json::Value data(Json::objectValue);
  data["path"] = std::filesystem::relative(path, root_).string();
  data["bytes"] = static_cast<Json::UInt64>(content.size());
  data["append"] = append;
  return json_string(ok_result(data));
}

std::string ToolExecutor::list_files(const Json::Value& args) {
  auto path = checked_path(args.get("path", ".").asString(), false);
  if (!std::filesystem::is_directory(path)) throw std::runtime_error("path is not a directory");
  const bool recursive = args.get("recursive", false).asBool();
  const auto maximum = std::clamp(args.get("max_entries", 500).asInt(), 1, 2000);
  Json::Value entries(Json::arrayValue);
  auto add = [&](const std::filesystem::directory_entry& entry) {
    if (entries.size() >= static_cast<Json::ArrayIndex>(maximum)) return;
    Json::Value item(Json::objectValue);
    item["path"] = std::filesystem::relative(entry.path(), root_).string();
    std::error_code ec;
    auto status = entry.symlink_status(ec);
    item["type"] = std::filesystem::is_symlink(status) ? "symlink"
                    : std::filesystem::is_directory(status) ? "directory"
                    : std::filesystem::is_regular_file(status) ? "file"
                                                               : "other";
    if (std::filesystem::is_regular_file(status)) item["size"] = static_cast<Json::UInt64>(entry.file_size(ec));
    entries.append(item);
  };
  std::error_code ec;
  if (recursive) {
    std::filesystem::recursive_directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied, ec);
    for (const auto& entry : iterator) {
      if (entries.size() >= static_cast<Json::ArrayIndex>(maximum)) break;
      add(entry);
      if (entry.is_symlink(ec)) iterator.disable_recursion_pending();
    }
  } else {
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
      if (entries.size() >= static_cast<Json::ArrayIndex>(maximum)) break;
      add(entry);
    }
  }
  Json::Value data(Json::objectValue);
  data["entries"] = entries;
  data["truncated"] = entries.size() >= static_cast<Json::ArrayIndex>(maximum);
  return json_string(ok_result(data));
}

std::string ToolExecutor::search_text(const Json::Value& args) {
  const auto query = args.get("query", "").asString();
  if (query.empty()) throw std::runtime_error("query cannot be empty");
  const auto base = checked_path(args.get("path", ".").asString(), false);
  if (!std::filesystem::is_directory(base)) throw std::runtime_error("path is not a directory");
  const int maximum = std::clamp(args.get("max_results", 100).asInt(), 1, 500);
  Json::Value matches(Json::arrayValue);
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      base, std::filesystem::directory_options::skip_permission_denied, error);
  for (const auto& entry : iterator) {
    if (matches.size() >= static_cast<Json::ArrayIndex>(maximum)) break;
    const auto status = entry.symlink_status(error);
    if (std::filesystem::is_symlink(status)) {
      if (entry.is_directory(error)) iterator.disable_recursion_pending();
      continue;
    }
    if (!std::filesystem::is_regular_file(status) ||
        entry.file_size(error) > 4ULL * 1024 * 1024) {
      continue;
    }
    std::ifstream input(entry.path());
    if (!input) continue;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      if (line.find('\0') != std::string::npos) break;
      if (line.find(query) == std::string::npos) continue;
      Json::Value match(Json::objectValue);
      match["path"] = std::filesystem::relative(entry.path(), root_).string();
      match["line"] = line_number;
      match["text"] = line.substr(0, 2000);
      matches.append(match);
      if (matches.size() >= static_cast<Json::ArrayIndex>(maximum)) break;
    }
  }
  Json::Value data(Json::objectValue);
  data["matches"] = matches;
  data["truncated"] = matches.size() >= static_cast<Json::ArrayIndex>(maximum);
  return json_string(ok_result(data));
}

namespace {

bool readonly_workspace_path(const std::filesystem::path& root, const std::string& value) {
  if (value.empty() || value.front() == '-') return false;
  const std::filesystem::path path(value);
  if (path.is_absolute()) return false;
  return path_is_within(root, std::filesystem::weakly_canonical(root / path));
}

bool unsigned_number(const std::string& value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isdigit(character);
         });
}

std::vector<std::string> readonly_command(const std::filesystem::path& root,
                                          const Json::Value& args) {
  const auto name = args.get("command", "").asString();
  if (!args["arguments"].isNull() && !args["arguments"].isArray()) {
    throw std::runtime_error("arguments must be an array");
  }
  std::vector<std::string> input;
  for (const auto& item : args["arguments"]) {
    if (!item.isString() || item.asString().find('\0') != std::string::npos) {
      throw std::runtime_error("command arguments must be strings without NUL bytes");
    }
    input.push_back(item.asString());
  }
  std::vector<std::string> command;
  auto require_paths = [&](std::size_t begin) {
    for (std::size_t index = begin; index < input.size(); ++index) {
      if (!readonly_workspace_path(root, input[index])) {
        throw std::runtime_error("only relative workspace paths are allowed");
      }
    }
  };
  if (name == "pwd") {
    if (!input.empty()) throw std::runtime_error("pwd accepts no arguments");
    command = {"/usr/bin/pwd"};
  } else if (name == "ls") {
    static const std::unordered_set<std::string> allowed = {
        "-a", "-A", "-l", "-h", "-la", "-al", "-lh", "-hl",
        "-lah", "-lha", "-alh", "-ahl", "-hal", "-hla"};
    std::size_t first_path = 0;
    while (first_path < input.size() && input[first_path].starts_with('-')) {
      if (!allowed.contains(input[first_path])) throw std::runtime_error("unsupported ls option");
      ++first_path;
    }
    require_paths(first_path);
    command = {"/usr/bin/ls"};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "stat" || name == "file" || name == "wc" ||
             name == "head" || name == "tail") {
    std::size_t first_path = 0;
    if (name == "file" && first_path < input.size() && input[first_path] == "-b") {
      ++first_path;
    } else if (name == "wc") {
      static const std::unordered_set<std::string> allowed = {"-c", "-l", "-w", "-m"};
      while (first_path < input.size() && input[first_path].starts_with('-')) {
        if (!allowed.contains(input[first_path])) throw std::runtime_error("unsupported wc option");
        ++first_path;
      }
    } else if (name == "head" || name == "tail") {
      if (first_path < input.size() &&
          (input[first_path] == "-n" || input[first_path] == "-c")) {
        ++first_path;
        if (first_path >= input.size() || !unsigned_number(input[first_path])) {
          throw std::runtime_error("numeric option requires a non-negative integer");
        }
        ++first_path;
      }
    }
    require_paths(first_path);
    command = {"/usr/bin/" + name};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "rg") {
    static const std::unordered_set<std::string> allowed = {
        "-n", "-i", "-F", "--hidden", "--no-heading"};
    std::size_t pattern = 0;
    while (pattern < input.size() && input[pattern].starts_with('-')) {
      if (!allowed.contains(input[pattern])) throw std::runtime_error("unsupported rg option");
      ++pattern;
    }
    if (pattern >= input.size()) throw std::runtime_error("rg requires a pattern");
    require_paths(pattern + 1);
    command = {"/usr/bin/rg"};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "git") {
    if (input.empty()) throw std::runtime_error("git requires a read-only subcommand");
    static const std::unordered_set<std::string> subcommands = {
        "status", "diff", "log", "show"};
    static const std::unordered_set<std::string> options = {
        "--short", "--branch", "--porcelain", "--cached", "--stat", "--name-only",
        "--name-status", "--oneline", "--decorate", "--no-patch"};
    if (!subcommands.contains(input.front())) throw std::runtime_error("unsupported git subcommand");
    bool paths = false;
    for (std::size_t index = 1; index < input.size(); ++index) {
      if (input[index] == "--") { paths = true; continue; }
      if (paths) {
        if (!readonly_workspace_path(root, input[index])) throw std::runtime_error("invalid git path");
      } else if (input[index].starts_with('-')) {
        if (!options.contains(input[index]) &&
            !std::regex_match(input[index], std::regex("-n[0-9]+"))) {
          throw std::runtime_error("unsupported git option");
        }
      } else if (!std::regex_match(input[index], std::regex("[A-Za-z0-9_./~^{}:-]+"))) {
        throw std::runtime_error("invalid git revision");
      }
    }
    command = {"/usr/bin/git", "-c", "core.fsmonitor=false", "-c", "core.pager=cat",
               "-c", "diff.external=", input.front()};
    if (input.front() == "diff" || input.front() == "show" || input.front() == "log") {
      command.push_back("--no-ext-diff");
      command.push_back("--no-textconv");
    }
    command.insert(command.end(), input.begin() + 1, input.end());
  } else if (name == "nvidia-smi") {
    static const std::unordered_set<std::string> simple = {
        "-L", "--list-gpus", "--help-query-gpu"};
    static const std::unordered_set<std::string> fields = {
        "name", "uuid", "driver_version", "compute_cap", "memory.total",
        "memory.used", "memory.free", "utilization.gpu", "utilization.memory",
        "temperature.gpu", "power.draw", "power.limit"};
    if (input.size() == 1 && simple.contains(input.front())) {
      command = {"/usr/bin/nvidia-smi", input.front()};
    } else if (input.empty()) {
      command = {"/usr/bin/nvidia-smi"};
    } else {
      std::string query;
      std::string format;
      for (const auto& argument : input) {
        if (argument.starts_with("--query-gpu=") && query.empty()) {
          query = argument.substr(12);
          std::istringstream names(query);
          std::string field;
          while (std::getline(names, field, ',')) {
            if (!fields.contains(field)) throw std::runtime_error("unsupported nvidia-smi field");
          }
        } else if (argument.starts_with("--format=") && format.empty()) {
          format = argument.substr(9);
          if (format != "csv" && format != "csv,noheader" &&
              format != "csv,nounits" && format != "csv,noheader,nounits") {
            throw std::runtime_error("unsupported nvidia-smi format");
          }
        } else {
          throw std::runtime_error("unsupported nvidia-smi option");
        }
      }
      if (query.empty()) throw std::runtime_error("nvidia-smi query requires --query-gpu");
      command = {"/usr/bin/nvidia-smi", "--query-gpu=" + query,
                 "--format=" + (format.empty() ? "csv" : format)};
    }
  } else if (name == "uname") {
    static const std::unordered_set<std::string> allowed = {
        "-a", "--all", "-s", "--kernel-name", "-r", "--kernel-release",
        "-v", "--kernel-version", "-m", "--machine", "-p", "--processor",
        "-i", "--hardware-platform", "-o", "--operating-system"};
    for (const auto& argument : input) {
      if (!allowed.contains(argument)) throw std::runtime_error("unsupported uname option");
    }
    command = {"/usr/bin/uname"};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "lscpu") {
    static const std::unordered_set<std::string> allowed = {"-J", "--json"};
    for (const auto& argument : input) {
      if (!allowed.contains(argument)) throw std::runtime_error("unsupported lscpu option");
    }
    command = {"/usr/bin/lscpu"};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "free") {
    static const std::unordered_set<std::string> allowed = {
        "-b", "--bytes", "-k", "--kibi", "-m", "--mebi", "-g",
        "--gibi", "-h", "--human", "--si", "-w", "--wide"};
    for (const auto& argument : input) {
      if (!allowed.contains(argument)) throw std::runtime_error("unsupported free option");
    }
    command = {"/usr/bin/free"};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "df") {
    static const std::unordered_set<std::string> allowed = {
        "-h", "--human-readable", "-H", "--si", "-i", "--inodes",
        "-T", "--print-type", "-P", "--portability"};
    for (const auto& argument : input) {
      if (!allowed.contains(argument)) throw std::runtime_error("unsupported df option");
    }
    command = {"/usr/bin/df"};
    command.insert(command.end(), input.begin(), input.end());
  } else if (name == "uptime") {
    static const std::unordered_set<std::string> allowed = {
        "-p", "--pretty", "-s", "--since"};
    if (input.size() > 1 || (!input.empty() && !allowed.contains(input.front()))) {
      throw std::runtime_error("unsupported uptime option");
    }
    command = {"/usr/bin/uptime"};
    command.insert(command.end(), input.begin(), input.end());
  } else {
    throw std::runtime_error("command is not allowlisted");
  }
  return command;
}

CommandResult run_program(std::vector<std::string> program,
                          const std::filesystem::path& cwd,
                          int timeout_seconds,
                          bool sandboxed,
                          std::size_t max_output,
                          bool clean_environment,
                          bool readonly_workspace) {
  if (program.empty()) throw std::runtime_error("program cannot be empty");
  int pipes[2];
  if (::pipe2(pipes, O_CLOEXEC) != 0) throw std::runtime_error("cannot create command pipe");
  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipes[0]);
    ::close(pipes[1]);
    throw std::runtime_error("cannot fork command");
  }
  if (pid == 0) {
    ::setpgid(0, 0);
    ::dup2(pipes[1], STDOUT_FILENO);
    ::dup2(pipes[1], STDERR_FILENO);
    ::close(pipes[0]);
    ::close(pipes[1]);
    std::vector<std::string> arguments;
    if (sandboxed) {
      const std::string root = cwd.string();
      arguments = {
          "/usr/bin/bwrap", "--die-with-parent", "--new-session", "--unshare-pid",
          "--unshare-uts", "--unshare-ipc", "--clearenv", "--setenv", "PATH",
          "/usr/local/bin:/usr/bin", "--setenv", "HOME", root, "--setenv", "LANG",
          "C.UTF-8", "--ro-bind", "/usr", "/usr", "--ro-bind-try", "/lib", "/lib",
          "--ro-bind-try", "/lib64", "/lib64", "--ro-bind", "/etc", "/etc",
          "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
          readonly_workspace ? "--ro-bind" : "--bind", root, root, "--chdir", root};
      arguments.insert(arguments.end(), program.begin(), program.end());
    } else {
      if (::chdir(cwd.c_str()) != 0) _exit(126);
      if (clean_environment) {
        ::clearenv();
        ::setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
        ::setenv("HOME", cwd.c_str(), 1);
        ::setenv("LANG", "C.UTF-8", 1);
      }
      arguments = std::move(program);
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    _exit(127);
  }
  ::close(pipes[1]);
  ::fcntl(pipes[0], F_SETFL, ::fcntl(pipes[0], F_GETFL) | O_NONBLOCK);
  CommandResult result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  bool done = false;
  while (!done) {
    std::array<char, 8192> buffer{};
    while (result.output.size() < max_output) {
      auto count = ::read(pipes[0], buffer.data(),
                          std::min(buffer.size(), max_output - result.output.size()));
      if (count > 0) result.output.append(buffer.data(), static_cast<std::size_t>(count));
      else break;
    }
    int status = 0;
    auto waited = ::waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
      else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
      done = true;
      continue;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      ::kill(-pid, SIGKILL);
      ::waitpid(pid, &status, 0);
      result.exit_code = 124;
      result.timed_out = true;
      done = true;
      continue;
    }
    pollfd descriptor{pipes[0], POLLIN, 0};
    ::poll(&descriptor, 1, 50);
  }
  std::array<char, 8192> tail{};
  while (result.output.size() < max_output) {
    auto count = ::read(pipes[0], tail.data(),
                        std::min(tail.size(), max_output - result.output.size()));
    if (count <= 0) break;
    result.output.append(tail.data(), static_cast<std::size_t>(count));
  }
  ::close(pipes[0]);
  if (result.output.size() == max_output) result.output += "\n[output truncated]";
  return result;
}

}  // namespace

std::string ToolExecutor::run_readonly_command(const Json::Value& args) {
  const auto command = readonly_command(root_, args);
  const int timeout = std::clamp(args.get("timeout_seconds", 30).asInt(), 1, 60);
  const bool system_status = command.front() == "/usr/bin/nvidia-smi" ||
      command.front() == "/usr/bin/uname" || command.front() == "/usr/bin/lscpu" ||
      command.front() == "/usr/bin/free" || command.front() == "/usr/bin/df" ||
      command.front() == "/usr/bin/uptime";
  auto result = run_program(command, root_, timeout, !system_status, 1024ULL * 1024,
                            system_status, true);
  Json::Value data(Json::objectValue);
  data["exit_code"] = result.exit_code;
  data["output"] = result.output;
  data["timed_out"] = result.timed_out;
  data["read_only"] = true;
  return json_string(ok_result(data));
}

CommandResult ToolExecutor::run_process(const std::string& command,
                                        const std::filesystem::path& cwd,
                                        int timeout_seconds,
                                        bool sandboxed,
                                        std::size_t max_output,
                                        bool clean_environment,
                                        bool readonly_workspace) {
  return run_program({"/usr/bin/sh", "-lc", command}, cwd, timeout_seconds, sandboxed,
                     max_output, clean_environment, readonly_workspace);
}

std::string ToolExecutor::run_command(const Json::Value& args) {
  auto command = args.get("command", "").asString();
  if (command.empty()) throw std::runtime_error("command cannot be empty");
  const int timeout = std::clamp(args.get("timeout_seconds", 60).asInt(), 1, 300);
  const bool elevated = args.get("elevated", false).asBool();
  const auto reason = args.get("reason", "No reason provided").asString();
  if (elevated && !approval_(command, root_, reason)) {
    return json_string(error_result("user denied one-time execution outside the sandbox"));
  }
  auto result = run_process(command, root_, timeout, !elevated, 1024ULL * 1024, elevated);
  Json::Value data(Json::objectValue);
  data["exit_code"] = result.exit_code;
  data["output"] = result.output;
  data["timed_out"] = result.timed_out;
  data["sandboxed"] = !elevated;
  return json_string(ok_result(data));
}

std::string ToolExecutor::fetch_http(const Json::Value& args) {
  const auto url = args.get("url", "").asString();
  if (url.empty()) throw std::runtime_error("URL cannot be empty");
  const auto maximum = std::clamp<Json::Int64>(args.get("max_bytes", 1048576).asInt64(), 1, 2097152);
  auto body = safe_fetch(url, 30, static_cast<std::size_t>(maximum));
  Json::Value data(Json::objectValue);
  data["url"] = url;
  data["content"] = body;
  data["bytes"] = static_cast<Json::UInt64>(body.size());
  return json_string(ok_result(data));
}

std::string ToolExecutor::browse_page(const Json::Value& args) {
  const auto url = args.get("url", "").asString();
  if (url.empty()) throw std::runtime_error("URL cannot be empty");
  const auto maximum = std::clamp<Json::Int64>(args.get("max_bytes", 1048576).asInt64(), 1, 2097152);
  auto body = safe_fetch(url, 30, static_cast<std::size_t>(maximum));
  auto text = strip_html(body);
  if (text.size() > 200000) text.resize(200000);
  Json::Value data(Json::objectValue);
  data["url"] = url;
  data["content"] = text;
  return json_string(ok_result(data));
}

std::string ToolExecutor::web_search(const Json::Value& args) {
  auto query = args.get("query", "").asString();
  if (query.empty()) throw std::runtime_error("query cannot be empty");
  auto url = "https://html.duckduckgo.com/html/?q=" + HttpClient::url_encode(query);
  auto html = safe_fetch(url, 30, 1024ULL * 1024);
  auto text = strip_html(html);
  if (text.size() > 100000) text.resize(100000);
  Json::Value data(Json::objectValue);
  data["query"] = query;
  data["content"] = text;
  return json_string(ok_result(data));
}

bool ToolExecutor::terminal_approval(const std::string& command,
                                     const std::filesystem::path& cwd,
                                     const std::string& reason) {
  int fd = ::open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (fd < 0) return false;
  std::string safe_command = command;
  auto sanitize = [](std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char c) {
                                 return c == 0x1b || (c < 0x20 && c != '\n' && c != '\t');
                               }),
                value.end());
    return value;
  };
  safe_command = sanitize(std::move(safe_command));
  auto safe_reason = sanitize(reason);
  std::string prompt = "\nAI requests one-time execution outside the workspace sandbox.\n"
                       "reason: " + safe_reason + "\ncwd: " + cwd.string() +
                       "\nenvironment: clean (PATH, HOME, LANG only)\ncommand:\n" + safe_command +
                       "\nRun once as your current user? [y/N] ";
  ::write(fd, prompt.data(), prompt.size());
  char answer[16] = {};
  auto count = ::read(fd, answer, sizeof(answer) - 1);
  ::close(fd);
  if (count <= 0) return false;
  std::string value(answer, static_cast<std::size_t>(count));
  return value == "y\n" || value == "Y\n" || value == "yes\n" || value == "YES\n";
}

}  // namespace ask
