#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <json/json.h>

#include "ask/types.hpp"

namespace ask {

std::size_t estimate_tokens(const Provider& provider, const std::string& model,
                            const std::vector<Message>& messages,
                            const std::string& system_prompt,
                            const Json::Value& tools = Json::Value());

}  // namespace ask
