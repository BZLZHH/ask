#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "ask/types.hpp"

namespace ask {

class SessionStore {
 public:
  explicit SessionStore(std::filesystem::path path = {});
  ~SessionStore();
  SessionStore(const SessionStore&) = delete;
  SessionStore& operator=(const SessionStore&) = delete;

  static std::filesystem::path default_path();
  static std::string new_id();

  void save(const Session& session);
  std::optional<Session> load(const std::string& id) const;
  std::vector<Session> list(std::size_t limit = 100) const;
  bool remove(const std::string& id);
  void mark_quick_resume(const Session& session);
  void clear_quick_resume();
  std::optional<Session> consume_quick_resume(const std::filesystem::path& cwd,
                                              int max_age_seconds = 10);
  std::filesystem::path quick_resume_path() const;

 private:
  void initialize();
  std::filesystem::path path_;
  sqlite3* db_{nullptr};
};

}  // namespace ask
