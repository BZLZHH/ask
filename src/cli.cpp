#include "ask/cli.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#include "ask/client.hpp"
#include "ask/config.hpp"
#include "ask/repl.hpp"
#include "ask/session.hpp"
#include "ask/tui.hpp"

namespace ask {
namespace {

std::string option_value(int argc, char** argv, int& index, const std::string& name) {
  if (index + 1 >= argc) throw std::invalid_argument(name + " requires a value");
  return argv[++index];
}

std::string join(const std::vector<std::string>& values) {
  std::string output;
  for (const auto& value : values) {
    if (!output.empty()) output += ' ';
    output += value;
  }
  return output;
}

std::string read_stdin(std::size_t maximum = 16ULL * 1024 * 1024) {
  std::string result;
  std::array<char, 8192> buffer{};
  while (std::cin) {
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = std::cin.gcount();
    if (count > 0) {
      if (result.size() + static_cast<std::size_t>(count) > maximum) {
        throw std::runtime_error("stdin exceeds the 16 MiB limit");
      }
      result.append(buffer.data(), static_cast<std::size_t>(count));
    }
  }
  if (result.find('\0') != std::string::npos) throw std::runtime_error("binary stdin is not supported");
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
  return result;
}

void attach_tty_to_stdin() {
  int tty = ::open("/dev/tty", O_RDONLY | O_CLOEXEC);
  if (tty < 0) throw std::runtime_error("--interactive requires a controlling terminal");
  if (::dup2(tty, STDIN_FILENO) < 0) {
    ::close(tty);
    throw std::runtime_error("cannot attach interactive terminal");
  }
  ::close(tty);
  std::cin.clear();
}

}  // namespace

CliOptions parse_cli(int argc, char** argv) {
  CliOptions options;
  std::vector<std::string> positional;
  bool literal = false;
  for (int index = 1; index < argc; ++index) {
    std::string argument = argv[index];
    if (!literal && argument == "--") {
      literal = true;
    } else if (!literal && (argument == "-h" || argument == "--help")) {
      options.help = true;
    } else if (!literal && argument == "--version") {
      options.version = true;
    } else if (!literal && argument == "--config") {
      options.config = true;
    } else if (!literal && argument == "--do") {
      options.do_mode = true;
    } else if (!literal && (argument == "-i" || argument == "--interactive")) {
      options.interactive = true;
    } else if (!literal && argument == "--no-repl") {
      options.no_repl = true;
    } else if (!literal && argument == "--no-stream") {
      options.no_stream = true;
    } else if (!literal && argument == "--json") {
      options.json = true;
    } else if (!literal && (argument == "-q" || argument == "--quiet")) {
      options.quiet = true;
    } else if (!literal && (argument == "-p" || argument == "--provider")) {
      options.provider = option_value(argc, argv, index, argument);
    } else if (!literal && argument.rfind("--provider=", 0) == 0) {
      options.provider = argument.substr(11);
    } else if (!literal && (argument == "-m" || argument == "--model")) {
      options.model = option_value(argc, argv, index, argument);
    } else if (!literal && argument.rfind("--model=", 0) == 0) {
      options.model = argument.substr(8);
    } else if (!literal && !argument.empty() && argument.front() == '-') {
      throw std::invalid_argument("unknown option: " + argument);
    } else {
      positional.push_back(argument);
    }
  }
  if (!positional.empty() && positional.front() == "resume" && !literal) {
    options.resume = true;
    if (positional.size() > 2) throw std::invalid_argument("resume accepts at most one session id");
    if (positional.size() == 2) options.resume_id = positional[1];
  } else {
    options.prompt = join(positional);
  }
  const int entry_points = (options.config ? 1 : 0) + (options.resume ? 1 : 0);
  if (entry_points > 1 || (options.config && (!options.prompt.empty() || options.do_mode)) ||
      (options.resume && !options.prompt.empty())) {
    throw std::invalid_argument("--config, resume and a new prompt are mutually exclusive");
  }
  if (options.interactive && options.no_repl) {
    throw std::invalid_argument("--interactive and --no-repl are mutually exclusive");
  }
  return options;
}

std::string usage() {
  return R"USAGE(Usage:
  ask [options] [prompt ...]
  ask --do [options] [prompt ...]
  ask --config
  ask resume [session-id] [--provider ID] [--model MODEL]

Options:
  -p, --provider ID    Override the configured provider for this session
  -m, --model MODEL    Override the configured model for this session
      --do             Start the session with workspace tools enabled
  -i, --interactive    Enter the REPL even when input was piped
      --no-repl        Exit after one response
      --no-stream      Wait for the complete response before printing
      --json           Emit the one-shot result as JSON
  -q, --quiet          Hide tool progress messages
      --config         Open the provider configuration TUI
  -h, --help           Show this help
      --version        Show the version

Piped stdin is combined after an explicit prompt. Non-TTY use is one-shot by default.
)USAGE";
}

int run_cli(const CliOptions& options) {
  if (options.help) {
    std::cout << usage();
    return 0;
  }
  if (options.version) {
    std::cout << "ask " ASK_VERSION << '\n';
    return 0;
  }

  ConfigStore config_store;
  auto config = config_store.load();
  ChatClient client;
  if (options.config) {
    if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO)) {
      throw std::runtime_error("--config requires a terminal");
    }
    Tui::configure(config_store, &client);
    return 0;
  }

  SessionStore sessions;
  Session session;
  bool resumed = false;
  if (options.resume) {
    std::string id = options.resume_id;
    if (id.empty()) {
      if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO)) {
        throw std::runtime_error("resume without a session id requires a terminal");
      }
      auto selected = Tui::choose_session(sessions);
      if (!selected) return 0;
      id = *selected;
    }
    auto loaded = sessions.load(id);
    if (!loaded) throw std::runtime_error("session not found: " + id);
    session = std::move(*loaded);
    resumed = true;
    if (!session.cwd.empty() && session.cwd != std::filesystem::current_path()) {
      std::cerr << "ask: resumed in a different directory; do tools are rooted at "
                << std::filesystem::current_path().string() << '\n';
    }
  } else {
    session.id = SessionStore::new_id();
    session.cwd = std::filesystem::current_path().string();
  }

  RunOptions run;
  run.provider = !options.provider.empty() ? options.provider
                 : resumed && !session.provider.empty() ? session.provider
                                                        : config.default_provider;
  const auto* provider = config.find_provider(run.provider);
  if (!provider) throw std::runtime_error("unknown provider: " + run.provider);
  run.model = !options.model.empty() ? options.model
              : resumed && !session.model.empty() ? session.model
              : run.provider == config.default_provider && !config.default_model.empty()
                  ? config.default_model
                  : provider->default_model;
  run.do_mode = options.do_mode || (resumed && session.do_mode);
  run.json_output = options.json;
  run.quiet = options.quiet;
  run.stream_output = !options.no_stream && !options.json;
  if (run.model.empty()) throw std::runtime_error("no model configured for provider " + run.provider);

  const bool stdin_tty = ::isatty(STDIN_FILENO);
  const bool stdout_tty = ::isatty(STDOUT_FILENO);
  std::string prompt = options.prompt;
  if (!stdin_tty && !resumed) {
    auto piped = read_stdin();
    if (!piped.empty()) prompt += (prompt.empty() ? "" : "\n\n") + piped;
  }
  if (!resumed && prompt.empty() && !stdin_tty) {
    throw std::runtime_error("no prompt was provided on argv or stdin");
  }

  Conversation conversation(config_store, sessions, config, std::move(session), run);
  bool request_ok = true;
  if (!prompt.empty()) request_ok = conversation.send(prompt);

  bool enter_repl = !options.no_repl && (options.interactive || (stdin_tty && stdout_tty));
  if (resumed && stdin_tty && stdout_tty && !options.no_repl) enter_repl = true;
  if (enter_repl) {
    if (!stdin_tty) attach_tty_to_stdin();
    return conversation.repl();
  }
  return request_ok ? 0 : 1;
}

}  // namespace ask
