#pragma once

#include <csignal>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ask/http.hpp"
#include "ask/types.hpp"

namespace ask {

class ChatClient {
 public:
  using TextDelta = std::function<void(std::string_view)>;

  ChatResponse complete(const Provider& provider,
                        const std::string& model,
                        const std::vector<Message>& messages,
                        const std::string& system_prompt,
                        const Json::Value& tools = Json::Value(),
                        int max_output_tokens = 4096) const;

  ChatResponse stream(const Provider& provider,
                      const std::string& model,
                      const std::vector<Message>& messages,
                      const std::string& system_prompt,
                      const Json::Value& tools,
                      int max_output_tokens,
                      const TextDelta& on_text,
                      const volatile std::sig_atomic_t* cancelled = nullptr) const;

  std::vector<std::string> fetch_models(const Provider& provider) const;

 private:
  HttpClient http_;
};

std::size_t estimate_tokens(const std::vector<Message>& messages,
                            const std::string& system_prompt,
                            const Json::Value& tools = Json::Value());

}  // namespace ask
