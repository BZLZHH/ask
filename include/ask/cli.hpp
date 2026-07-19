#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ask {

struct CliOptions {
  bool help{false};
  bool version{false};
  bool config{false};
  bool do_mode{false};
  bool interactive{false};
  bool no_repl{false};
  bool no_stream{false};
  bool json{false};
  bool quiet{false};
  bool resume{false};
  std::string resume_id;
  std::string provider;
  std::string model;
  std::string prompt;
};

CliOptions parse_cli(int argc, char** argv);
std::string usage();
int run_cli(const CliOptions& options);

}  // namespace ask
