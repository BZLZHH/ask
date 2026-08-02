#pragma once

#include <filesystem>
#include <functional>
#include <set>
#include <string>

#include <json/json.h>

namespace ask {

struct CommandResult {
  int exit_code{-1};
  std::string output;
  bool timed_out{false};
};

class ToolExecutor {
 public:
  enum class Access { read_only, full };
  using Approval =
      std::function<bool(const std::string&, const std::filesystem::path&, const std::string&)>;

  explicit ToolExecutor(std::filesystem::path root, Approval approval = {});

  // A non-empty allowed_full_tools set is an enforced allowlist for tools that
  // require full access. Read-only tools remain available.
  Json::Value schemas(Access access = Access::full, bool allow_escalation = true,
                      const std::set<std::string>& allowed_full_tools = {}) const;
  std::string execute(const std::string& name, const std::string& arguments,
                      Access access = Access::full,
                      const std::set<std::string>& allowed_full_tools = {},
                      bool allow_elevation = true);
  const std::filesystem::path& root() const { return root_; }

  static CommandResult run_process(const std::string& command,
                                   const std::filesystem::path& cwd,
                                   int timeout_seconds,
                                   bool sandboxed,
                                   std::size_t max_output = 1024ULL * 1024,
                                   bool clean_environment = false,
                                   bool readonly_workspace = false);
  static bool terminal_approval(const std::string& command,
                                const std::filesystem::path& cwd,
                                const std::string& reason);

 private:
  std::filesystem::path checked_path(const std::string& input, bool for_create) const;
  std::string read_file(const Json::Value& args);
  std::string write_file(const Json::Value& args);
  std::string list_files(const Json::Value& args);
  std::string search_text(const Json::Value& args);
  std::string git_status(const Json::Value& args);
  std::string git_diff(const Json::Value& args);
  std::string git_log(const Json::Value& args);
  std::string git_show(const Json::Value& args);
  std::string run_readonly_command(const Json::Value& args);
  std::string run_command(const Json::Value& args);
  std::string fetch_http(const Json::Value& args);
  std::string browse_page(const Json::Value& args);
  std::string web_search(const Json::Value& args);

  std::filesystem::path root_;
  Approval approval_;
};

}  // namespace ask
