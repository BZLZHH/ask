#include "ask/client.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "ask/config.hpp"

namespace ask {
namespace {

std::string json_string(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

Json::Value parse_json_text(const std::string& text) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream input(text);
  if (!Json::parseFromStream(builder, input, &root, &errors)) {
    throw std::runtime_error("invalid SSE JSON: " + errors);
  }
  if (root["error"].isString()) throw std::runtime_error(root["error"].asString());
  if (root["error"]["message"].isString()) {
    throw std::runtime_error(root["error"]["message"].asString());
  }
  return root;
}

class SseDecoder {
 public:
  explicit SseDecoder(std::function<void(const std::string&)> handler)
      : handler_(std::move(handler)) {}

  bool feed(std::string_view chunk) {
    buffer_.append(chunk);
    std::size_t newline = 0;
    while ((newline = buffer_.find('\n')) != std::string::npos) {
      auto line = buffer_.substr(0, newline);
      buffer_.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      consume_line(line);
    }
    return true;
  }

  void finish() {
    if (!buffer_.empty()) {
      if (buffer_.back() == '\r') buffer_.pop_back();
      consume_line(buffer_);
      buffer_.clear();
    }
    dispatch();
  }

  bool saw_data() const { return saw_data_; }

 private:
  void consume_line(const std::string& line) {
    if (line.empty()) {
      dispatch();
      return;
    }
    if (line.rfind("data:", 0) != 0) return;
    auto value = line.substr(5);
    if (!value.empty() && value.front() == ' ') value.erase(value.begin());
    if (!data_.empty()) data_ += '\n';
    data_ += value;
  }

  void dispatch() {
    if (data_.empty()) return;
    saw_data_ = true;
    auto value = std::move(data_);
    data_.clear();
    handler_(value);
  }

  std::string buffer_;
  std::string data_;
  std::function<void(const std::string&)> handler_;
  bool saw_data_{false};
};

Json::Value parse_json(const HttpResponse& response) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream input(response.body);
  const bool parsed = Json::parseFromStream(builder, input, &root, &errors);
  if (response.status < 200 || response.status >= 300) {
    std::string message;
    if (parsed && root["error"].isString()) message = root["error"].asString();
    if (parsed && root["error"]["message"].isString()) message = root["error"]["message"].asString();
    if (message.empty()) message = response.body.substr(0, 1000);
    throw std::runtime_error("provider HTTP " + std::to_string(response.status) + ": " + message);
  }
  if (!parsed) {
    throw std::runtime_error("provider returned invalid JSON (HTTP " +
                             std::to_string(response.status) + "): " + errors);
  }
  return root;
}

std::string endpoint(std::string base, const std::string& suffix) {
  while (!base.empty() && base.back() == '/') base.pop_back();
  if (base.size() >= suffix.size() &&
      base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return base;
  }
  if (base.ends_with("/v1") && suffix.rfind("/v1/", 0) == 0) {
    return base + suffix.substr(3);
  }
  return base + suffix;
}

std::vector<std::string> common_headers(const Provider& provider) {
  std::vector<std::string> headers{"Content-Type: application/json", "Accept: application/json"};
  for (const auto& [name, value] : provider.headers) headers.push_back(name + ": " + value);
  return headers;
}

Json::Value openai_message(const Message& message) {
  Json::Value item(Json::objectValue);
  item["role"] = message.role;
  if (message.content.empty() && !message.tool_calls.empty()) {
    item["content"] = Json::nullValue;
  } else {
    item["content"] = message.content;
  }
  if (!message.tool_call_id.empty()) item["tool_call_id"] = message.tool_call_id;
  if (!message.tool_calls.empty()) {
    Json::Value calls(Json::arrayValue);
    for (const auto& call : message.tool_calls) {
      Json::Value value(Json::objectValue);
      value["id"] = call.id;
      value["type"] = "function";
      value["function"]["name"] = call.name;
      value["function"]["arguments"] = call.arguments;
      calls.append(value);
    }
    item["tool_calls"] = calls;
  }
  return item;
}

ChatResponse parse_openai(const Json::Value& root) {
  const auto& choice = root["choices"][0];
  if (choice.isNull()) throw std::runtime_error("provider response has no choices");
  const auto& message = choice["message"];
  ChatResponse response;
  response.content = message.get("content", "").asString();
  response.finish_reason = choice.get("finish_reason", "").asString();
  for (const auto& item : message["tool_calls"]) {
    ToolCall call;
    call.id = item.get("id", "").asString();
    call.name = item["function"].get("name", "").asString();
    call.arguments = item["function"].get("arguments", "{}").asString();
    response.tool_calls.push_back(std::move(call));
  }
  response.usage.prompt_tokens = root["usage"].get("prompt_tokens", 0).asInt();
  response.usage.completion_tokens = root["usage"].get("completion_tokens", 0).asInt();
  response.usage.total_tokens = root["usage"].get("total_tokens", 0).asInt();
  response.raw = root;
  return response;
}

Json::Value anthropic_messages(const std::vector<Message>& messages) {
  Json::Value output(Json::arrayValue);
  for (std::size_t index = 0; index < messages.size(); ++index) {
    const auto& message = messages[index];
    if (message.role == "assistant") {
      Json::Value item(Json::objectValue);
      item["role"] = "assistant";
      Json::Value content(Json::arrayValue);
      if (!message.content.empty()) {
        Json::Value text(Json::objectValue);
        text["type"] = "text";
        text["text"] = message.content;
        content.append(text);
      }
      for (const auto& call : message.tool_calls) {
        Json::Value use(Json::objectValue);
        use["type"] = "tool_use";
        use["id"] = call.id;
        use["name"] = call.name;
        Json::CharReaderBuilder builder;
        Json::Value arguments;
        std::string errors;
        std::istringstream input(call.arguments);
        if (!Json::parseFromStream(builder, input, &arguments, &errors)) arguments = Json::Value(Json::objectValue);
        use["input"] = arguments;
        content.append(use);
      }
      item["content"] = content;
      output.append(item);
    } else if (message.role == "tool") {
      Json::Value item(Json::objectValue);
      item["role"] = "user";
      Json::Value content(Json::arrayValue);
      Json::Value result(Json::objectValue);
      result["type"] = "tool_result";
      result["tool_use_id"] = message.tool_call_id;
      result["content"] = message.content;
      content.append(result);
      item["content"] = content;
      output.append(item);
    } else if (message.role != "system") {
      Json::Value item(Json::objectValue);
      item["role"] = "user";
      item["content"] = message.content;
      output.append(item);
    }
  }
  return output;
}

Json::Value anthropic_tools(const Json::Value& tools) {
  Json::Value output(Json::arrayValue);
  for (const auto& tool : tools) {
    Json::Value item(Json::objectValue);
    item["name"] = tool["function"]["name"];
    item["description"] = tool["function"].get("description", "");
    item["input_schema"] = tool["function"]["parameters"];
    output.append(item);
  }
  return output;
}

ChatResponse parse_anthropic(const Json::Value& root) {
  ChatResponse response;
  for (const auto& block : root["content"]) {
    auto type = block.get("type", "").asString();
    if (type == "text") response.content += block.get("text", "").asString();
    if (type == "tool_use") {
      response.tool_calls.push_back({block.get("id", "").asString(),
                                     block.get("name", "").asString(),
                                     json_string(block["input"])});
    }
  }
  response.finish_reason = root.get("stop_reason", "").asString();
  response.usage.prompt_tokens = root["usage"].get("input_tokens", 0).asInt();
  response.usage.completion_tokens = root["usage"].get("output_tokens", 0).asInt();
  response.usage.total_tokens = response.usage.prompt_tokens + response.usage.completion_tokens;
  response.raw = root;
  return response;
}

Json::Value gemini_contents(const std::vector<Message>& messages) {
  Json::Value output(Json::arrayValue);
  for (const auto& message : messages) {
    if (message.role == "system") continue;
    Json::Value item(Json::objectValue);
    item["role"] = message.role == "assistant" ? "model" : "user";
    Json::Value parts(Json::arrayValue);
    if (!message.content.empty()) {
      Json::Value text(Json::objectValue);
      text["text"] = message.content;
      parts.append(text);
    }
    for (const auto& call : message.tool_calls) {
      Json::Value part(Json::objectValue);
      part["functionCall"]["name"] = call.name;
      Json::CharReaderBuilder builder;
      Json::Value arguments;
      std::string errors;
      std::istringstream input(call.arguments);
      if (!Json::parseFromStream(builder, input, &arguments, &errors)) arguments = Json::Value(Json::objectValue);
      part["functionCall"]["args"] = arguments;
      parts.append(part);
    }
    if (message.role == "tool") {
      Json::Value part(Json::objectValue);
      part["functionResponse"]["name"] = message.tool_call_id;
      part["functionResponse"]["response"]["result"] = message.content;
      parts.append(part);
    }
    item["parts"] = parts;
    output.append(item);
  }
  return output;
}

Json::Value gemini_tools(const Json::Value& tools) {
  Json::Value declarations(Json::arrayValue);
  for (const auto& tool : tools) {
    Json::Value item(Json::objectValue);
    item["name"] = tool["function"]["name"];
    item["description"] = tool["function"].get("description", "");
    item["parameters"] = tool["function"]["parameters"];
    declarations.append(item);
  }
  Json::Value outer(Json::arrayValue);
  Json::Value group(Json::objectValue);
  group["functionDeclarations"] = declarations;
  outer.append(group);
  return outer;
}

ChatResponse parse_gemini(const Json::Value& root) {
  ChatResponse response;
  const auto& candidate = root["candidates"][0];
  if (candidate.isNull()) throw std::runtime_error("Gemini response has no candidates");
  for (const auto& part : candidate["content"]["parts"]) {
    if (part["text"].isString()) response.content += part["text"].asString();
    if (part["functionCall"].isObject()) {
      const auto& call = part["functionCall"];
      auto name = call.get("name", "").asString();
      response.tool_calls.push_back({name, name, json_string(call["args"])});
    }
  }
  response.finish_reason = candidate.get("finishReason", "").asString();
  response.usage.prompt_tokens = root["usageMetadata"].get("promptTokenCount", 0).asInt();
  response.usage.completion_tokens = root["usageMetadata"].get("candidatesTokenCount", 0).asInt();
  response.usage.total_tokens = root["usageMetadata"].get("totalTokenCount", 0).asInt();
  response.raw = root;
  return response;
}

int effective_max_output_tokens(const Settings& settings, int override_value) {
  return override_value > 0 ? override_value : settings.max_output_tokens;
}

bool reasoning_enabled(const Settings& settings) {
  return settings.reasoning_effort != "default" && settings.reasoning_effort != "off";
}

double effort_ratio(const std::string& effort) {
  if (effort == "minimal" || effort == "low") return 0.05;
  if (effort == "medium") return 0.50;
  if (effort == "high") return 0.80;
  if (effort == "xhigh") return 0.90;
  return 0.80;
}

int calculated_thinking_budget(const Settings& settings, int max_output_tokens) {
  if (settings.thinking_budget_tokens > 0) return settings.thinking_budget_tokens;
  return std::max(1, static_cast<int>(max_output_tokens * effort_ratio(settings.reasoning_effort)));
}

bool protected_custom_key(const std::string& key) {
  static const std::set<std::string> keys = {
      "model", "messages", "contents", "system", "systemInstruction",
      "tools", "tool_choice", "stream"};
  return keys.contains(key);
}

void merge_custom_parameters(Json::Value& target, const Json::Value& custom, bool root = true) {
  if (!custom.isObject()) return;
  for (const auto& name : custom.getMemberNames()) {
    if (root && protected_custom_key(name)) continue;
    if (target[name].isObject() && custom[name].isObject()) {
      merge_custom_parameters(target[name], custom[name], false);
    } else {
      target[name] = custom[name];
    }
  }
}

void apply_generation_settings(Json::Value& body, const Provider& provider,
                               const std::string& model, const Settings& settings,
                               int max_output_tokens,
                               const ModelCapabilities& capabilities) {
  const bool openai = provider.protocol == "openai" || provider.protocol == "openai_chat";
  const bool anthropic = provider.protocol == "anthropic";
  const bool gemini = provider.protocol == "gemini";

  if (capabilities.temperature && settings.temperature) {
    if (gemini) body["generationConfig"]["temperature"] = *settings.temperature;
    else body["temperature"] = *settings.temperature;
  }
  if (capabilities.top_p && settings.top_p) {
    if (gemini) body["generationConfig"]["topP"] = *settings.top_p;
    else body["top_p"] = *settings.top_p;
  }

  if (capabilities.thinking && openai && settings.reasoning_effort != "default") {
    if (provider.id == "openrouter") {
      if (settings.reasoning_effort == "off") {
        body["reasoning"]["enabled"] = false;
        body["reasoning"]["exclude"] = true;
      } else {
        body["reasoning"]["effort"] =
            settings.reasoning_effort == "auto" ? "medium" : settings.reasoning_effort;
      }
    } else {
      body["reasoning_effort"] = settings.reasoning_effort == "auto"
                                      ? "medium"
                                      : settings.reasoning_effort == "off"
                                            ? "none"
                                            : settings.reasoning_effort;
    }
  } else if (capabilities.thinking && anthropic && settings.reasoning_effort == "off") {
    body["thinking"]["type"] = "disabled";
  } else if (capabilities.thinking && anthropic && reasoning_enabled(settings)) {
    const int budget = settings.thinking_budget_tokens > 0
                           ? settings.thinking_budget_tokens
                           : std::max(1024, calculated_thinking_budget(settings, max_output_tokens));
    if (budget >= max_output_tokens) {
      throw std::runtime_error(
          "Anthropic thinking budget must be smaller than maximum output tokens");
    }
    body.removeMember("temperature");
    body.removeMember("top_p");
    body["thinking"]["type"] = "enabled";
    body["thinking"]["budget_tokens"] = budget;
  } else if (capabilities.thinking && gemini && settings.reasoning_effort != "default") {
    auto& thinking = body["generationConfig"]["thinkingConfig"];
    thinking["includeThoughts"] = false;
    if (settings.reasoning_effort == "off") {
      if (model.find("flash") != std::string::npos) thinking["thinkingBudget"] = 0;
    } else if (settings.reasoning_effort == "auto" && settings.thinking_budget_tokens == 0) {
      thinking["thinkingBudget"] = -1;
    } else {
      thinking["thinkingBudget"] = calculated_thinking_budget(settings, max_output_tokens);
    }
  }

  merge_custom_parameters(body, settings.custom_parameters);

  if (!capabilities.temperature) {
    body.removeMember("temperature");
    body["generationConfig"].removeMember("temperature");
  }
  if (!capabilities.top_p) {
    body.removeMember("top_p");
    body["generationConfig"].removeMember("topP");
  }
  if (!capabilities.thinking) {
    body.removeMember("reasoning_effort");
    body.removeMember("reasoning");
    body.removeMember("thinking");
    body["generationConfig"].removeMember("thinkingConfig");
  }
  if (!capabilities.json) {
    body.removeMember("response_format");
    body["generationConfig"].removeMember("responseMimeType");
  }

  if (anthropic && body["thinking"].get("type", "").asString() == "enabled") {
    body.removeMember("temperature");
    body.removeMember("top_p");
    const auto& final_budget = body["thinking"]["budget_tokens"];
    const auto& final_max_tokens = body["max_tokens"];
    if (!final_budget.isInt() || final_budget.asInt() < 1 ||
        !final_max_tokens.isInt() || final_max_tokens.asInt() < 1 ||
        final_budget.asInt() >= final_max_tokens.asInt()) {
      throw std::runtime_error(
          "Anthropic thinking budget must be a positive integer smaller than maximum output tokens");
    }
  }
}

}  // namespace

ChatResponse ChatClient::complete(const Provider& provider,
                                  const std::string& model,
                                  const std::vector<Message>& messages,
                                  const std::string& system_prompt,
                                  const Settings& settings,
                                  const Json::Value& tools,
                                  int max_output_tokens_override) const {
  if (provider.base_url.empty()) throw std::runtime_error("provider base URL is empty");
  if (model.empty()) throw std::runtime_error("model is empty");
  const auto capabilities = capabilities_for_model(provider, model);
  auto headers = common_headers(provider);
  auto key = ConfigStore::api_key_for(provider);
  Json::Value body(Json::objectValue);
  std::string url;
  const int max_output_tokens =
      effective_max_output_tokens(settings, max_output_tokens_override);

  if (provider.protocol == "openai" || provider.protocol == "openai_chat") {
    url = endpoint(provider.base_url, "/chat/completions");
    if (!key.empty()) headers.push_back("Authorization: Bearer " + key);
    body["model"] = model;
    Json::Value wire_messages(Json::arrayValue);
    if (!system_prompt.empty()) wire_messages.append(openai_message({"system", system_prompt, {}, {}}));
    for (const auto& message : messages) wire_messages.append(openai_message(message));
    body["messages"] = wire_messages;
    body["stream"] = false;
    body["max_tokens"] = max_output_tokens;
    if (capabilities.tools && tools.isArray() && !tools.empty()) {
      body["tools"] = tools;
      body["tool_choice"] = "auto";
    }
    apply_generation_settings(body, provider, model, settings, max_output_tokens, capabilities);
    return parse_openai(parse_json(http_.request("POST", url, headers, json_string(body),
                                                    provider.timeout_seconds)));
  }

  if (provider.protocol == "anthropic") {
    url = endpoint(provider.base_url, "/v1/messages");
    if (!key.empty()) headers.push_back("x-api-key: " + key);
    headers.push_back("anthropic-version: 2023-06-01");
    body["model"] = model;
    body["system"] = system_prompt;
    body["messages"] = anthropic_messages(messages);
    body["max_tokens"] = max_output_tokens;
    if (capabilities.tools && tools.isArray() && !tools.empty()) body["tools"] = anthropic_tools(tools);
    apply_generation_settings(body, provider, model, settings, max_output_tokens, capabilities);
    return parse_anthropic(parse_json(http_.request("POST", url, headers, json_string(body),
                                                      provider.timeout_seconds)));
  }

  if (provider.protocol == "gemini") {
    url = endpoint(provider.base_url, "/models/") + HttpClient::url_encode(model) + ":generateContent";
    if (!key.empty()) headers.push_back("x-goog-api-key: " + key);
    body["contents"] = gemini_contents(messages);
    if (!system_prompt.empty()) body["systemInstruction"]["parts"][0]["text"] = system_prompt;
    body["generationConfig"]["maxOutputTokens"] = max_output_tokens;
    if (capabilities.tools && tools.isArray() && !tools.empty()) body["tools"] = gemini_tools(tools);
    apply_generation_settings(body, provider, model, settings, max_output_tokens, capabilities);
    return parse_gemini(parse_json(http_.request("POST", url, headers, json_string(body),
                                                   provider.timeout_seconds)));
  }
  throw std::runtime_error("unsupported provider protocol: " + provider.protocol);
}

ChatResponse ChatClient::stream(const Provider& provider,
                                const std::string& model,
                                const std::vector<Message>& messages,
                                const std::string& system_prompt,
                                const Settings& settings,
                                const Json::Value& tools,
                                int max_output_tokens_override,
                                const TextDelta& on_text,
                                const volatile std::sig_atomic_t* cancelled) const {
  if (provider.base_url.empty()) throw std::runtime_error("provider base URL is empty");
  if (model.empty()) throw std::runtime_error("model is empty");
  const auto capabilities = capabilities_for_model(provider, model);
  if (!capabilities.streaming) {
    auto response = complete(provider, model, messages, system_prompt, settings, tools,
                             max_output_tokens_override);
    if (!response.content.empty()) on_text(response.content);
    return response;
  }
  auto headers = common_headers(provider);
  headers.erase(std::remove_if(headers.begin(), headers.end(), [](const std::string& header) {
                  std::string name = header.substr(0, header.find(':'));
                  std::transform(name.begin(), name.end(), name.begin(),
                                 [](unsigned char c) { return std::tolower(c); });
                  return name == "accept";
                }),
                headers.end());
  headers.push_back("Accept: text/event-stream");
  const auto key = ConfigStore::api_key_for(provider);
  Json::Value body(Json::objectValue);
  std::string url;
  ChatResponse response;
  const int max_output_tokens =
      effective_max_output_tokens(settings, max_output_tokens_override);

  if (provider.protocol == "openai" || provider.protocol == "openai_chat") {
    url = endpoint(provider.base_url, "/chat/completions");
    if (!key.empty()) headers.push_back("Authorization: Bearer " + key);
    body["model"] = model;
    Json::Value wire_messages(Json::arrayValue);
    if (!system_prompt.empty()) wire_messages.append(openai_message({"system", system_prompt, {}, {}}));
    for (const auto& message : messages) wire_messages.append(openai_message(message));
    body["messages"] = wire_messages;
    body["stream"] = true;
    body["max_tokens"] = max_output_tokens;
    if (capabilities.tools && tools.isArray() && !tools.empty()) {
      body["tools"] = tools;
      body["tool_choice"] = "auto";
    }
    apply_generation_settings(body, provider, model, settings, max_output_tokens, capabilities);

    SseDecoder decoder([&](const std::string& data) {
      if (data == "[DONE]") return;
      const auto root = parse_json_text(data);
      response.raw = root;
      if (root["usage"].isObject()) {
        response.usage.prompt_tokens = root["usage"].get("prompt_tokens", response.usage.prompt_tokens).asInt();
        response.usage.completion_tokens =
            root["usage"].get("completion_tokens", response.usage.completion_tokens).asInt();
        response.usage.total_tokens = root["usage"].get("total_tokens", response.usage.total_tokens).asInt();
      }
      const auto& choice = root["choices"][0];
      if (choice.isNull()) return;
      if (choice["finish_reason"].isString()) response.finish_reason = choice["finish_reason"].asString();
      const auto& delta = choice["delta"];
      if (delta["content"].isString()) {
        const auto text = delta["content"].asString();
        response.content += text;
        on_text(text);
      }
      for (const auto& item : delta["tool_calls"]) {
        const auto index = item.get("index", 0).asUInt();
        if (index > 1024) throw std::runtime_error("provider returned an invalid tool call index");
        if (response.tool_calls.size() <= index) response.tool_calls.resize(index + 1);
        auto& call = response.tool_calls[index];
        if (item["id"].isString()) call.id = item["id"].asString();
        if (item["function"]["name"].isString()) call.name += item["function"]["name"].asString();
        if (item["function"]["arguments"].isString()) {
          call.arguments += item["function"]["arguments"].asString();
        }
      }
    });
    const auto http = http_.request_stream(
        "POST", url, headers, json_string(body),
        [&](std::string_view chunk) { return decoder.feed(chunk); }, provider.timeout_seconds,
        16ULL * 1024 * 1024, cancelled);
    decoder.finish();
    if (http.status < 200 || http.status >= 300) (void)parse_json(http);
    if (!decoder.saw_data()) {
      response = parse_openai(parse_json(http));
      if (!response.content.empty()) on_text(response.content);
    }
    for (auto& call : response.tool_calls) {
      if (call.arguments.empty()) call.arguments = "{}";
    }
    return response;
  }

  if (provider.protocol == "anthropic") {
    url = endpoint(provider.base_url, "/v1/messages");
    if (!key.empty()) headers.push_back("x-api-key: " + key);
    headers.push_back("anthropic-version: 2023-06-01");
    body["model"] = model;
    body["system"] = system_prompt;
    body["messages"] = anthropic_messages(messages);
    body["max_tokens"] = max_output_tokens;
    body["stream"] = true;
    if (capabilities.tools && tools.isArray() && !tools.empty()) body["tools"] = anthropic_tools(tools);
    apply_generation_settings(body, provider, model, settings, max_output_tokens, capabilities);
    std::map<unsigned, std::size_t> tool_blocks;
    SseDecoder decoder([&](const std::string& data) {
      const auto root = parse_json_text(data);
      response.raw = root;
      const auto type = root.get("type", "").asString();
      if (type == "message_start") {
        response.usage.prompt_tokens = root["message"]["usage"].get("input_tokens", 0).asInt();
      } else if (type == "content_block_start") {
        const auto& block = root["content_block"];
        if (block.get("type", "").asString() == "tool_use") {
          ToolCall call{block.get("id", "").asString(), block.get("name", "").asString(), ""};
          if (block["input"].isObject() && !block["input"].empty()) call.arguments = json_string(block["input"]);
          tool_blocks[root.get("index", 0).asUInt()] = response.tool_calls.size();
          response.tool_calls.push_back(std::move(call));
        }
      } else if (type == "content_block_delta") {
        const auto& delta = root["delta"];
        if (delta.get("type", "").asString() == "text_delta") {
          const auto text = delta.get("text", "").asString();
          response.content += text;
          on_text(text);
        } else if (delta.get("type", "").asString() == "input_json_delta") {
          const auto found = tool_blocks.find(root.get("index", 0).asUInt());
          if (found != tool_blocks.end()) {
            response.tool_calls[found->second].arguments += delta.get("partial_json", "").asString();
          }
        }
      } else if (type == "message_delta") {
        if (root["delta"]["stop_reason"].isString()) {
          response.finish_reason = root["delta"]["stop_reason"].asString();
        }
        response.usage.completion_tokens =
            root["usage"].get("output_tokens", response.usage.completion_tokens).asInt();
        response.usage.total_tokens = response.usage.prompt_tokens + response.usage.completion_tokens;
      }
    });
    const auto http = http_.request_stream(
        "POST", url, headers, json_string(body),
        [&](std::string_view chunk) { return decoder.feed(chunk); }, provider.timeout_seconds,
        16ULL * 1024 * 1024, cancelled);
    decoder.finish();
    if (http.status < 200 || http.status >= 300) (void)parse_json(http);
    if (!decoder.saw_data()) {
      response = parse_anthropic(parse_json(http));
      if (!response.content.empty()) on_text(response.content);
    }
    for (auto& call : response.tool_calls) {
      if (call.arguments.empty()) call.arguments = "{}";
    }
    return response;
  }

  if (provider.protocol == "gemini") {
    url = endpoint(provider.base_url, "/models/") + HttpClient::url_encode(model) +
          ":streamGenerateContent?alt=sse";
    if (!key.empty()) headers.push_back("x-goog-api-key: " + key);
    body["contents"] = gemini_contents(messages);
    if (!system_prompt.empty()) body["systemInstruction"]["parts"][0]["text"] = system_prompt;
    body["generationConfig"]["maxOutputTokens"] = max_output_tokens;
    if (capabilities.tools && tools.isArray() && !tools.empty()) body["tools"] = gemini_tools(tools);
    apply_generation_settings(body, provider, model, settings, max_output_tokens, capabilities);
    SseDecoder decoder([&](const std::string& data) {
      const auto root = parse_json_text(data);
      response.raw = root;
      if (root["usageMetadata"].isObject()) {
        response.usage.prompt_tokens =
            root["usageMetadata"].get("promptTokenCount", response.usage.prompt_tokens).asInt();
        response.usage.completion_tokens =
            root["usageMetadata"].get("candidatesTokenCount", response.usage.completion_tokens).asInt();
        response.usage.total_tokens =
            root["usageMetadata"].get("totalTokenCount", response.usage.total_tokens).asInt();
      }
      for (const auto& candidate : root["candidates"]) {
        if (candidate["finishReason"].isString()) response.finish_reason = candidate["finishReason"].asString();
        for (const auto& part : candidate["content"]["parts"]) {
          if (part["text"].isString()) {
            const auto text = part["text"].asString();
            response.content += text;
            on_text(text);
          }
          if (part["functionCall"].isObject()) {
            const auto& call = part["functionCall"];
            const auto name = call.get("name", "").asString();
            response.tool_calls.push_back({name, name, json_string(call["args"])});
          }
        }
      }
    });
    const auto http = http_.request_stream(
        "POST", url, headers, json_string(body),
        [&](std::string_view chunk) { return decoder.feed(chunk); }, provider.timeout_seconds,
        16ULL * 1024 * 1024, cancelled);
    decoder.finish();
    if (http.status < 200 || http.status >= 300) (void)parse_json(http);
    if (!decoder.saw_data()) {
      response = parse_gemini(parse_json(http));
      if (!response.content.empty()) on_text(response.content);
    }
    return response;
  }
  throw std::runtime_error("unsupported provider protocol: " + provider.protocol);
}

std::vector<std::string> ChatClient::fetch_models(const Provider& provider) const {
  auto headers = common_headers(provider);
  auto key = ConfigStore::api_key_for(provider);
  std::string url;
  if (provider.protocol == "anthropic") {
    url = endpoint(provider.base_url, "/v1/models");
    if (!key.empty()) headers.push_back("x-api-key: " + key);
    headers.push_back("anthropic-version: 2023-06-01");
  } else if (provider.protocol == "gemini") {
    url = endpoint(provider.base_url, "/models");
    if (!key.empty()) headers.push_back("x-goog-api-key: " + key);
  } else {
    url = endpoint(provider.base_url, "/models");
    if (!key.empty()) headers.push_back("Authorization: Bearer " + key);
  }
  auto root = parse_json(http_.request("GET", url, headers, {}, provider.timeout_seconds));
  std::vector<std::string> models;
  const auto& data = provider.protocol == "gemini" ? root["models"] : root["data"];
  for (const auto& item : data) {
    auto id = item.get(provider.protocol == "gemini" ? "name" : "id", "").asString();
    if (id.rfind("models/", 0) == 0) id.erase(0, 7);
    if (!id.empty()) models.push_back(id);
  }
  std::sort(models.begin(), models.end());
  return models;
}

std::size_t estimate_tokens(const std::vector<Message>& messages,
                            const std::string& system_prompt,
                            const Json::Value& tools) {
  std::size_t characters = system_prompt.size();
  std::size_t overhead = 8;
  for (const auto& message : messages) {
    characters += message.role.size() + message.content.size() + message.tool_call_id.size();
    overhead += 6;
    for (const auto& call : message.tool_calls) {
      characters += call.id.size() + call.name.size() + call.arguments.size();
      overhead += 8;
    }
  }
  if (!tools.isNull()) characters += json_string(tools).size();
  return (characters + 2) / 3 + overhead;
}

}  // namespace ask
