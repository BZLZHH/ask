#include "ask/session.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include <sys/stat.h>

#include "ask/config.hpp"

namespace ask {
namespace {

class Statement {
 public:
  Statement(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(db));
    }
  }
  ~Statement() { sqlite3_finalize(statement_); }
  sqlite3_stmt* get() { return statement_; }

 private:
  sqlite3_stmt* statement_{nullptr};
};

void check(sqlite3* db, int status) {
  if (status != SQLITE_OK && status != SQLITE_DONE && status != SQLITE_ROW) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
}

void bind_text(sqlite3* db, sqlite3_stmt* stmt, int index, const std::string& value) {
  check(db, sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

std::string column_text(sqlite3_stmt* stmt, int column) {
  const auto* value = sqlite3_column_text(stmt, column);
  return value ? reinterpret_cast<const char*>(value) : "";
}

std::int64_t now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string calls_to_json(const std::vector<ToolCall>& calls) {
  Json::Value root(Json::arrayValue);
  for (const auto& call : calls) {
    Json::Value item(Json::objectValue);
    item["id"] = call.id;
    item["name"] = call.name;
    item["arguments"] = call.arguments;
    root.append(item);
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, root);
}

std::vector<ToolCall> calls_from_json(const std::string& text) {
  std::vector<ToolCall> calls;
  if (text.empty()) return calls;
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream input(text);
  if (!Json::parseFromStream(builder, input, &root, &errors)) return calls;
  for (const auto& item : root) {
    calls.push_back({item.get("id", "").asString(), item.get("name", "").asString(),
                     item.get("arguments", "{}").asString()});
  }
  return calls;
}

}  // namespace

SessionStore::SessionStore(std::filesystem::path path) {
  if (path.empty()) path = default_path();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) throw std::runtime_error("cannot create data directory: " + ec.message());
  std::filesystem::permissions(path.parent_path(), std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    std::string message = db_ ? sqlite3_errmsg(db_) : "cannot open session database";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(message);
  }
  ::chmod(path.c_str(), 0600);
  sqlite3_busy_timeout(db_, 3000);
  initialize();
}

SessionStore::~SessionStore() {
  if (db_) sqlite3_close(db_);
}

std::filesystem::path SessionStore::default_path() {
  return ConfigStore::data_dir() / "sessions.db";
}

void SessionStore::initialize() {
  const char* sql = R"SQL(
    PRAGMA journal_mode=WAL;
    PRAGMA foreign_keys=ON;
    CREATE TABLE IF NOT EXISTS sessions (
      id TEXT PRIMARY KEY,
      title TEXT NOT NULL,
      provider TEXT NOT NULL,
      model TEXT NOT NULL,
      do_mode INTEGER NOT NULL,
      cwd TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      summary TEXT NOT NULL DEFAULT '',
      active_from INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS messages (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
      ordinal INTEGER NOT NULL,
      role TEXT NOT NULL,
      content TEXT NOT NULL,
      tool_call_id TEXT NOT NULL DEFAULT '',
      tool_calls_json TEXT NOT NULL DEFAULT '[]',
      UNIQUE(session_id, ordinal)
    );
    CREATE INDEX IF NOT EXISTS idx_sessions_updated ON sessions(updated_at DESC);
  )SQL";
  char* error = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "failed to initialize session database";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
  // Older development databases predate active_from.
  sqlite3_exec(db_, "ALTER TABLE sessions ADD COLUMN active_from INTEGER NOT NULL DEFAULT 0",
               nullptr, nullptr, nullptr);
}

std::string SessionStore::new_id() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&seconds, &local);
  std::random_device device;
  std::mt19937 generator(device());
  std::uniform_int_distribution<unsigned> distribution(0, 0xffffff);
  std::ostringstream output;
  output << std::put_time(&local, "%Y%m%d-%H%M%S") << '-' << std::hex << std::setw(6)
         << std::setfill('0') << distribution(generator);
  return output.str();
}

void SessionStore::save(const Session& original) {
  Session session = original;
  if (session.id.empty()) throw std::runtime_error("cannot save a session without an id");
  if (!session.created_at) session.created_at = now_seconds();
  session.updated_at = now_seconds();
  check(db_, sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr));
  try {
    Statement upsert(db_, R"SQL(
      INSERT INTO sessions(id,title,provider,model,do_mode,cwd,created_at,updated_at,summary,active_from)
      VALUES(?,?,?,?,?,?,?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET title=excluded.title, provider=excluded.provider,
        model=excluded.model, do_mode=excluded.do_mode, cwd=excluded.cwd,
        updated_at=excluded.updated_at, summary=excluded.summary, active_from=excluded.active_from
    )SQL");
    auto* stmt = upsert.get();
    bind_text(db_, stmt, 1, session.id);
    bind_text(db_, stmt, 2, session.title);
    bind_text(db_, stmt, 3, session.provider);
    bind_text(db_, stmt, 4, session.model);
    check(db_, sqlite3_bind_int(stmt, 5, session.do_mode ? 1 : 0));
    bind_text(db_, stmt, 6, session.cwd);
    check(db_, sqlite3_bind_int64(stmt, 7, session.created_at));
    check(db_, sqlite3_bind_int64(stmt, 8, session.updated_at));
    bind_text(db_, stmt, 9, session.summary);
    check(db_, sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(session.active_from)));
    check(db_, sqlite3_step(stmt));

    Statement clear(db_, "DELETE FROM messages WHERE session_id=?");
    bind_text(db_, clear.get(), 1, session.id);
    check(db_, sqlite3_step(clear.get()));

    Statement insert(db_, R"SQL(
      INSERT INTO messages(session_id,ordinal,role,content,tool_call_id,tool_calls_json)
      VALUES(?,?,?,?,?,?)
    )SQL");
    int ordinal = 0;
    for (const auto& message : session.messages) {
      sqlite3_reset(insert.get());
      sqlite3_clear_bindings(insert.get());
      bind_text(db_, insert.get(), 1, session.id);
      check(db_, sqlite3_bind_int(insert.get(), 2, ordinal++));
      bind_text(db_, insert.get(), 3, message.role);
      bind_text(db_, insert.get(), 4, message.content);
      bind_text(db_, insert.get(), 5, message.tool_call_id);
      bind_text(db_, insert.get(), 6, calls_to_json(message.tool_calls));
      check(db_, sqlite3_step(insert.get()));
    }
    check(db_, sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr));
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

std::optional<Session> SessionStore::load(const std::string& id) const {
  Statement query(db_, R"SQL(
    SELECT id,title,provider,model,do_mode,cwd,created_at,updated_at,summary,active_from
    FROM sessions WHERE id=?
  )SQL");
  bind_text(db_, query.get(), 1, id);
  if (sqlite3_step(query.get()) != SQLITE_ROW) return std::nullopt;
  Session session;
  session.id = column_text(query.get(), 0);
  session.title = column_text(query.get(), 1);
  session.provider = column_text(query.get(), 2);
  session.model = column_text(query.get(), 3);
  session.do_mode = sqlite3_column_int(query.get(), 4) != 0;
  session.cwd = column_text(query.get(), 5);
  session.created_at = sqlite3_column_int64(query.get(), 6);
  session.updated_at = sqlite3_column_int64(query.get(), 7);
  session.summary = column_text(query.get(), 8);
  session.active_from = static_cast<std::size_t>(sqlite3_column_int64(query.get(), 9));

  Statement messages(db_, R"SQL(
    SELECT role,content,tool_call_id,tool_calls_json FROM messages
    WHERE session_id=? ORDER BY ordinal
  )SQL");
  bind_text(db_, messages.get(), 1, id);
  while (sqlite3_step(messages.get()) == SQLITE_ROW) {
    Message message;
    message.role = column_text(messages.get(), 0);
    message.content = column_text(messages.get(), 1);
    message.tool_call_id = column_text(messages.get(), 2);
    message.tool_calls = calls_from_json(column_text(messages.get(), 3));
    session.messages.push_back(std::move(message));
  }
  return session;
}

std::vector<Session> SessionStore::list(std::size_t limit) const {
  Statement query(db_, R"SQL(
    SELECT id,title,provider,model,do_mode,cwd,created_at,updated_at,summary,active_from
    FROM sessions ORDER BY updated_at DESC LIMIT ?
  )SQL");
  check(db_, sqlite3_bind_int64(query.get(), 1, static_cast<sqlite3_int64>(limit)));
  std::vector<Session> sessions;
  while (sqlite3_step(query.get()) == SQLITE_ROW) {
    Session session;
    session.id = column_text(query.get(), 0);
    session.title = column_text(query.get(), 1);
    session.provider = column_text(query.get(), 2);
    session.model = column_text(query.get(), 3);
    session.do_mode = sqlite3_column_int(query.get(), 4) != 0;
    session.cwd = column_text(query.get(), 5);
    session.created_at = sqlite3_column_int64(query.get(), 6);
    session.updated_at = sqlite3_column_int64(query.get(), 7);
    session.summary = column_text(query.get(), 8);
    session.active_from = static_cast<std::size_t>(sqlite3_column_int64(query.get(), 9));
    sessions.push_back(std::move(session));
  }
  return sessions;
}

bool SessionStore::remove(const std::string& id) {
  Statement statement(db_, "DELETE FROM sessions WHERE id=?");
  bind_text(db_, statement.get(), 1, id);
  check(db_, sqlite3_step(statement.get()));
  return sqlite3_changes(db_) > 0;
}

}  // namespace ask
