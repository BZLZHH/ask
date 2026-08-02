#include "ask/prompt_template.hpp"

#include <iostream>
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

bool evaluate_condition(const std::string& raw, const TemplateContext& context,
                        bool& known) {
  known = true;
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
  known = false;
  return false;
}

std::string lookup_variable(const std::string& raw, const TemplateContext& context,
                            bool& known) {
  known = true;
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
  if (iterator == variables.end()) {
    known = false;
    return "{{" + key + "}}";
  }
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
      bool known = false;
      const auto condition = evaluate_condition(token.substr(4), context, known);
      if (!known) std::cerr << "ask: prompt template condition is unknown: "
                            << token.substr(4) << '\n';
      visible = visible && condition;
      if (visible)
        branches.back().branch_taken = true;
    } else if (begins_with(token, "#unless ")) {
      branches.push_back({visible, false});
      bool known = false;
      const auto condition = evaluate_condition(token.substr(8), context, known);
      if (!known) std::cerr << "ask: prompt template condition is unknown: "
                            << token.substr(8) << '\n';
      visible = visible && !condition;
      if (visible)
        branches.back().branch_taken = true;
    } else if (token == "else") {
      if (!branches.empty()) {
        auto& frame = branches.back();
        visible = frame.parent_visible && !frame.branch_taken;
        if (visible)
          frame.branch_taken = true;
      } else {
        std::cerr << "ask: prompt template has {{else}} without an open {{#if}} or {{#unless}}\n";
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
      bool known = false;
      const auto value = lookup_variable(token, context, known);
      if (!known) std::cerr << "ask: prompt template variable is unknown: " << token << '\n';
      if (visible) output << value;
    }
    position = end + 2;
  }
  if (!branches.empty()) {
    std::cerr << "ask: prompt template has an unclosed {{#if}} or {{#unless}} block\n";
  }
  return output.str();
}

} // namespace ask
