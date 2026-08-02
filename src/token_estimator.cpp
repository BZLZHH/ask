#include "ask/token_estimator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace ask {
namespace {

constexpr std::size_t kBaseMessageOverhead = 4;
constexpr std::size_t kAnthropicMessageOverhead = 6;
constexpr std::size_t kGeminiMessageOverhead = 8;
constexpr std::size_t kToolCallOverhead = 8;
constexpr double kSafetyFactor = 1.10;

enum class EstimatorProfile {
  openai,
  anthropic,
  gemini,
  generic,
};

std::string lower_ascii(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

EstimatorProfile profile_for(const Provider& provider,
                             const std::string& model) {
  const auto protocol = lower_ascii(provider.protocol);
  const auto model_name = lower_ascii(model);
  if (protocol == "anthropic" ||
      model_name.find("claude") != std::string::npos) {
    return EstimatorProfile::anthropic;
  }
  if (protocol == "gemini" || model_name.find("gemini") != std::string::npos) {
    return EstimatorProfile::gemini;
  }
  return EstimatorProfile::generic;
}

std::uint32_t decode_utf8(std::string_view text, std::size_t& position) {
  const auto first = static_cast<unsigned char>(text[position]);
  if (first < 0x80) {
    ++position;
    return first;
  }
  const auto continuation = [&](std::size_t offset) {
    return static_cast<unsigned char>(text[position + offset]) & 0x3FU;
  };
  if ((first & 0xE0U) == 0xC0U && position + 1 < text.size()) {
    const auto value = ((first & 0x1FU) << 6U) | continuation(1);
    position += 2;
    return value;
  }
  if ((first & 0xF0U) == 0xE0U && position + 2 < text.size()) {
    const auto value =
        ((first & 0x0FU) << 12U) | (continuation(1) << 6U) | continuation(2);
    position += 3;
    return value;
  }
  if ((first & 0xF8U) == 0xF0U && position + 3 < text.size()) {
    const auto value = ((first & 0x07U) << 18U) | (continuation(1) << 12U) |
                       (continuation(2) << 6U) | continuation(3);
    position += 4;
    return value;
  }
  ++position;
  return first;
}

bool is_cjk(std::uint32_t codepoint) {
  return (codepoint >= 0x3000U && codepoint <= 0x303FU) ||
         (codepoint >= 0x3040U && codepoint <= 0x30FFU) ||
         (codepoint >= 0x3400U && codepoint <= 0x4DBFU) ||
         (codepoint >= 0x4E00U && codepoint <= 0x9FFFU) ||
         (codepoint >= 0xAC00U && codepoint <= 0xD7AFU) ||
         (codepoint >= 0xF900U && codepoint <= 0xFAFFU) ||
         (codepoint >= 0xFF00U && codepoint <= 0xFFEFU) ||
         (codepoint >= 0x20000U && codepoint <= 0x2FA1FU);
}

double token_estimate_for_codepoint(std::uint32_t codepoint) {
  if (codepoint < 0x80U) {
    if (std::isalnum(static_cast<unsigned char>(codepoint)))
      return 0.28;
    if (std::isspace(static_cast<unsigned char>(codepoint)))
      return 0.20;
    return 0.38;
  }
  if (is_cjk(codepoint))
    return 0.62;
  if (codepoint >= 0x1F000U && codepoint <= 0x1FAFFU)
    return 1.20;
  return 0.75;
}

double profile_multiplier(EstimatorProfile profile) {
  switch (profile) {
  case EstimatorProfile::anthropic:
    return 0.95;
  case EstimatorProfile::gemini:
    return 1.05;
  case EstimatorProfile::openai:
  case EstimatorProfile::generic:
    return 1.0;
  }
  return 1.0;
}

std::size_t estimate_text_tokens(std::string_view text,
                                 EstimatorProfile profile) {
  double tokens = 0.0;
  for (std::size_t position = 0; position < text.size();) {
    tokens += token_estimate_for_codepoint(decode_utf8(text, position));
  }
  return static_cast<std::size_t>(
      std::ceil(tokens * profile_multiplier(profile)));
}

std::size_t estimate_json_tokens(std::string_view text) {
  return static_cast<std::size_t>(
      std::ceil(static_cast<double>(text.size()) * 0.34));
}

std::string json_string(const Json::Value& value) {
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return Json::writeString(writer, value);
}

} // namespace

std::size_t estimate_tokens(const Provider& provider, const std::string& model,
                            const std::vector<Message>& messages,
                            const std::string& system_prompt,
                            const Json::Value& tools) {
  const auto profile = profile_for(provider, model);
  const auto message_overhead = [&] {
    if (profile == EstimatorProfile::anthropic)
      return kAnthropicMessageOverhead;
    if (profile == EstimatorProfile::gemini)
      return kGeminiMessageOverhead;
    return kBaseMessageOverhead;
  }();

  double tokens =
      static_cast<double>(estimate_text_tokens(system_prompt, profile));
  for (const auto& message : messages) {
    tokens += message_overhead;
    tokens += estimate_text_tokens(message.role, profile);
    tokens += estimate_text_tokens(message.content, profile);
    tokens += estimate_text_tokens(message.tool_call_id, profile);
    for (const auto& call : message.tool_calls) {
      tokens += kToolCallOverhead;
      tokens += estimate_text_tokens(call.id, profile);
      tokens += estimate_text_tokens(call.name, profile);
      tokens += estimate_json_tokens(call.arguments);
    }
  }
  if (!tools.isNull())
    tokens += estimate_json_tokens(json_string(tools));
  return static_cast<std::size_t>(std::ceil(tokens * kSafetyFactor));
}

} // namespace ask
