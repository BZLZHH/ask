#pragma once

#include <csignal>
#include <filesystem>
#include <optional>
#include <string>

#include "ask/client.hpp"
#include "ask/config.hpp"
#include "ask/session.hpp"
#include "ask/tools.hpp"

namespace ask {

struct RunOptions {
  std::string provider;
  std::string model;
  bool do_mode{false};
  bool json_output{false};
  bool quiet{false};
  bool stream_output{true};
};

class Conversation {
 public:
  Conversation(ConfigStore& config_store,
               SessionStore& session_store,
               Config config,
               Session session,
               RunOptions options);

  bool send(const std::string& input, std::optional<bool> one_shot_do = std::nullopt);
  int repl();
  bool compact(bool automatic = false);
  const Session& session() const { return session_; }

 private:
  bool maybe_compact(const std::string& pending, bool do_mode,
                     const std::string& system_prompt);
  std::string handle_do_mode_request(const std::string& arguments,
                                     bool& allow_once_for_next_batch);
  bool execute_shell(const std::string& command);
  void persist();
  const Provider& provider() const;
  std::string read_multiline(const std::string& first);

  ConfigStore& config_store_;
  SessionStore& session_store_;
  Config config_;
  Session session_;
  RunOptions options_;
  ChatClient client_;
  ToolExecutor tools_;
  volatile std::sig_atomic_t cancelled_{0};
};

}  // namespace ask
