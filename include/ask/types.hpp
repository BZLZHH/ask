#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace ask {

struct ModelCapabilities {
  bool tools{true};
  bool streaming{true};
  bool thinking{true};
  bool temperature{true};
  bool top_p{true};
  bool json{true};
  int context_window{0};
};

struct Provider {
  std::string id;
  std::string name;
  std::string protocol{"openai"};
  std::string base_url;
  std::string api_key;
  std::string api_key_env;
  std::vector<std::string> models;
  std::string default_model;
  std::map<std::string, ModelCapabilities> model_capabilities;
  std::map<std::string, std::string> headers;
  int context_window{128000};
  int timeout_seconds{120};
  bool enabled{true};
};

struct Settings {
  double auto_compact_ratio{0.70};
  int max_tool_rounds{12};
  int max_output_tokens{4096};
  std::optional<double> temperature;
  std::optional<double> top_p;
  std::string reasoning_effort{"default"};
  int thinking_budget_tokens{0};
  bool stream_output{true};
  Json::Value custom_parameters{Json::objectValue};
  bool save_sessions{true};
  std::string system_prompt;
  std::string conversation_entry_mode{"always_continue"};
  std::string judge_provider;
  std::string judge_model;
};

struct Config {
  int version{1};
  std::string default_provider{"openai"};
  std::string default_model{"gpt-4o-mini"};
  std::vector<Provider> providers;
  Settings settings;

  Provider* find_provider(const std::string& id);
  const Provider* find_provider(const std::string& id) const;
};

struct ToolCall {
  std::string id;
  std::string name;
  std::string arguments;
};

struct Message {
  std::string role;
  std::string content;
  std::string tool_call_id;
  std::vector<ToolCall> tool_calls;
};

struct Session {
  std::string id;
  std::string title;
  std::string provider;
  std::string model;
  bool do_mode{false};
  std::string cwd;
  std::int64_t created_at{0};
  std::int64_t updated_at{0};
  std::string summary;
  std::size_t active_from{0};
  std::vector<Message> messages;
};

struct Usage {
  int prompt_tokens{0};
  int completion_tokens{0};
  int total_tokens{0};
};

struct ChatResponse {
  std::string content;
  std::vector<ToolCall> tool_calls;
  std::string finish_reason;
  Usage usage;
  Json::Value raw;
};

}  // namespace ask
