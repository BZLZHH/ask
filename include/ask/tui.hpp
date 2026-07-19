#pragma once

#include <optional>
#include <string>

#include "ask/client.hpp"
#include "ask/config.hpp"
#include "ask/session.hpp"

namespace ask {

class Tui {
 public:
  static bool configure(ConfigStore& store, ChatClient* client = nullptr);
  static std::optional<std::string> choose_session(SessionStore& store);
  static bool choose_model(const Config& config, std::string& provider, std::string& model);
};

}  // namespace ask
