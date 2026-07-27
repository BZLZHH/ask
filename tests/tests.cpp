#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sys/stat.h>

#include "ask/cli.hpp"
#include "ask/conference.hpp"
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

 const char* conference[] = {"ask", "conference", "review", "the", "release", "--model", "m"};
 auto conference_options = ask::parse_cli(7, const_cast<char**>(conference));
 expect(conference_options.conference && !conference_options.conference_resume,
 "conference command is parsed");
 expect(conference_options.prompt == "review the release" && conference_options.model == "m",
 "conference goal and model are parsed");

 const char* conference_resume[] = {"ask", "conference", "resume", "conference-1"};
 auto resumed_conference = ask::parse_cli(4, const_cast<char**>(conference_resume));
 expect(resumed_conference.conference && resumed_conference.conference_resume &&
 resumed_conference.conference_id == "conference-1",
 "conference resume command is parsed");
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

void test_conferences(const std::filesystem::path& root) {
 const auto directory = root / "data" / "conferences";
 ask::ConferenceStore store(directory);
 auto config = ask::ConfigStore::defaults();
 auto conference = ask::ConferenceEngine::create(
 config, "Choose a safe release plan", root, config.default_provider, config.default_model);
 expect(conference.status == ask::ConferenceStatus::awaiting_setup && conference.participants.size() == 4 &&
 conference.participants.front().kind == "moderator" &&
 conference.participants.front().seat_number == 0 && conference.agenda.size() == 4 &&
 conference.setup.depth == ask::ConferenceDepth::standard && !conference.setup.user_approved,
 "conference creation initializes a reviewable numbered meeting plan");
 store.save(conference);
 auto saved = store.load(conference.id);
 expect(saved && saved->goal == conference.goal && saved->rules == conference.rules &&
 saved->setup.depth == ask::ConferenceDepth::standard &&
 saved->participants[1].provider == conference.participants[1].provider &&
 saved->participants[1].model == conference.participants[1].model,
 "conference plan and per-seat model bindings round trip through persistent storage");
 const auto serialized_conference = ask::conference_to_json(conference);
 expect(!serialized_conference["participants"][0].isMember("response_token_limit"),
 "conference participants no longer impose per-seat response token limits");
 conference.agenda_round = 5;
 conference.autopilot_round_limit = 0;
 store.save(conference);
 saved = store.load(conference.id);
 expect(saved && saved->agenda_round == 5 && saved->autopilot_round_limit == 0,
 "agenda phase progress and unlimited autopilot round trip through storage");
 conference.events.push_back({"stream-test", 1, 0, "discussion", "Moderator", "Moderator",
 "partial", "", "streaming"});
 conference.events.push_back({"advisor-summary-test", 2, 1, "discussion", "Architect #1", "Architect",
 "Plan A reduces rollback risk through phased rollout.", "", "completed"});
 conference.user_questions.push_back({"user-question-test", "Moderator #0", "Which rollout window do you prefer?",
 "objective", {"Weekday", "Weekend"}, 1, 9999999999,
 "pending", ""});
 store.save(conference);
 saved = store.load(conference.id);
 const bool stream_persisted = saved && std::any_of(saved->events.begin(), saved->events.end(),
 [](const auto& event) { return event.id == "stream-test" && event.state == "streaming" &&
 event.content == "partial"; });
 expect(stream_persisted,
 "streaming conference events persist with identity and partial content");
 expect(saved && saved->user_questions.size() == 1 &&
 saved->user_questions.front().type == "objective" &&
 saved->user_questions.front().options == std::vector<std::string>{"Weekday", "Weekend"},
 "structured moderator questions and answer options persist with the conference");
 struct stat conference_directory_info {};
 struct stat conference_file_info {};
 const auto conference_path = directory / (conference.id + ".json");
 expect(::stat(directory.c_str(), &conference_directory_info) == 0 &&
 (conference_directory_info.st_mode & 0777) == 0700 &&
 ::stat(conference_path.c_str(), &conference_file_info) == 0 &&
 (conference_file_info.st_mode & 0777) == 0600,
 "conference storage uses private directory and record permissions");

 ask::ConferenceEngine engine(config, std::move(conference), store);
 engine.start();
 expect(engine.conference().status == ask::ConferenceStatus::awaiting_setup,
 "conference cannot start before the user reviews its plan");
 engine.assign_next_speaker("auditor", "User wants risk review first", true, true);
 expect(engine.conference().next_speaker_id == "auditor" &&
 engine.conference().next_speaker_reason == "User wants risk review first",
 "user can explicitly override the next scheduled speaker");
 auto planned = engine.snapshot();
 planned.participants[1].model = "alternate-advisor-model";
 engine.update_setup(ask::ConferenceDepth::deep, 3, 16, planned.participants);
 expect(engine.conference().setup.depth == ask::ConferenceDepth::deep &&
 engine.conference().setup.agenda_turn_budget == 16 &&
 engine.conference().participants[1].model == "alternate-advisor-model",
 "reviewed meeting depth and per-seat configuration are stored");
 engine.approve_setup();
 expect(engine.conference().status == ask::ConferenceStatus::running &&
 engine.conference().setup.user_approved &&
 engine.conference().next_speaker_id == "moderator",
 "approved meeting plan starts with moderator seat zero");
 engine.interrupt("Weekend");
 expect(engine.conference().status == ask::ConferenceStatus::running &&
 engine.conference().user_questions.front().status == "answered" &&
 engine.conference().user_questions.front().answer == "Weekend" &&
 engine.conference().next_speaker_id == "moderator",
 "a user answer resolves the pending moderator question and returns the floor to the moderator");
 engine.pause();
 expect(engine.conference().status == ask::ConferenceStatus::paused,
 "conference pauses without ending");
 engine.resume();
 engine.interrupt("Require a rollback plan before deciding.");
 expect(engine.conference().status == ask::ConferenceStatus::awaiting_user &&
 engine.conference().events.back().type == "user",
 "user interruption has priority and becomes a visible event");
 engine.focus_agenda(2);
 expect(engine.conference().current_agenda_id == "risks" &&
 engine.conference().agenda[2].status == "active",
 "agenda focus is structured and persistent");
 engine.update_rules("Auditor responds before every decision.");
 expect(engine.conference().rules == "Auditor responds before every decision.",
 "rule changes are applied to future turns");
 engine.update_goal("Choose a rollout plan with a verified rollback path");
 expect(engine.conference().goal == "Choose a rollout plan with a verified rollback path",
 "goal changes are stored as user-directed conference state");
 engine.set_autopilot(true, 999, {"write_file", "unknown_tool"});
 expect(engine.conference().autopilot_enabled && engine.conference().autopilot_round_limit == 50 &&
 engine.conference().autopilot_preauthorized_tools == std::vector<std::string>{"write_file"},
 "autopilot persists only known selected full tools and clamps its budget");
 engine.set_autopilot(true, 0, {});
 expect(engine.conference().autopilot_enabled && engine.conference().autopilot_round_limit == 0,
 "autopilot accepts an explicit unlimited user-selected mode");
 engine.conference().decisions.push_back("Use a phased rollout");
 engine.resolve_decision(0, "evidence");
 expect(engine.conference().status == ask::ConferenceStatus::awaiting_user &&
 !engine.conference().open_questions.empty(),
 "requesting decision evidence blocks automatic progression");
 engine.resolve_decision(0, "confirmed");
 expect(engine.conference().decisions.front().find("[confirmed]") == 0,
 "user decision confirmation is recorded structurally");
 engine.stop();
 expect(engine.conference().status == ask::ConferenceStatus::stopped,
 "conference can be stopped while retaining history");

 auto loaded = store.load(engine.conference().id);
 expect(loaded && loaded->status == ask::ConferenceStatus::stopped &&
 loaded->current_agenda_id == "risks" && loaded->events.size() >= 10,
 "conference state, agenda, and event history persist after lifecycle changes");
 expect(loaded && loaded->autopilot_enabled && loaded->autopilot_round_limit == 0 &&
 loaded->autopilot_preauthorized_tools.empty(),
 "unlimited autopilot policy round trips through private conference storage");
 expect(loaded && !loaded->decisions.empty() &&
 loaded->decisions.front().find("[confirmed]") == 0 &&
 engine.summary().find("Open questions") != std::string::npos &&
 engine.summary().find(" A risks ") != std::string::npos &&
 engine.summary().find("Agenda and phase conclusions") != std::string::npos,
 "conference summary retains structured sections, agenda conclusions, and participant contributions");
 const auto export_path = engine.export_summary("exports/release-summary.md");
 std::ifstream exported(export_path);
 std::string exported_text((std::istreambuf_iterator<char>(exported)), std::istreambuf_iterator<char>());
 expect(exported_text.find("Choose a rollout plan with a verified rollback path") != std::string::npos &&
 exported_text.find("Auditor responds before every decision.") != std::string::npos,
 "conference summary exports only after the user-visible export action");
 bool rejected_export = false;
 try {
 (void)engine.export_summary("../outside.md");
 } catch (const std::invalid_argument&) { rejected_export = true; }
 expect(rejected_export, "conference export rejects paths outside the workspace");
 expect(!store.load("../../invalid") && !store.remove("../../invalid"),
 "conference store rejects path traversal identifiers");
 expect(store.remove(engine.conference().id), "conference can be removed");
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
 auto restricted_schemas = tools.schemas(ask::ToolExecutor::Access::full, false, {"write_file"});
 std::string restricted_names;
 for (const auto& schema : restricted_schemas) restricted_names += schema["function"].get("name", "").asString() + " ";
 expect(restricted_names.find("write_file") != std::string::npos &&
 restricted_names.find("run_command") == std::string::npos &&
 restricted_names.find("fetch_http") == std::string::npos,
 "full tool schemas enforce a selected autopilot allowlist");
 auto forged_restricted = tools.execute("run_command", R"({"command":"printf should-not-run"})",
 ask::ToolExecutor::Access::full, {"write_file"}, false);
 expect(forged_restricted.find("not preauthorized") != std::string::npos,
 "forged full tool calls are rejected outside the autopilot allowlist");
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

} // namespace

int main() {
 auto root = temporary_directory();
 try {
 test_cli();
 test_config(root);
 test_sessions(root);
 test_conferences(root);
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
