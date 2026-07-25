#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sys/stat.h>

#include "ask/cli.hpp"
#include "ask/config.hpp"
#include "ask/session.hpp"
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
  session.messages = {{"user", "hello", {}, {}},
                      {"assistant", "calling", {}, {{"call-1", "read_file", "{\"path\":\"a\"}"}}},
                      {"tool", "result", "call-1", {}}};
  store.save(session);
  auto loaded = store.load(session.id);
  expect(loaded.has_value(), "saved session can be loaded");
  expect(loaded->messages.size() == 3, "all session messages round trip");
  expect(loaded->messages[1].tool_calls[0].name == "read_file", "tool calls round trip");
  expect(loaded->active_from == 1 && loaded->summary == "old facts", "compact view round trips");
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
  auto write = tools.execute("write_file", R"({"path":"nested/file.txt","content":"hello"})");
  expect(write.find("\"ok\":true") != std::string::npos, "write_file succeeds inside workspace");
  auto read = tools.execute("read_file", R"({"path":"nested/file.txt"})");
  expect(read.find("hello") != std::string::npos, "read_file returns written content");
  auto escape = tools.execute("read_file", R"({"path":"../../etc/passwd"})");
  expect(escape.find("\"ok\":false") != std::string::npos, "path traversal is rejected");

  auto external = root / "outside.txt";
  std::ofstream(external) << "secret";
  std::filesystem::create_symlink(external, workspace / "escape-link");
  auto symlink = tools.execute("read_file", R"({"path":"escape-link"})");
  expect(symlink.find("\"ok\":false") != std::string::npos, "external symlink is rejected");

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

}  // namespace

int main() {
  auto root = temporary_directory();
  try {
    test_cli();
    test_config(root);
    test_sessions(root);
    test_tools(root);
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
