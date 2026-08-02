#pragma once

#include <string>

namespace ask {

struct TemplateContext {
  std::string cwd;
  std::string date;
  std::string time;
  std::string datetime;
  std::string hostname;
  std::string user;
  std::string shell;
  std::string os;
  std::string arch;
  std::string model;
  std::string provider;
  std::string provider_name;
  std::string protocol;
  bool do_mode{false};
  bool read_only{true};
  bool has_tools{true};
  bool streaming{true};
};

std::string expand_template(const std::string& raw,
                            const TemplateContext& context);

}  // namespace ask
