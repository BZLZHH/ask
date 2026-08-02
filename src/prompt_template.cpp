#include "ask/prompt_template.hpp"

#include <map>
#include <sstream>
#include <vector>

namespace ask {
namespace {

std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool evaluate_condition(const std::string& raw,
                        const TemplateContext& context) {
  const auto condition = trimmed(raw);
  if (condition == "do_mode")
    return context.do_mode;
  if (condition == "read_only")
    return context.read_only;
  if (condition == "has_tools")
    return context.has_tools;
  if (condition == "streaming")
    return context.streaming;
  const auto separator = condition.find(':');
  if (separator == std::string::npos)
    return false;
  const auto key = trimmed(condition.substr(0, separator));
  const auto value = trimmed(condition.substr(separator + 1));
  if (key == "provider")
    return context.provider == value;
  if (key == "protocol")
    return context.protocol == value;
  if (key == "model")
    return context.model == value;
  return false;
}

std::string lookup_variable(const std::string& raw,
                            const TemplateContext& context) {
  const auto key = trimmed(raw);
  static const std::map<std::string, const std::string TemplateContext::*>
      variables = {
          {"cwd", &TemplateContext::cwd},
          {"date", &TemplateContext::date},
          {"time", &TemplateContext::time},
          {"datetime", &TemplateContext::datetime},
          {"hostname", &TemplateContext::hostname},
          {"user", &TemplateContext::user},
          {"shell", &TemplateContext::shell},
          {"os", &TemplateContext::os},
          {"arch", &TemplateContext::arch},
          {"model", &TemplateContext::model},
          {"provider", &TemplateContext::provider},
          {"provider_name", &TemplateContext::provider_name},
          {"protocol", &TemplateContext::protocol},
      };
  const auto iterator = variables.find(key);
  if (iterator == variables.end())
    return "{{" + key + "}}";
  return context.*(iterator->second);
}

struct BranchFrame {
  bool parent_visible;
  bool branch_taken;
};

bool begins_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

} // namespace

std::string expand_template(const std::string& raw,
                            const TemplateContext& context) {
  if (raw.find("{{") == std::string::npos)
    return raw;

  std::ostringstream output;
  std::vector<BranchFrame> branches;
  bool visible = true;
  for (std::size_t position = 0; position < raw.size();) {
    if (position + 3 <= raw.size() && raw[position] == '\\' &&
        raw[position + 1] == '{' && raw[position + 2] == '{') {
      if (visible)
        output << "{{";
      position += 3;
      continue;
    }
    if (raw.compare(position, 2, "{{") != 0) {
      if (visible)
        output << raw[position];
      ++position;
      continue;
    }

    const auto end = raw.find("}}", position + 2);
    if (end == std::string::npos) {
      if (visible)
        output << raw.substr(position);
      break;
    }

    const auto token = trimmed(raw.substr(position + 2, end - position - 2));
    if (begins_with(token, "#if ")) {
      branches.push_back({visible, false});
      visible = visible && evaluate_condition(token.substr(4), context);
      if (visible)
        branches.back().branch_taken = true;
    } else if (begins_with(token, "#unless ")) {
      branches.push_back({visible, false});
      visible = visible && !evaluate_condition(token.substr(8), context);
      if (visible)
        branches.back().branch_taken = true;
    } else if (token == "else") {
      if (!branches.empty()) {
        auto& frame = branches.back();
        visible = frame.parent_visible && !frame.branch_taken;
        if (visible)
          frame.branch_taken = true;
      }
    } else if (token == "/if" || token == "/unless") {
      if (!branches.empty()) {
        visible = branches.back().parent_visible;
        branches.pop_back();
      }
    } else if (begins_with(token, "#")) {
      if (visible)
        output << "{{" << token << "}}";
    } else {
      if (visible)
        output << lookup_variable(token, context);
    }
    position = end + 2;
  }
  return output.str();
}

} // namespace ask
