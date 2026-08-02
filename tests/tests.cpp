#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sys/stat.h>

#include "ask/cli.hpp"
#include "ask/config.hpp"
#include "ask/prompt_template.hpp"
#include "ask/session.hpp"
#include "ask/token_estimator.hpp"
#include "ask/tools.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/ask-tests-XXXXXX";
  char* path = ::mkdtemp(pattern.data());
  if (!path) throw std::runtime_error("mkdtemp failed");
  return path;
}

void test_cli() {
  const char* arguments[] = {"ask", "--do", "--no-stream", "--provider", "local", "--model=m", "hello", "world"};
  auto options = ask::parse_cli(8, const_cast<char**>(arguments));
  expect(options.do_mode, "--do enables do mode");
  expect(options.provider == "local", "provider option is parsed");
  expect(options.model == "m", "model option is parsed");
  expect(options.no_stream, "--no-stream is parsed");
  expect(options.prompt == "hello world", "positional prompt is joined");

  const char* resume[] = {"ask", "resume", "abc", "--model", "new"};
  auto resumed = ask::parse_cli(5, const_cast<char**>(resume));
  expect(resumed.resume && resumed.resume_id == "abc", "resume id is parsed");
  expect(resumed.model == "new", "resume accepts a model override");

  bool rejected = false;
  const char* invalid[] = {"ask", "--wat"};
  try {
    (void)ask::parse_cli(2, const_cast<char**>(invalid));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  expect(rejected, "unknown options are rejected");
}

void test_config(const std::filesystem::path& root) {
  auto path = root / "config" / "config.json";
  ask::ConfigStore store(path);
  auto config = ask::ConfigStore::defaults();
  config.default_provider = "ollama";
  config.default_model = "stale-model";
  auto* ollama = config.find_provider("ollama");
  expect(ollama != nullptr, "default config includes Ollama");
  ollama->enabled = true;
  ollama->default_model = "qwen3:8b";
  ollama->headers["X-Test"] = "yes";
  ollama->model_capabilities["qwen3:8b"] = {
      .tools = false, .streaming = true, .thinking = false, .temperature = true,
      .top_p = false, .json = false, .context_window = 8192};
  config.settings.temperature = 0.25;
  config.settings.top_p = 0.85;
  config.settings.reasoning_effort = "high";
  config.settings.thinking_budget_tokens = 2048;
  config.settings.stream_output = false;
  config.settings.custom_parameters["response_format"]["type"] = "json_object";
  config.settings.conversation_entry_mode = "automatic";
  config.settings.judge_provider = "ollama";
  config.settings.judge_model = "qwen3:8b";
  store.save(config);
  auto loaded = store.load();
  expect(loaded.default_provider == "ollama", "config default provider round trips");
  expect(loaded.default_model == "qwen3:8b",
         "global default model follows the default provider");
  expect(loaded.find_provider("ollama")->headers.at("X-Test") == "yes", "headers round trip");
  const auto loaded_capabilities = loaded.find_provider("ollama")->model_capabilities.at("qwen3:8b");
  expect(!loaded_capabilities.tools && loaded_capabilities.streaming &&
             !loaded_capabilities.thinking && !loaded_capabilities.top_p &&
             !loaded_capabilities.json && loaded_capabilities.context_window == 8192,
         "model capability registry round trips");
  expect(ask::capabilities_for_model(*loaded.find_provider("ollama"), "qwen3:8b").context_window == 8192,
         "registered model capabilities take precedence over provider defaults");
  expect(!ask::capabilities_for_model(*loaded.find_provider("ollama"), "o3-mini").temperature,
         "built-in model capability inference disables unsupported sampling");
  expect(loaded.settings.temperature && *loaded.settings.temperature == 0.25,
         "temperature round trips");
  expect(loaded.settings.top_p && *loaded.settings.top_p == 0.85, "top_p round trips");
  expect(loaded.settings.reasoning_effort == "high", "reasoning effort round trips");
  expect(loaded.settings.thinking_budget_tokens == 2048, "thinking budget round trips");
  expect(!loaded.settings.stream_output, "stream output preference round trips");
  expect(loaded.settings.custom_parameters["response_format"]["type"] == "json_object",
         "custom request parameters round trip");
  expect(loaded.settings.conversation_entry_mode == "automatic",
         "conversation entry mode round trips");
  expect(loaded.settings.judge_provider == "ollama" &&
             loaded.settings.judge_model == "qwen3:8b",
         "judge provider and model round trip");
  struct stat info {};
  expect(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
         "config is stored with mode 0600");

  auto invalid = ask::config_to_json(ask::ConfigStore::defaults());
  invalid["settings"]["temperature"] = -5.0;
  invalid["settings"]["top_p"] = 9.0;
  invalid["settings"]["reasoning_effort"] = "impossible";
  invalid["settings"]["thinking_budget_tokens"] = -12;
  invalid["settings"]["max_output_tokens"] = 2000000;
  invalid["settings"]["custom_parameters"] = Json::Value(Json::arrayValue);
  invalid["settings"]["conversation_entry_mode"] = "impossible";
  invalid["settings"]["judge_provider"] = "missing";
  invalid["settings"]["judge_model"] = "missing-model";
  {
    std::ofstream output(path);
    output << invalid;
  }
  auto sanitized = store.load();
  expect(sanitized.settings.temperature && *sanitized.settings.temperature == 0.0,
         "temperature is clamped to its lower boundary");
  expect(sanitized.settings.top_p && *sanitized.settings.top_p == 1.0,
         "top_p is clamped to its upper boundary");
  expect(sanitized.settings.reasoning_effort == "default",
         "invalid reasoning effort falls back to provider default");
  expect(sanitized.settings.thinking_budget_tokens == 0,
         "negative thinking budget falls back to automatic");
  expect(sanitized.settings.max_output_tokens == 1000000,
         "maximum output tokens are clamped to the supported upper boundary");
  expect(sanitized.settings.custom_parameters.isObject() &&
             sanitized.settings.custom_parameters.empty(),
         "non-object custom parameters fall back to an empty object");
  expect(sanitized.settings.conversation_entry_mode == "always_continue",
         "invalid conversation entry mode preserves legacy behavior");
  expect(sanitized.settings.judge_provider == sanitized.default_provider &&
             sanitized.settings.judge_model == sanitized.default_model,
         "invalid judge selection falls back to the default model");

  auto legacy = ask::config_to_json(ask::ConfigStore::defaults());
  legacy["settings"].removeMember("temperature");
  legacy["settings"].removeMember("top_p");
  legacy["settings"].removeMember("reasoning_effort");
  legacy["settings"].removeMember("thinking_budget_tokens");
  legacy["settings"].removeMember("stream_output");
  legacy["settings"].removeMember("custom_parameters");
  legacy["settings"].removeMember("conversation_entry_mode");
  legacy["settings"].removeMember("judge_provider");
  legacy["settings"].removeMember("judge_model");
  {
    std::ofstream output(path);
    output << legacy;
  }
  auto compatible = store.load();
  expect(!compatible.settings.temperature && !compatible.settings.top_p,
         "legacy configs keep provider sampling defaults");
  expect(compatible.settings.reasoning_effort == "default" &&
             compatible.settings.thinking_budget_tokens == 0 && compatible.settings.stream_output,
         "legacy configs receive safe AI call defaults");
  expect(compatible.settings.conversation_entry_mode == "always_continue",
         "legacy configs continue entering conversations");
}

void test_sessions(const std::filesystem::path& root) {
  auto path = root / "data" / "sessions.db";
  ask::SessionStore store(path);
  ask::Session session;
  session.id = "test-session";
  session.title = "A test";
  session.provider = "local";
  session.model = "mock";
  session.cwd = root.string();
  session.summary = "old facts";
  session.active_from = 1;
  session.total_prompt_tokens = 120;
  session.total_cached_tokens = 90;
  session.total_cache_creation_tokens = 20;
  session.request_count = 3;
  session.messages = {{"user", "hello", {}, {}},
                      {"assistant", "calling", {}, {{"call-1", "read_file", "{\"path\":\"a\"}"}}},
                      {"tool", "result", "call-1", {}}};
  store.save(session);
  auto loaded = store.load(session.id);
  expect(loaded.has_value(), "saved session can be loaded");
  expect(loaded->messages.size() == 3, "all session messages round trip");
  expect(loaded->messages[1].tool_calls[0].name == "read_file", "tool calls round trip");
  expect(loaded->active_from == 1 && loaded->summary == "old facts", "compact view round trips");
  expect(loaded->total_prompt_tokens == 120 && loaded->total_cached_tokens == 90 &&
             loaded->total_cache_creation_tokens == 20 && loaded->request_count == 3,
         "conversation cache statistics round trip");
  expect(!store.list().empty(), "session appears in listing");
  store.mark_quick_resume(session);
  struct stat quick_info {};
  expect(::stat(store.quick_resume_path().c_str(), &quick_info) == 0 &&
             (quick_info.st_mode & 0777) == 0600,
         "quick resume state is stored with mode 0600");
  auto quick = store.consume_quick_resume(root);
  expect(quick && quick->id == session.id && quick->messages.size() == session.messages.size(),
         "quick resume snapshot preserves the conversation");
  expect(!store.consume_quick_resume(root), "quick resume state is consumed only once");
  store.mark_quick_resume(session);
  expect(!store.consume_quick_resume(root / "other"),
         "quick resume state cannot cross working directories");
  expect(store.remove(session.id), "session can be deleted");
  struct stat info {};
  expect(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
         "session database is mode 0600");
}

void test_tools(const std::filesystem::path& root) {
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ask::ToolExecutor tools(workspace, [](const auto&, const auto&, const auto&) { return false; });
  auto readonly_schemas = tools.schemas(ask::ToolExecutor::Access::read_only);
  std::string readonly_names;
  for (const auto& schema : readonly_schemas) {
    readonly_names += schema["function"].get("name", "").asString() + " ";
  }
  expect(readonly_names.find("read_file") != std::string::npos &&
             readonly_names.find("list_files") != std::string::npos &&
             readonly_names.find("search_text") != std::string::npos &&
             readonly_names.find("run_readonly_command") != std::string::npos &&
             readonly_names.find("request_do_mode") != std::string::npos,
         "ask mode exposes read-only tools and the explicit upgrade request");
  expect(readonly_names.find("write_file") == std::string::npos &&
             readonly_names.find("run_command") == std::string::npos,
         "ask mode schema does not expose mutating tools");
  auto forced_readonly = tools.schemas(ask::ToolExecutor::Access::read_only, false);
  std::string forced_names;
  for (const auto& schema : forced_readonly) {
    forced_names += schema["function"].get("name", "").asString() + " ";
  }
  expect(forced_names.find("request_do_mode") == std::string::npos,
         "forced read-only turns cannot request an upgrade");
  auto write = tools.execute("write_file", R"({"path":"nested/file.txt","content":"hello"})");
  expect(write.find("\"ok\":true") != std::string::npos, "write_file succeeds inside workspace");
  auto read = tools.execute("read_file", R"({"path":"nested/file.txt"})");
  expect(read.find("hello") != std::string::npos, "read_file returns written content");
  auto search = tools.execute("search_text", R"({"query":"hello","path":"."})",
                              ask::ToolExecutor::Access::read_only);
  expect(search.find("nested/file.txt") != std::string::npos,
         "ask mode can search workspace text");
  auto blocked_write = tools.execute(
      "write_file", R"({"path":"blocked.txt","content":"no"})",
      ask::ToolExecutor::Access::read_only);
  expect(blocked_write.find("requires do mode") != std::string::npos &&
             !std::filesystem::exists(workspace / "blocked.txt"),
         "ask mode rejects a forged mutating tool call");
  auto readonly_head = tools.execute(
      "run_readonly_command",
      R"({"command":"head","arguments":["-n","1","nested/file.txt"]})",
      ask::ToolExecutor::Access::read_only);
  expect(readonly_head.find("hello") != std::string::npos,
         "ask mode runs an allowlisted read-only command");
  auto system_status = tools.execute(
      "run_readonly_command", R"({"command":"uname","arguments":["-s"]})",
      ask::ToolExecutor::Access::read_only);
  expect(system_status.find("Linux") != std::string::npos,
         "ask mode can inspect allowlisted system status");
  auto blocked_system_option = tools.execute(
      "run_readonly_command", R"({"command":"nvidia-smi","arguments":["-pm","1"]})",
      ask::ToolExecutor::Access::read_only);
  expect(blocked_system_option.find("unsupported nvidia-smi option") != std::string::npos,
         "read-only system status rejects mutating nvidia-smi options");
  auto shell_escape = tools.execute(
      "run_readonly_command",
      R"({"command":"ls","arguments":[".; touch escaped.txt"]})",
      ask::ToolExecutor::Access::read_only);
  expect(!shell_escape.empty() && !std::filesystem::exists(workspace / "escaped.txt"),
         "read-only command arguments cannot inject shell operations");
  auto git_write = tools.execute(
      "run_readonly_command",
      R"({"command":"git","arguments":["checkout","HEAD"]})",
      ask::ToolExecutor::Access::read_only);
  expect(git_write.find("unsupported git subcommand") != std::string::npos,
         "read-only command rejects mutating git subcommands");
  auto escape = tools.execute("read_file", R"({"path":"../../etc/passwd"})");
  expect(escape.find("\"ok\":false") != std::string::npos, "path traversal is rejected");

  auto external = root / "outside.txt";
  std::ofstream(external) << "secret";
  std::filesystem::create_symlink(external, workspace / "escape-link");
  auto symlink = tools.execute("read_file", R"({"path":"escape-link"})");
  expect(symlink.find("\"ok\":false") != std::string::npos, "external symlink is rejected");

  auto git_workspace = root / "git-workspace";
  std::filesystem::create_directories(git_workspace);
  std::ofstream(git_workspace / "tracked.txt") << "before\n";
  const auto git_path = git_workspace.string();
  expect(std::system(("git -C '" + git_path + "' init -q").c_str()) == 0,
         "test Git repository initializes");
  expect(std::system(("git -C '" + git_path + "' config user.email test@example.com && "
                      "git -C '" + git_path + "' config user.name test").c_str()) == 0,
         "test Git identity configures");
  expect(std::system(("git -C '" + git_path + "' add tracked.txt && git -C '" + git_path +
                      "' commit -q -m initial").c_str()) == 0,
         "test Git repository commits");
  std::ofstream(git_workspace / "tracked.txt", std::ios::trunc) << "after\n";
  std::ofstream(git_workspace / "untracked.txt") << "new\n";
  ask::ToolExecutor git_tools(git_workspace);
  auto git_schemas = git_tools.schemas(ask::ToolExecutor::Access::read_only);
  std::string git_names;
  for (const auto& schema : git_schemas) git_names += schema["function"].get("name", "").asString() + " ";
  expect(git_names.find("git_status") != std::string::npos &&
             git_names.find("git_diff") != std::string::npos &&
             git_names.find("git_log") != std::string::npos &&
             git_names.find("git_show") != std::string::npos,
         "ask mode exposes native Git read-only tools");
  auto status = git_tools.execute("git_status", R"({"include_untracked":true})",
                                  ask::ToolExecutor::Access::read_only);
  expect(status.find("\"conflicted\":false") != std::string::npos &&
             status.find("tracked.txt") != std::string::npos &&
             status.find("untracked.txt") != std::string::npos,
         "git_status returns structured branch and file state");
  auto diff = git_tools.execute("git_diff", R"({})", ask::ToolExecutor::Access::read_only);
  expect(diff.find("after") != std::string::npos && diff.find("before") != std::string::npos,
         "git_diff returns working tree changes");
  auto log = git_tools.execute("git_log", R"({"limit":5})", ask::ToolExecutor::Access::read_only);
  expect(log.find("initial") != std::string::npos, "git_log returns recent commits");
  auto show = git_tools.execute("git_show", R"({"revision":"HEAD"})",
                                ask::ToolExecutor::Access::read_only);
  expect(show.find("initial") != std::string::npos, "git_show returns a selected revision");
  auto invalid_revision = git_tools.execute("git_show", R"({"revision":"--exec=touch"})",
                                            ask::ToolExecutor::Access::read_only);
  expect(invalid_revision.find("invalid git revision") != std::string::npos,
         "git_show rejects option injection");

  auto command = tools.execute("run_command", R"({"command":"printf sandbox-ok","timeout_seconds":5})");
  expect(command.find("sandbox-ok") != std::string::npos, "sandbox command runs");
  auto outside_path = external.string();
  auto hidden = tools.execute("run_command", "{\"command\":\"test ! -e '" + outside_path +
                                              "' && printf hidden\",\"timeout_seconds\":5}");
  expect(hidden.find("hidden") != std::string::npos, "sandbox cannot see files outside workspace");
  ::setenv("ASK_TEST_SECRET", "must-not-leak", 1);
  auto environment = tools.execute(
      "run_command", R"({"command":"printf ${ASK_TEST_SECRET-unset}","timeout_seconds":5})");
  expect(environment.find("unset") != std::string::npos &&
             environment.find("must-not-leak") == std::string::npos,
         "sandbox command environment excludes host secrets");
  auto denied = tools.execute("run_command", R"({"command":"id","elevated":true})");
  expect(denied.find("denied") != std::string::npos, "elevated command requires approval");
  std::string approved_reason;
  ask::ToolExecutor approved(
      workspace, [&](const auto&, const auto&, const auto& reason) {
        approved_reason = reason;
        return true;
      });
  auto elevated = approved.execute(
      "run_command",
      R"({"command":"printf ${ASK_TEST_SECRET-unset}","elevated":true,"reason":"test reason"})");
  expect(approved_reason == "test reason", "elevation reason is passed to the approval boundary");
  expect(elevated.find("unset") != std::string::npos &&
             elevated.find("must-not-leak") == std::string::npos,
         "approved command still receives a clean environment");
  auto private_http = tools.execute("fetch_http", R"({"url":"http://127.0.0.1/secret"})");
  expect(private_http.find("\"ok\":false") != std::string::npos,
         "HTTP tool rejects loopback destinations");
}

void test_prompt_template() {
  ask::TemplateContext context;
  context.cwd = "/tmp/project";
  context.model = "gpt-4o";
  context.provider = "openai";
  context.provider_name = "OpenAI";
  context.protocol = "openai";
  context.do_mode = true;
  context.read_only = false;
  context.has_tools = true;
  context.streaming = true;

  expect(ask::expand_template("plain text", context) == "plain text",
         "templates without markers pass through unchanged");
  expect(ask::expand_template("{{cwd}}/x", context) == "/tmp/project/x",
         "template variables are replaced");
  expect(ask::expand_template("{{#if do_mode}}yes{{else}}no{{/if}}", context) == "yes",
         "true if branch renders");
  expect(ask::expand_template("{{#unless read_only}}full{{/unless}}", context) == "full",
         "unless renders when the condition is false");
  expect(ask::expand_template("{{#if provider:openai}}{{provider_name}}{{/if}}", context) ==
             "OpenAI",
         "keyed conditions can select by provider");

  context.do_mode = false;
  context.read_only = true;
  expect(ask::expand_template("{{#if do_mode}}yes{{else}}no{{/if}}", context) == "no",
         "false if branch renders the else body");
  expect(ask::expand_template("{{#if do_mode}}a{{#if read_only}}b{{else}}c{{/if}}d"
                              "{{else}}e{{/if}}",
                              context) == "e",
         "nested condition blocks preserve parent visibility");
  expect(ask::expand_template("\\{{cwd}}", context) == "{{cwd}}",
         "backslash escapes the template marker");
  expect(ask::expand_template("{{unknown}}", context) == "{{unknown}}",
         "unknown variables remain literal");
}

void test_token_estimator() {
  ask::Provider provider;
  provider.id = "local";
  provider.protocol = "openai";

  const std::string chinese =
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";
  const auto openai_estimate =
      ask::estimate_tokens(provider, "gpt-4o", {}, chinese, Json::Value());
  expect(openai_estimate < 20, "UTF-8 text is not estimated by byte count");
  expect(openai_estimate > 0, "token estimates are always positive");

  provider.protocol = "anthropic";
  const auto anthropic_estimate =
      ask::estimate_tokens(provider, "claude-3-5-sonnet", {}, chinese, Json::Value());
  provider.protocol = "gemini";
  const auto gemini_estimate =
      ask::estimate_tokens(provider, "gemini-2.0-flash", {}, chinese, Json::Value());
  expect(anthropic_estimate < gemini_estimate,
         "model-family profiles calibrate token estimates");

  provider.protocol = "openai";
  std::vector<ask::Message> messages{{"user", "hello", {}, {}}};
  Json::Value tools(Json::arrayValue);
  Json::Value tool(Json::objectValue);
  tool["type"] = "function";
  tool["function"]["name"] = "read_file";
  tool["function"]["description"] = "Read a file.";
  tool["function"]["parameters"]["type"] = "object";
  tools.append(tool);
  const auto without_tools =
      ask::estimate_tokens(provider, "gpt-4o", messages, "hello", Json::Value());
  const auto with_tools =
      ask::estimate_tokens(provider, "gpt-4o", messages, "hello", tools);
  expect(with_tools > without_tools, "tool schemas are included in the estimate");
}

}  // namespace

int main() {
  auto root = temporary_directory();
  try {
    test_cli();
    test_config(root);
    test_sessions(root);
    test_tools(root);
    test_prompt_template();
    test_token_estimator();
  } catch (const std::exception& error) {
    std::cerr << "UNCAUGHT: " << error.what() << '\n';
    ++failures;
  }
  std::filesystem::remove_all(root);
  if (failures) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
