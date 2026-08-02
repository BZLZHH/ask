#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>

#include "ask/client.hpp"
#include "ask/config.hpp"
#include "ask/tools.hpp"
#include "ask/types.hpp"

namespace ask {

enum class ConferenceStatus { draft, preparing, awaiting_setup, running, paused, awaiting_user, concluding, completed, stopped };

enum class ConferenceDepth { quick, standard, deep, audit };

struct ConferenceParticipant {
  std::string id;
  int seat_number{0};
  std::string name;
  std::string role;
  std::string responsibility;
  std::string provider;
  std::string model;
  std::string kind{"advisor"};
  bool enabled{true};
};

struct ConferenceSetup {
  int version{1};
  ConferenceDepth depth{ConferenceDepth::standard};
  int suggested_advisor_count{3};
  int agenda_turn_budget{8};
  bool user_approved{false};
  std::string decision_rule{"user_confirms"};
  std::string rationale;
};

struct ConferenceAgendaItem {
  std::string id;
  std::string title;
  std::string status{"pending"};
  std::string conclusion;
  std::string owner;
};

struct ConferenceUserQuestion {
  std::string id;
  std::string requester;
  std::string question;
  // subjective, objective, or mixed
  std::string type{"subjective"};
  std::vector<std::string> options;
  std::int64_t created_at{0};
  std::int64_t expires_at{0};
  std::string status{"pending"};
  std::string answer;
};

struct ConferenceEvent {
  std::string id;
  std::int64_t timestamp{0};
  int round{0};
  std::string type;
  std::string author;
  std::string role;
  std::string content;
  std::string detail;
  // discussion events are created as streaming and become completed only
  // after the provider response has ended.
  std::string state{"completed"};
};

struct Conference {
  std::string id;
  std::string title;
  std::string goal;
  std::string provider;
  std::string model;
  std::string cwd;
  ConferenceStatus status{ConferenceStatus::draft};
  std::int64_t created_at{0};
  std::int64_t updated_at{0};
  int round{0};
  // Counts turns in the active agenda item. It is a soft depth signal for
  // the moderator, whereas round remains the lifetime meeting counter.
  int agenda_round{0};
  std::size_t next_participant{0};
  std::string next_speaker_id{"moderator"};
  std::string next_speaker_reason;
  bool return_to_moderator{false};
  std::string current_agenda_id;
  std::string rules;
  ConferenceSetup setup;
  // Autopilot is opt-in. Selected tools are the only full-access tools that
  // automatic rounds may use; an empty list means read-only operation.
  bool autopilot_enabled{false};
  // Zero means continuously advance until a user/host stop condition.
  int autopilot_round_limit{12};
  int autopilot_rounds_run{0};
  bool autopilot_stop_for_decisions{true};
  std::vector<std::string> autopilot_preauthorized_tools;
  std::vector<ConferenceParticipant> participants;
  std::vector<ConferenceAgendaItem> agenda;
  std::vector<std::string> facts;
  std::vector<std::string> open_questions;
  std::vector<std::string> decisions;
  std::vector<std::string> action_items;
  std::vector<ConferenceUserQuestion> user_questions;
  // A model-produced, auditable memory of older events. Events themselves
  // are never removed; this only controls what is sent to later requests.
  std::string context_summary;
  std::size_t compacted_until{0};
  std::int64_t total_prompt_tokens{0};
  std::int64_t total_cached_tokens{0};
  std::int64_t total_cache_creation_tokens{0};
  std::int64_t request_count{0};
  std::vector<ConferenceEvent> events;
};

std::string conference_status_name(ConferenceStatus status);
std::optional<ConferenceStatus> conference_status_from_name(const std::string& name);
std::string conference_depth_name(ConferenceDepth depth);
std::optional<ConferenceDepth> conference_depth_from_name(const std::string& name);
Json::Value conference_to_json(const Conference& conference);
std::optional<Conference> conference_from_json(const Json::Value& value);

class ConferenceStore {
 public:
  explicit ConferenceStore(std::filesystem::path directory = {});

  static std::filesystem::path default_directory();
  static std::string new_id();

  void save(const Conference& conference) const;
  std::optional<Conference> load(const std::string& id) const;
  std::vector<Conference> list(std::size_t limit = 100) const;
  bool remove(const std::string& id) const;

 private:
  std::filesystem::path directory_;
};

class ConferenceEngine {
 public:
  using StreamObserver = std::function<void()>;
  ConferenceEngine(Config config, Conference conference, ConferenceStore& store);
  ~ConferenceEngine();
  ConferenceEngine(const ConferenceEngine&) = delete;
  ConferenceEngine& operator=(const ConferenceEngine&) = delete;

  static Conference create(const Config& config, const std::string& goal,
                           const std::filesystem::path& cwd,
                           const std::string& provider = {}, const std::string& model = {});

  const Conference& conference() const { return conference_; }
  Conference& conference() { return conference_; }
  Conference snapshot() const;
  const Config& config() const { return config_; }
  bool is_generating() const { return generating_.load(); }
  void persist();
  // The TUI installs an observer while it is open so text deltas can redraw
  // the currently speaking event without waiting for the full response.
  void set_stream_observer(StreamObserver observer) { stream_observer_ = std::move(observer); }
  void start();
  // Prepares a user-reviewable meeting plan. The initial implementation is
  // deterministic so it is available even when no provider call is possible.
  void prepare_setup();
  void approve_setup();
  void update_setup(ConferenceDepth depth, int advisor_count, int agenda_turn_budget,
                    const std::vector<ConferenceParticipant>& participants);
  void assign_next_speaker(const std::string& participant_id, const std::string& reason,
                           bool return_to_moderator = false, bool user_override = false);
  void pause();
  void resume();
  void stop();
  void interrupt(const std::string& content);
  // Called by the TUI and before autonomous rounds so a missed answer becomes
  // a visible moderator input instead of an indefinitely blocked meeting.
  void check_user_question_timeouts();
  void update_goal(const std::string& goal);
  void focus_agenda(std::size_t index);
  void update_rules(const std::string& rules);
  void resolve_decision(std::size_t index, const std::string& outcome);
  // Normal rounds are read-only. Full access is available only after an explicit
  // user approval in the conference UI, and applies to this one speaker turn.
  void advance(bool allow_write = false);
  // Persists explicit user authorization for autonomous progression. A zero
  // round limit means continue until a visible stop condition occurs.
  // Unknown or read-only names are discarded; only full-access tools are kept.
  void set_autopilot(bool enabled, int round_limit,
                     const std::vector<std::string>& preauthorized_tools,
                     bool stop_for_decisions = true);
  void run_autopilot();
  void conclude();
  std::string summary() const;
  std::filesystem::path export_summary(const std::string& path = {}) const;

 private:
  const Provider& provider(const ConferenceParticipant& participant) const;
  ConferenceParticipant& next_enabled_participant();
  ConferenceParticipant* find_participant(const std::string& id);
  std::vector<Message> prompt_messages(const ConferenceParticipant& participant) const;
  std::string system_prompt(const ConferenceParticipant& participant, bool allow_write,
                            bool autopilot = false) const;
  Json::Value conference_tool_schemas(ToolExecutor::Access access,
                                      const std::set<std::string>& allowed_full_tools) const;
  std::string execute_subagent(const ConferenceParticipant& participant, const std::string& arguments,
                               ToolExecutor::Access access,
                               const std::set<std::string>& allowed_full_tools);
  std::size_t record(const std::string& type, const std::string& author, const std::string& role,
                     const std::string& content, const std::string& detail = {},
                     const std::string& state = "completed");
  void absorb_structured_output(const ConferenceParticipant& participant, const std::string& content);
  void advance_with_policy(bool allow_write, const std::set<std::string>& allowed_full_tools,
                           bool autopilot);
  void launch_task(std::function<void()> task);
  void run_autopilot_task();
  void generate_setup_with_moderator();
  void maybe_compact_history();
  void mark_interrupted_event(std::size_t event_index, const std::string& reason);
  bool apply_moderator_directives(const std::string& content);

  Config config_;
  Conference conference_;
  ConferenceStore& store_;
  ChatClient client_;
  ToolExecutor tools_;
  StreamObserver stream_observer_;
  mutable std::recursive_mutex mutex_;
  std::thread worker_;
  std::atomic_bool generating_{false};
  // Increments when a user change should apply to the next model request.
  // A response begun under an older revision is retained as history but is
  // never allowed to mutate structured meeting state.
  std::atomic_uint64_t context_revision_{0};
  volatile std::sig_atomic_t cancel_requested_{0};
  std::optional<std::size_t> active_stream_event_;
};

}  // namespace ask
