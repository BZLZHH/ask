#include "ask/tui.hpp"

#include <algorithm>
#include <array>
#include <clocale>
#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <ncurses.h>

namespace ask {
namespace {

class ConferenceScreen {
 public:
 ConferenceScreen() {
 std::setlocale(LC_CTYPE, "");
 if (!initscr()) throw std::runtime_error("cannot initialize terminal UI");
 cbreak(); noecho(); keypad(stdscr, true); timeout(100); curs_set(0); use_default_colors();
 if (has_colors()) {
 start_color();
 init_pair(1, COLOR_CYAN, -1);
 init_pair(2, COLOR_BLACK, COLOR_CYAN);
 init_pair(3, COLOR_YELLOW, -1);
 init_pair(4, COLOR_RED, -1);
 }
 }
 ~ConferenceScreen() { endwin(); }
};

enum class Focus { agenda, discussion, controls, input };

bool enter_key(int key) { return key == '\n' || key == KEY_ENTER; }
bool escape_key(int key) { return key == 27; }

std::string clipped(const std::string& text, int width) {
 if (width <= 0) return {};
 if (static_cast<int>(text.size()) <= width) return text;
 if (width <= 3) return text.substr(0, static_cast<std::size_t>(width));
 return text.substr(0, static_cast<std::size_t>(width - 3)) + "...";
}

std::string status_text(ConferenceStatus status) {
 switch (status) {
 case ConferenceStatus::draft: return "Draft";
 case ConferenceStatus::preparing: return "Preparing plan";
 case ConferenceStatus::awaiting_setup: return "Awaiting plan approval";
 case ConferenceStatus::running: return "Running";
 case ConferenceStatus::paused: return "Paused";
 case ConferenceStatus::awaiting_user: return "Awaiting user";
 case ConferenceStatus::concluding: return "Concluding";
 case ConferenceStatus::completed: return "Completed";
 case ConferenceStatus::stopped: return "Stopped";
 }
 return "Draft";
}

std::string agenda_marker(const ConferenceAgendaItem& item) {
 if (item.status == "completed") return "[x]";
 if (item.status == "active") return "[*]";
 if (item.status == "blocked") return "[!]";
 if (item.status == "cancelled") return "[-]";
 return "[ ]";
}

std::string event_kind(const ConferenceEvent& event) {
 if (event.type == "discussion" && event.state == "streaming") return "Speaking";
 if (event.type == "discussion" && event.state == "limited") return "Output limit";
 if (event.type == "discussion" &&
 (event.state == "failed" || event.state == "interrupted")) return "Interrupted";
 if (event.type == "user") return "User";
 if (event.type == "discussion") return event.role.empty() ? "Discussion" : event.role;
 if (event.type == "tool_request") return "Tool request";
 if (event.type == "tool_authorization") return "Tool authorized";
 if (event.type == "tool_result") return "Tool result";
 if (event.type == "tool_error") return "Tool error";
 if (event.type == "error") return "Error";
 if (event.type == "rules") return "Rules";
 if (event.type == "agenda") return "Agenda";
 return "System";
}

void footer(const std::string& text) {
 attron(A_DIM);
 mvhline(LINES - 2, 0, ACS_HLINE, COLS);
 mvaddnstr(LINES - 1, 1, clipped(text, std::max(0, COLS - 2)).c_str(), std::max(0, COLS - 2));
 attroff(A_DIM);
}

void header(const Conference& conference) {
 erase();
 attron(COLOR_PAIR(1) | A_BOLD);
 const auto text = "AI Conference - " + conference.title + " - " + status_text(conference.status) +
 " - Round " + std::to_string(conference.round);
 mvaddnstr(0, 1, clipped(text, std::max(0, COLS - 2)).c_str(), std::max(0, COLS - 2));
 attroff(COLOR_PAIR(1) | A_BOLD);
 mvhline(1, 0, ACS_HLINE, COLS);
 const auto active = std::find_if(conference.agenda.begin(), conference.agenda.end(),
 [&](const auto& item) { return item.id == conference.current_agenda_id; });
 const auto goal = "Goal: " + conference.goal +
 (active == conference.agenda.end() ? "" : " | Topic: " + active->title);
 mvaddnstr(2, 1, clipped(goal, std::max(0, COLS - 2)).c_str(), std::max(0, COLS - 2));
 const auto scheduled = std::find_if(conference.participants.begin(), conference.participants.end(),
 [&](const auto& item) { return item.id == conference.next_speaker_id; });
 const auto schedule = "Next: " + (scheduled == conference.participants.end()
 ? conference.next_speaker_id : "#" + std::to_string(scheduled->seat_number) + " " + scheduled->name) +
 (conference.next_speaker_reason.empty() ? "" : " | " + conference.next_speaker_reason) +
 " | Depth: " + conference_depth_name(conference.setup.depth) +
 " | Topic turns: " + std::to_string(conference.agenda_round) +
 "/" + std::to_string(conference.setup.agenda_turn_budget) +
 " (checkpoint)";
 mvaddnstr(3, 1, clipped(schedule, std::max(0, COLS - 2)).c_str(), std::max(0, COLS - 2));
 mvhline(4, 0, ACS_HLINE, COLS);
}

void section(int row, int col, int width, const std::string& text, bool focused) {
 if (focused) attron(COLOR_PAIR(2) | A_BOLD); else attron(A_BOLD);
 mvaddnstr(row, col, focused ? "[ " : " ", std::max(0, width));
 mvaddnstr(row, col + 2, text.c_str(), std::max(0, width - 2));
 if (focused) attroff(COLOR_PAIR(2) | A_BOLD); else attroff(A_BOLD);
}

void text_lines(int& row, int col, int width, int bottom, const std::string& text) {
 std::istringstream stream(text);
 std::string line;
 while (row < bottom && std::getline(stream, line)) {
 mvaddnstr(row++, col, clipped(line, width).c_str(), width);
 }
}

std::string event_content(const ConferenceEvent& event) {
 if (event.type == "discussion" && event.state == "streaming" && event.content.empty()) {
 return " ...";
 }
 return event.content;
}

std::string tail_lines(const std::string& text, int maximum) {
 if (maximum <= 0) return {};
 std::vector<std::string> lines;
 std::istringstream input(text);
 std::string line;
 while (std::getline(input, line)) lines.push_back(line);
 if (static_cast<int>(lines.size()) <= maximum) return text;
 std::ostringstream output;
 output << "...\n";
 for (int index = static_cast<int>(lines.size()) - maximum;
 index < static_cast<int>(lines.size()); ++index) {
 output << lines[static_cast<std::size_t>(index)];
 if (index + 1 < static_cast<int>(lines.size())) output << '\n';
 }
 return output.str();
}

int displayed_line_count(const std::string& text) {
 if (text.empty()) return 1;
 return 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
}

// The review cursor is event-based, but live follow must be line-aware: a
// single streamed response can be much taller than the old fixed eight-event
// window. Start as far back as fits and let the final event use its tail.
int live_event_start(const std::vector<ConferenceEvent>& events, int available_rows) {
 available_rows = std::max(1, available_rows);
 int used = 0;
 int start = static_cast<int>(events.size());
 for (int index = static_cast<int>(events.size()) - 1; index >= 0; --index) {
 const int height = 1 + displayed_line_count(event_content(events[static_cast<std::size_t>(index)]));
 if (start != static_cast<int>(events.size()) && used + height > available_rows) break;
 used += height;
 start = index;
 }
 return std::max(0, start);
}

std::optional<std::string> edit_text(const std::string& title, const std::string& initial,
 const std::string& hint, bool readonly = false) {
 std::string value = initial;
 std::size_t cursor = value.size();
 for (;;) {
 erase();
 attron(COLOR_PAIR(1) | A_BOLD);
 mvaddnstr(0, 2, title.c_str(), std::max(0, COLS - 4));
 attroff(COLOR_PAIR(1) | A_BOLD);
 mvhline(1, 0, ACS_HLINE, COLS);
 mvaddnstr(3, 2, hint.c_str(), std::max(0, COLS - 4));
 const int width = std::max(1, COLS - 6);
 const std::size_t offset = cursor > static_cast<std::size_t>(width - 1) ? cursor - width + 1 : 0;
 attron(A_REVERSE);
 mvaddnstr(5, 2, std::string(static_cast<std::size_t>(width), ' ').c_str(), width);
 mvaddnstr(5, 2, value.substr(offset, static_cast<std::size_t>(width)).c_str(), width);
 attroff(A_REVERSE);
 footer(readonly ? "Esc or Enter close" : "Enter apply Esc cancel");
 curs_set(readonly ? 0 : 1);
 if (!readonly) move(5, 2 + static_cast<int>(cursor - offset));
 refresh();
 const int key = getch();
 if (escape_key(key)) return std::nullopt;
 if (enter_key(key)) return value;
 if (readonly) continue;
 if (key == KEY_LEFT && cursor > 0) --cursor;
 else if (key == KEY_RIGHT && cursor < value.size()) ++cursor;
 else if (key == KEY_HOME) cursor = 0;
 else if (key == KEY_END) cursor = value.size();
 else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && cursor > 0) {
 value.erase(cursor - 1, 1); --cursor;
 } else if (key == KEY_DC && cursor < value.size()) {
 value.erase(cursor, 1);
 } else if (key >= 32 && key <= 126 && value.size() < 16384) {
 value.insert(value.begin() + static_cast<std::ptrdiff_t>(cursor), static_cast<char>(key)); ++cursor;
 }
 }
}

std::optional<int> menu(const std::string& title, const std::string& detail,
 const std::vector<std::string>& choices, int selected = 0) {
 if (choices.empty()) return std::nullopt;
 selected = std::clamp(selected, 0, static_cast<int>(choices.size()) - 1);
 for (;;) {
 erase();
 attron(COLOR_PAIR(1) | A_BOLD);
 mvaddnstr(0, 2, title.c_str(), std::max(0, COLS - 4));
 attroff(COLOR_PAIR(1) | A_BOLD);
 mvhline(1, 0, ACS_HLINE, COLS);
 mvaddnstr(3, 2, clipped(detail, std::max(0, COLS - 4)).c_str(), std::max(0, COLS - 4));
 for (int index = 0; index < static_cast<int>(choices.size()) && 6 + index < LINES - 2; ++index) {
 if (index == selected) attron(COLOR_PAIR(2));
 const auto line = (index == selected ? "> " : " ") + choices[static_cast<std::size_t>(index)];
 mvaddnstr(6 + index, 2, clipped(line, std::max(0, COLS - 4)).c_str(), std::max(0, COLS - 4));
 if (index == selected) attroff(COLOR_PAIR(2));
 }
 footer("Up/Down select Enter confirm Esc cancel");
 refresh();
 const int key = getch();
 if (key == KEY_UP && selected > 0) --selected;
 else if (key == KEY_DOWN && selected + 1 < static_cast<int>(choices.size())) ++selected;
 else if (enter_key(key) || key == KEY_RIGHT) return selected;
 else if (escape_key(key) || key == KEY_LEFT) return std::nullopt;
 }
}

bool confirm(const std::string& title, const std::string& detail, const std::string& action) {
 const auto answer = menu(title, detail, {"Cancel", action});
 return answer && *answer == 1;
}

std::string trim_copy(std::string value) {
 const auto first = value.find_first_not_of(" \t\r\n");
 if (first == std::string::npos) return {};
 const auto last = value.find_last_not_of(" \t\r\n");
 return value.substr(first, last - first + 1);
}

std::pair<std::string, std::string> split_command(const std::string& value) {
 const auto cleaned = trim_copy(value);
 const auto separator = cleaned.find_first_of(" \t");
 if (separator == std::string::npos) return {cleaned, {}};
 return {cleaned.substr(0, separator), trim_copy(cleaned.substr(separator + 1))};
}

const ConferenceUserQuestion* pending_user_question(const Conference& conference) {
 const auto found = std::find_if(conference.user_questions.begin(), conference.user_questions.end(),
 [](const auto& question) { return question.status == "pending"; });
 return found == conference.user_questions.end() ? nullptr : &*found;
}

void answer_pending_question(ConferenceEngine& engine, const Conference& conference, std::string& notice) {
 const auto* question = pending_user_question(conference);
 if (!question) { notice = "No pending moderator question"; return; }
 std::optional<std::string> answer;
 if (question->type == "objective" && !question->options.empty()) {
 const auto selected = menu("Moderator question", question->question, question->options);
 if (selected) answer = question->options[static_cast<std::size_t>(*selected)];
 } else if (question->type == "mixed" && !question->options.empty()) {
 auto choices = question->options;
 choices.push_back("Type a different answer");
 const auto selected = menu("Moderator question", question->question, choices);
 if (selected) {
 if (*selected + 1 == static_cast<int>(choices.size())) {
 answer = edit_text("Your answer", {}, "Enter submits; Esc cancels.");
 } else {
 answer = question->options[static_cast<std::size_t>(*selected)];
 }
 }
 } else {
 answer = edit_text("Moderator question", question->question, "Enter submits; Esc cancels.");
 }
 if (!answer || trim_copy(*answer).empty()) return;
 engine.interrupt(*answer);
 notice = "Answer recorded; moderator will evaluate it next";
}

std::vector<std::string> controls_for(const Conference& conference) {
 if (conference.status == ConferenceStatus::preparing) {
 return {"Moderator is generating meeting plan", "View summary", "End conference"};
 }
 if (conference.status == ConferenceStatus::awaiting_setup || conference.status == ConferenceStatus::draft) {
 return {"Review meeting plan", "Regenerate moderator plan", "View summary",
 "View / edit rules", "End conference"};
 }
 if (conference.status == ConferenceStatus::awaiting_user && pending_user_question(conference)) {
 return {"Answer moderator question", "Continue without an answer", "Meeting parameters",
 "View summary", "End conference"};
 }
 return {conference.status == ConferenceStatus::running ? "Pause conference" : "Resume conference",
 "Advance assigned speaker", "Meeting parameters", "Choose next speaker",
 conference.autopilot_enabled ? "Run moderator autopilot" : "Autopilot disabled - configure",
 "Configure autopilot permissions", "Interrupt and ask", "View summary",
 "View / edit rules", "Run user-approved execution", "Focus agenda",
 "Conclude meeting", "End conference"};
}

std::optional<std::vector<std::string>> choose_autopilot_tools(
 const std::vector<std::string>& initial) {
 const std::vector<std::string> labels = {
 "write_file: modify workspace files",
 "run_command: sandboxed workspace command only",
 "fetch_http: fetch public URL",
 "browse_page: read public web page",
 "web_search: public web search"};
 const std::vector<std::string> names = {"write_file", "run_command", "fetch_http",
 "browse_page", "web_search"};
 std::vector<bool> checked(names.size(), false);
 for (std::size_t index = 0; index < names.size(); ++index) {
 checked[index] = std::find(initial.begin(), initial.end(), names[index]) != initial.end();
 }
 int selected = 0;
 for (;;) {
 erase();
 attron(COLOR_PAIR(1) | A_BOLD);
 mvaddnstr(0, 2, "Autopilot preauthorization", std::max(0, COLS - 4));
 attroff(COLOR_PAIR(1) | A_BOLD);
 mvhline(1, 0, ACS_HLINE, COLS);
 mvaddnstr(3, 2, "Only checked tools may be used automatically. Commands stay sandboxed; elevation is never automatic.",
 std::max(0, COLS - 4));
 for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
 if (index == selected) attron(COLOR_PAIR(2));
 const auto line = std::string(index == selected ? "> " : " ") +
 (checked[static_cast<std::size_t>(index)] ? "[x] " : "[ ] ") +
 labels[static_cast<std::size_t>(index)];
 mvaddnstr(6 + index, 2, clipped(line, std::max(0, COLS - 4)).c_str(), std::max(0, COLS - 4));
 if (index == selected) attroff(COLOR_PAIR(2));
 }
 footer("Up/Down select Space toggle Enter save Esc cancel");
 refresh();
 const int key = getch();
 if (key == KEY_UP && selected > 0) --selected;
 else if (key == KEY_DOWN && selected + 1 < static_cast<int>(labels.size())) ++selected;
 else if (key == ' ') checked[static_cast<std::size_t>(selected)] = !checked[static_cast<std::size_t>(selected)];
 else if (enter_key(key)) {
 std::vector<std::string> result;
 for (std::size_t index = 0; index < names.size(); ++index) if (checked[index]) result.push_back(names[index]);
 return result;
 } else if (escape_key(key)) return std::nullopt;
 }
}

void configure_autopilot(ConferenceEngine& engine, std::string& notice) {
 const auto initial = engine.snapshot();
 const auto enabled_choice = menu("Autopilot mode",
 "The moderator can automatically schedule participant rounds.",
 {"Manual progression", "Enable moderator autopilot"},
 initial.autopilot_enabled ? 1 : 0);
 if (!enabled_choice) return;
 const bool enabled = *enabled_choice == 1;
 const std::vector<std::string> limits = {"Unlimited until stopped", "4 rounds", "8 rounds", "12 rounds", "20 rounds"};
 const std::array<int, 5> values = {0, 4, 8, 12, 20};
 int current_limit = 3;
 for (int index = 0; index < static_cast<int>(values.size()); ++index) {
 if (values[static_cast<std::size_t>(index)] == initial.autopilot_round_limit) current_limit = index;
 }
 const auto limit = menu("Autopilot round limit",
 "Unlimited continues until the moderator concludes, requests a user-only decision, an error occurs, or you interrupt it.", limits, current_limit);
 if (!limit) return;
 const auto tools = choose_autopilot_tools(initial.autopilot_preauthorized_tools);
 if (!tools) return;
 if (enabled && !tools->empty() &&
 !confirm("Confirm automatic tool authorization",
 "Only the selected tools will be available in this conference. Every use remains recorded; sandbox elevation is prohibited.",
 "Enable selected permissions")) return;
 engine.set_autopilot(enabled, values[static_cast<std::size_t>(*limit)], *tools);
 notice = enabled ? "Autopilot policy saved; run it from Controls or /auto run"
 : "Autopilot disabled; selected permissions retained but inactive";
}

std::vector<std::string> enabled_provider_ids(const Config& config) {
 std::vector<std::string> ids;
 for (const auto& provider : config.providers) if (provider.enabled) ids.push_back(provider.id);
 return ids;
}

int default_agenda_budget(ConferenceDepth depth) {
 switch (depth) {
 case ConferenceDepth::quick: return 4;
 case ConferenceDepth::standard: return 8;
 case ConferenceDepth::deep: return 16;
 case ConferenceDepth::audit: return 24;
 }
 return 8;
}

std::string seat_label(const ConferenceParticipant& participant) {
 return "#" + std::to_string(participant.seat_number) + " " + participant.name +
 " [" + participant.provider + " / " + participant.model + "]" +
 (participant.enabled ? "" : " (disabled)");
}

void resize_advisors(std::vector<ConferenceParticipant>& participants, int count,
 const Conference& conference) {
 count = std::clamp(count, 1, 6);
 std::vector<ConferenceParticipant> result;
 for (const auto& participant : participants) {
 if (participant.kind == "moderator") result.push_back(participant);
 }
 std::vector<ConferenceParticipant> advisors;
 for (const auto& participant : participants) {
 if (participant.kind != "moderator") advisors.push_back(participant);
 }
 while (static_cast<int>(advisors.size()) < count) {
 const int seat = static_cast<int>(advisors.size()) + 1;
 advisors.push_back({"advisor-" + std::to_string(seat), seat,
 "Advisor #" + std::to_string(seat), "advisor seat ",
 " depth 。", conference.provider, conference.model,
 "advisor", true});
 }
 advisors.resize(static_cast<std::size_t>(count));
 for (int index = 0; index < static_cast<int>(advisors.size()); ++index) {
 auto& advisor = advisors[static_cast<std::size_t>(index)];
 advisor.seat_number = index + 1;
 if (advisor.id.empty()) advisor.id = "advisor-" + std::to_string(index + 1);
 result.push_back(advisor);
 }
 participants = std::move(result);
}

void edit_participant(ConferenceParticipant& participant, const Config& config) {
 for (;;) {
 const auto action = menu("Edit seat #" + std::to_string(participant.seat_number),
 participant.name + " | " + participant.provider + " / " + participant.model,
 {"Done", "Name", "Role", "Responsibility", "Provider",
 "Model", participant.enabled ? "Disable seat" : "Enable seat"});
 if (!action || *action == 0) return;
 if (*action == 1) {
 if (auto value = edit_text("Seat name", participant.name, "Enter applies; Esc cancels.")) participant.name = *value;
 } else if (*action == 2) {
 if (auto value = edit_text("Seat role", participant.role, "Enter applies; Esc cancels.")) participant.role = *value;
 } else if (*action == 3) {
 if (auto value = edit_text("Seat responsibility", participant.responsibility, "Enter applies; Esc cancels.")) participant.responsibility = *value;
 } else if (*action == 4) {
 const auto providers = enabled_provider_ids(config);
 auto current = std::find(providers.begin(), providers.end(), participant.provider);
 const auto selected = menu("Seat provider", "Only enabled providers are shown.", providers,
 current == providers.end() ? 0 : static_cast<int>(current - providers.begin()));
 if (selected) {
 participant.provider = providers[static_cast<std::size_t>(*selected)];
 if (const auto* provider = config.find_provider(participant.provider); provider && !provider->default_model.empty()) {
 participant.model = provider->default_model;
 }
 }
 } else if (*action == 5) {
 if (auto value = edit_text("Seat model", participant.model, "Model ID for selected provider.")) participant.model = *value;
 } else if (*action == 6) {
 participant.enabled = !participant.enabled;
 }
 }
}

void review_meeting_setup(ConferenceEngine& engine, std::string& notice) {
 auto conference = engine.snapshot();
 auto depth = conference.setup.depth;
 auto participants = conference.participants;
 int agenda_budget = conference.setup.agenda_turn_budget;
 for (;;) {
 const int advisors = static_cast<int>(std::count_if(participants.begin(), participants.end(),
 [](const auto& item) { return item.kind != "moderator"; }));
 std::vector<std::string> choices = {
 conference.status == ConferenceStatus::awaiting_setup ? "Approve plan and start meeting" : "Save meeting parameters",
 "Discussion depth: " + conference_depth_name(depth) + " (" + std::to_string(agenda_budget) + " turn budget)",
 "Advisor seats: " + std::to_string(advisors),
 "Edit a numbered seat",
 "Edit meeting rules",
 "Regenerate moderator proposal",
 "Cancel"};
 const auto selected = menu("Meeting plan review",
 "#0 is the moderator. It orchestrates and evaluates; numbered advisors provide the deep analysis.", choices);
 if (!selected || *selected == 6) return;
 if (*selected == 0) {
 engine.update_setup(depth, advisors, agenda_budget, participants);
 if (conference.status == ConferenceStatus::awaiting_setup) engine.approve_setup();
 notice = conference.status == ConferenceStatus::awaiting_setup
 ? "Meeting plan approved; moderator #0 is ready to speak"
 : "Meeting parameters saved; they apply to the next AI request";
 return;
 }
 if (*selected == 1) {
 const std::vector<ConferenceDepth> values = {ConferenceDepth::quick, ConferenceDepth::standard,
 ConferenceDepth::deep, ConferenceDepth::audit};
 std::vector<std::string> labels;
 int current = 0;
 for (int index = 0; index < static_cast<int>(values.size()); ++index) {
 labels.push_back(conference_depth_name(values[static_cast<std::size_t>(index)]) +
 " (" + std::to_string(default_agenda_budget(values[static_cast<std::size_t>(index)])) + " turns)");
 if (values[static_cast<std::size_t>(index)] == depth) current = index;
 }
 if (const auto choice = menu("Discussion depth", "Depth controls the meeting turn budget and guides response size.", labels, current)) {
 depth = values[static_cast<std::size_t>(*choice)];
 agenda_budget = default_agenda_budget(depth);
 }
 } else if (*selected == 2) {
 std::vector<std::string> counts;
 for (int count = 1; count <= 6; ++count) counts.push_back(std::to_string(count) + " advisor seats");
 if (const auto choice = menu("Advisor count", "Each advisor can use a different provider and model.", counts, advisors - 1)) {
 resize_advisors(participants, *choice + 1, conference);
 }
 } else if (*selected == 3) {
 std::vector<std::string> seats;
 for (const auto& participant : participants) seats.push_back(seat_label(participant));
 if (const auto choice = menu("Select seat", "Use a distinct model per seat when that adds a useful perspective.", seats)) {
 edit_participant(participants[static_cast<std::size_t>(*choice)], engine.config());
 }
 } else if (*selected == 4) {
 if (auto rules = edit_text("Conference rules", conference.rules, "Rules apply to future turns.")) {
 conference.rules = *rules;
 engine.update_rules(*rules);
 }
 } else if (*selected == 5) {
 engine.prepare_setup();
 conference = engine.snapshot();
 depth = conference.setup.depth;
 participants = conference.participants;
 agenda_budget = conference.setup.agenda_turn_budget;
 notice = "Moderator proposal regenerated; review it before approval";
 }
 }
}

void choose_next_speaker(ConferenceEngine& engine, std::string& notice) {
 const auto conference = engine.snapshot();
 std::vector<const ConferenceParticipant*> seats;
 std::vector<std::string> labels;
 for (const auto& participant : conference.participants) {
 if (!participant.enabled) continue;
 seats.push_back(&participant);
 labels.push_back(seat_label(participant) + (participant.kind == "moderator" ? " - coordinate and evaluate" : " - deep analysis"));
 }
 if (const auto selected = menu("Choose next speaker", "This user choice overrides the current schedule for the next request.", labels)) {
 const auto& participant = *seats[static_cast<std::size_t>(*selected)];
 const auto reason = edit_text("Why this speaker", "User requested this contribution.", "Enter confirms the scheduling reason.");
 if (!reason) return;
 engine.assign_next_speaker(participant.id, *reason, participant.kind != "moderator", true);
 notice = "Next speaker set to #" + std::to_string(participant.seat_number) + " " + participant.name;
 }
}

void handle_input(ConferenceEngine& engine, const std::string& input, std::string& notice) {
 const auto [command, argument] = split_command(input);
 if (command == "/pause") { engine.pause(); notice = "Conference paused"; return; }
 if (command == "/resume") { engine.resume(); notice = "Conference resumed"; return; }
 if (command == "/end") { engine.stop(); notice = "Conference stopped"; return; }
 if (command == "/advance") {
 if (engine.snapshot().status == ConferenceStatus::awaiting_setup || engine.snapshot().status == ConferenceStatus::draft) {
 notice = "Review and approve the meeting plan before advancing"; return;
 }
 if (engine.snapshot().status == ConferenceStatus::running) { engine.advance(); notice = "AI speaker started in background"; }
 else notice = "Start or resume the conference before advancing";
 return;
 }
 if (command == "/execute") {
 if (engine.snapshot().status == ConferenceStatus::draft) engine.start();
 if (engine.snapshot().status != ConferenceStatus::running) {
 notice = "Start or resume the conference before executing";
 return;
 }
 if (confirm("Authorize execution turn",
 "The next participant may use full workspace and external tools only for this turn. Tool requests and results will remain in the meeting timeline.",
 "Authorize execution")) {
 engine.advance(true);
 notice = "Authorized execution started in background";
 }
 return;
 }
 if (command == "/autopilot") { configure_autopilot(engine, notice); return; }
 if (command == "/setup") { review_meeting_setup(engine, notice); return; }
 if (command == "/next") { choose_next_speaker(engine, notice); return; }
 if (command == "/auto") {
 if (argument == "off") {
 const auto conference = engine.snapshot();
 engine.set_autopilot(false, conference.autopilot_round_limit,
 conference.autopilot_preauthorized_tools);
 notice = "Autopilot disabled";
 } else if (argument == "on") {
 const auto conference = engine.snapshot();
 engine.set_autopilot(true, conference.autopilot_round_limit,
 conference.autopilot_preauthorized_tools);
 notice = "Autopilot enabled with its saved permissions";
 } else if (argument == "run" || argument.empty()) {
 engine.run_autopilot();
 notice = engine.snapshot().autopilot_enabled ? "Autopilot started in background"
 : "Configure autopilot before running it";
 } else notice = "Usage: /auto [run|on|off] or /autopilot";
 return;
 }
 if (command == "/summary") {
 (void)edit_text("Meeting summary", engine.summary(), "Esc or Enter closes.", true);
 return;
 }
 if (command == "/goal") {
 if (argument.empty()) { notice = "Usage: /goal <new goal>"; return; }
 engine.update_goal(argument);
 notice = "Goal updated; conference awaits you";
 return;
 }
 if (command == "/rules") {
 (void)edit_text("Conference rules", engine.snapshot().rules, "Esc or Enter closes.", true);
 return;
 }
 if (command == "/rule" && argument == "edit") {
 if (auto rules = edit_text("Conference rules", engine.snapshot().rules, "Rules apply to future turns.")) {
 engine.update_rules(*rules); notice = "Rules updated";
 }
 return;
 }
 if (command == "/focus") {
 try {
 const auto selected = std::stoul(argument);
 if (selected == 0 || selected > engine.snapshot().agenda.size()) throw std::out_of_range("agenda");
 engine.focus_agenda(selected - 1);
 notice = "Agenda focus updated";
 } catch (...) { notice = "Usage: /focus <agenda number>"; }
 return;
 }
 if (command == "/ask") {
 const auto [role, question] = split_command(argument);
 if (role.empty() || question.empty()) { notice = "Usage: /ask <role> <question>"; return; }
 engine.interrupt("Directed question for " + role + ": " + question);
 notice = "Directed user interruption recorded";
 return;
 }
 if (command == "/decision") {
 const auto decisions = engine.snapshot().decisions;
 if (decisions.empty()) { notice = "No candidate decisions yet"; return; }
 auto selected = menu("Candidate decisions", "Choose a decision to resolve.", decisions);
 if (!selected) return;
 auto action = menu("Resolve decision", decisions[static_cast<std::size_t>(*selected)],
 {"Cancel", "Confirm decision", "Request more evidence", "Continue discussion", "Reject decision"});
 if (!action || *action == 0) return;
 static const std::array<std::string, 5> outcomes = {
 "", "confirmed", "evidence", "continue", "rejected"};
 engine.resolve_decision(static_cast<std::size_t>(*selected), outcomes[static_cast<std::size_t>(*action)]);
 notice = "Decision state recorded";
 return;
 }
 if (command == "/export") {
 try {
 const auto path = engine.export_summary(argument);
 notice = "Summary exported to " + path.string();
 } catch (const std::exception& error) { notice = error.what(); }
 return;
 }
 engine.interrupt(input);
 notice = "User interruption recorded; conference awaits you";
}

void draw(const Conference& conference, Focus focus, int agenda_selected, int event_offset,
 int control_selected, const std::string& input, const std::string& notice,
 bool follow_live) {
 header(conference);
 const int input_row = LINES - 3;
 const int top = 5;
 const int discussion_rows = std::max(1, input_row - top - 3);
 const int left_width = std::clamp(COLS / 4, 25, 36);
 const int right_width = std::clamp(COLS / 5, 22, 31);
 const bool narrow = COLS < 86;
 if (narrow) {
 int row = top + 2;
 if (focus == Focus::agenda) {
 section(top, 1, COLS - 2, "Agenda & State", true);
 for (int index = 0; index < static_cast<int>(conference.agenda.size()) && row < input_row - 1; ++index) {
 const auto& item = conference.agenda[static_cast<std::size_t>(index)];
 if (index == agenda_selected) attron(COLOR_PAIR(2));
 const auto line = agenda_marker(item) + " " + item.title;
 mvaddnstr(row++, 2, clipped(line, COLS - 4).c_str(), COLS - 4);
 if (index == agenda_selected) attroff(COLOR_PAIR(2));
 }
 if (row < input_row - 1) { attron(A_BOLD); mvaddstr(row++, 2, "Facts"); attroff(A_BOLD); }
 for (const auto& fact : conference.facts) {
 if (row >= input_row - 1) break;
 mvaddnstr(row++, 2, clipped("[x] " + fact, COLS - 4).c_str(), COLS - 4);
 }
 } else if (focus == Focus::controls) {
 section(top, 1, COLS - 2, "Controls", true);
 const auto controls = controls_for(conference);
 for (int index = 0; index < static_cast<int>(controls.size()) && row < input_row - 1; ++index) {
 if (index == control_selected) attron(COLOR_PAIR(2));
 const auto line = (index == control_selected ? "> " : " ") + controls[static_cast<std::size_t>(index)];
 mvaddnstr(row++, 2, clipped(line, COLS - 4).c_str(), COLS - 4);
 if (index == control_selected) attroff(COLOR_PAIR(2));
 }
 } else {
 section(top, 1, COLS - 2, "Discussion", focus == Focus::discussion);
 const int start = follow_live ? live_event_start(conference.events, discussion_rows)
 : std::max(0, event_offset);
 for (int index = start; index < static_cast<int>(conference.events.size()) && row < input_row - 1; ++index) {
 const auto& event = conference.events[static_cast<std::size_t>(index)];
 attron(event.type == "user" ? COLOR_PAIR(3) | A_BOLD : A_BOLD);
 const auto label = "[" + event.author + " | " + event_kind(event) + "]";
 mvaddnstr(row++, 2, clipped(label, COLS - 4).c_str(), COLS - 4);
 attroff(event.type == "user" ? COLOR_PAIR(3) | A_BOLD : A_BOLD);
 const auto content = follow_live && index + 1 == static_cast<int>(conference.events.size())
 ? tail_lines(event_content(event), std::max(1, input_row - row - 1))
 : event_content(event);
 text_lines(row, 4, COLS - 6, input_row - 1, content);
 }
 }
 } else {
 const int center_left = left_width + 1;
 const int center_width = std::max(18, COLS - left_width - right_width - 2);
 const int right_left = center_left + center_width + 1;
 mvvline(top, left_width, ACS_VLINE, input_row - top);
 mvvline(top, right_left - 1, ACS_VLINE, input_row - top);
 section(top, 1, left_width - 1, "Agenda & State", focus == Focus::agenda);
 int left_row = top + 2;
 for (int index = 0; index < static_cast<int>(conference.agenda.size()) && left_row < input_row - 1; ++index) {
 const auto& item = conference.agenda[static_cast<std::size_t>(index)];
 if (focus == Focus::agenda && index == agenda_selected) attron(COLOR_PAIR(2));
 const auto line = agenda_marker(item) + " " + item.title;
 mvaddnstr(left_row++, 2, clipped(line, left_width - 3).c_str(), left_width - 3);
 if (focus == Focus::agenda && index == agenda_selected) attroff(COLOR_PAIR(2));
 }
 if (left_row < input_row - 1) { attron(A_BOLD); mvaddnstr(left_row++, 2, "Meeting seats", left_width - 3); attroff(A_BOLD); }
 for (const auto& participant : conference.participants) {
 if (left_row >= input_row - 1) break;
 const auto line = "#" + std::to_string(participant.seat_number) + " " + participant.name +
 (participant.id == conference.next_speaker_id ? " *" : "");
 mvaddnstr(left_row++, 2, clipped(line, left_width - 3).c_str(), left_width - 3);
 }
 if (left_row < input_row - 1) { attron(A_BOLD); mvaddnstr(left_row++, 2, "Facts", left_width - 3); attroff(A_BOLD); }
 for (const auto& fact : conference.facts) {
 if (left_row >= input_row - 1) break;
 mvaddnstr(left_row++, 2, clipped("[x] " + fact, left_width - 3).c_str(), left_width - 3);
 }
 if (left_row < input_row - 1) { attron(A_BOLD); mvaddnstr(left_row++, 2, "Open questions", left_width - 3); attroff(A_BOLD); }
 for (const auto& question : conference.open_questions) {
 if (left_row >= input_row - 1) break;
 mvaddnstr(left_row++, 2, clipped("[?] " + question, left_width - 3).c_str(), left_width - 3);
 }
 section(top, center_left, center_width - 1, "Discussion", focus == Focus::discussion);
 int row = top + 2;
 const int start = follow_live ? live_event_start(conference.events, discussion_rows)
 : std::max(0, event_offset);
 for (int index = start; index < static_cast<int>(conference.events.size()) && row < input_row - 1; ++index) {
 const auto& event = conference.events[static_cast<std::size_t>(index)];
 attron(event.type == "user" ? COLOR_PAIR(3) | A_BOLD : A_BOLD);
 const auto label = "[" + event.author + " | " + event_kind(event) + "]";
 mvaddnstr(row++, center_left + 1, clipped(label, center_width - 3).c_str(), center_width - 3);
 attroff(event.type == "user" ? COLOR_PAIR(3) | A_BOLD : A_BOLD);
 const auto content = follow_live && index + 1 == static_cast<int>(conference.events.size())
 ? tail_lines(event_content(event), std::max(1, input_row - row - 1))
 : event_content(event);
 text_lines(row, center_left + 3, center_width - 5, input_row - 1, content);
 }
 section(top, right_left, right_width - 1, "Controls", focus == Focus::controls);
 const auto controls = controls_for(conference);
 for (int index = 0; index < static_cast<int>(controls.size()) && top + 2 + index < input_row - 1; ++index) {
 if (focus == Focus::controls && index == control_selected) attron(COLOR_PAIR(2));
 const auto line = (index == control_selected ? "> " : " ") + controls[static_cast<std::size_t>(index)];
 mvaddnstr(top + 2 + index, right_left + 1, clipped(line, right_width - 3).c_str(), right_width - 3);
 if (focus == Focus::controls && index == control_selected) attroff(COLOR_PAIR(2));
 }
 }
 mvhline(input_row - 1, 0, ACS_HLINE, COLS);
 if (focus == Focus::input) attron(A_REVERSE);
 mvaddnstr(input_row, 1, std::string(static_cast<std::size_t>(std::max(0, COLS - 2)), ' ').c_str(), std::max(0, COLS - 2));
 const auto text = input.empty()
 ? (pending_user_question(conference) ? "Enter answer to moderator question or /command" : "Enter an interruption or /command")
 : input;
 mvaddnstr(input_row, 1, clipped(text, std::max(0, COLS - 2)).c_str(), std::max(0, COLS - 2));
 if (focus == Focus::input) attroff(A_REVERSE);
 const std::string timeline_state = follow_live ? "Timeline: live (f toggles)" : "Timeline: reviewing (End resumes live)";
 footer(notice.empty() ? timeline_state + " | Left/Right focus Enter act Space pause ? help Esc exit" : notice + " | " + timeline_state);
 refresh();
}

void control(ConferenceEngine& engine, int selection, std::string& notice) {
 const auto conference = engine.snapshot();
 if (conference.status == ConferenceStatus::preparing) {
 if (selection == 1) {
 (void)edit_text("Meeting summary", engine.summary(), "Esc or Enter closes.", true);
 } else if (selection == 2 && confirm("End conference", "Stop this meeting while preserving the saved history.", "End")) {
 engine.stop(); notice = "Conference stopped";
 } else if (selection == 0) {
 notice = "Moderator is generating the plan in the background";
 }
 return;
 }
 if (conference.status == ConferenceStatus::awaiting_setup || conference.status == ConferenceStatus::draft) {
 switch (selection) {
 case 0: review_meeting_setup(engine, notice); return;
 case 1: engine.prepare_setup(); notice = "Moderator plan regenerated; review it before approval"; return;
 case 2: (void)edit_text("Meeting summary", engine.summary(), "Esc or Enter closes.", true); return;
 case 3:
 if (auto rules = edit_text("Conference rules", conference.rules, "Rules apply to future turns.")) {
 engine.update_rules(*rules); notice = "Rules updated";
 }
 return;
 case 4:
 if (confirm("End conference", "Stop this meeting while preserving the saved history.", "End")) {
 engine.stop(); notice = "Conference stopped";
 }
 return;
 }
 return;
 }
 if (conference.status == ConferenceStatus::awaiting_user && pending_user_question(conference)) {
 switch (selection) {
 case 0: answer_pending_question(engine, conference, notice); return;
 case 1: engine.interrupt("User question；Please missing information CONTINUE。 "); notice = "No-answer response recorded"; return;
 case 2: review_meeting_setup(engine, notice); return;
 case 3: (void)edit_text("Meeting summary", engine.summary(), "Esc or Enter closes.", true); return;
 case 4:
 if (confirm("End conference", "Stop this meeting while preserving the saved history.", "End")) {
 engine.stop(); notice = "Conference stopped";
 }
 return;
 }
 }
 switch (selection) {
 case 0:
 if (conference.status == ConferenceStatus::running) engine.pause(); else engine.resume();
 notice = "Conference status updated"; break;
 case 1:
 if (conference.status == ConferenceStatus::draft) engine.start();
 if (engine.snapshot().status == ConferenceStatus::running) { engine.advance(); notice = "AI speaker started in background"; }
 else notice = "Start or resume the conference before advancing";
 break;
 case 2: review_meeting_setup(engine, notice); break;
 case 3: choose_next_speaker(engine, notice); break;
 case 4:
 if (!conference.autopilot_enabled) { notice = "Configure autopilot permissions before running"; break; }
 engine.run_autopilot(); notice = "Autopilot started in background"; break;
 case 5: configure_autopilot(engine, notice); break;
 case 6: notice = "Focus the input and press Enter to send an interruption"; break;
 case 7: (void)edit_text("Meeting summary", engine.summary(), "Esc or Enter closes.", true); break;
 case 8:
 if (auto rules = edit_text("Conference rules", conference.rules, "Rules apply to future turns.")) {
 engine.update_rules(*rules); notice = "Rules updated";
 }
 break;
 case 9:
 if (conference.status != ConferenceStatus::running) {
 notice = "Start or resume the conference before executing";
 break;
 }
 if (confirm("Authorize execution turn",
 "The next participant may use full workspace and external tools only for this turn. Tool requests and results will remain in the meeting timeline.",
 "Authorize execution")) {
 engine.advance(true);
 notice = "Authorized execution started in background";
 }
 break;
 case 10: notice = "Select an agenda item in the left panel and press Enter"; break;
 case 11:
 if (confirm("Conclude meeting", "Mark remaining agenda items complete and produce the structured record.", "Conclude")) {
 engine.conclude(); notice = "Conference completed";
 }
 break;
 case 12:
 if (confirm("End conference", "Stop this meeting while preserving the saved history.", "End")) {
 engine.stop(); notice = "Conference stopped";
 }
 break;
 }
}

} // namespace

std::optional<std::string> Tui::choose_conference(ConferenceStore& store) {
 ConferenceScreen screen;
 const auto conferences = store.list(200);
 if (conferences.empty()) {
 erase(); mvaddstr(2, 2, "No saved AI Conferences."); footer("Press any key to return"); refresh(); getch();
 return std::nullopt;
 }
 int selected = 0;
 for (;;) {
 erase(); attron(COLOR_PAIR(1) | A_BOLD); mvaddstr(0, 2, "Resume AI Conference"); attroff(COLOR_PAIR(1) | A_BOLD);
 mvhline(1, 0, ACS_HLINE, COLS);
 for (int index = 0; index < static_cast<int>(conferences.size()) && index + 3 < LINES - 1; ++index) {
 if (index == selected) attron(COLOR_PAIR(2));
 const auto& item = conferences[static_cast<std::size_t>(index)];
 const auto text = (index == selected ? "> " : " ") + status_text(item.status) + " " + item.title;
 mvaddnstr(3 + index, 2, clipped(text, COLS - 4).c_str(), COLS - 4);
 if (index == selected) attroff(COLOR_PAIR(2));
 }
 footer("Up/Down select Enter resume Esc cancel"); refresh();
 const int key = getch();
 if (key == KEY_UP && selected > 0) --selected;
 else if (key == KEY_DOWN && selected + 1 < static_cast<int>(conferences.size())) ++selected;
 else if (enter_key(key)) return conferences[static_cast<std::size_t>(selected)].id;
 else if (escape_key(key)) return std::nullopt;
 }
}

void Tui::run_conference(ConferenceEngine& engine) {
 ConferenceScreen screen;
 Focus focus = Focus::discussion;
 int agenda_selected = 0;
 int event_offset = live_event_start(engine.snapshot().events, std::max(1, LINES - 11));
 int control_selected = 0;
 std::string input;
 std::string notice;
 bool follow_live = true;
 for (;;) {
 engine.check_user_question_timeouts();
 const auto conference = engine.snapshot();
 if (follow_live) event_offset = live_event_start(conference.events, std::max(1, LINES - 11));
 agenda_selected = std::clamp(agenda_selected, 0,
 std::max(0, static_cast<int>(conference.agenda.size()) - 1));
 control_selected = std::clamp(control_selected, 0,
 std::max(0, static_cast<int>(controls_for(conference).size()) - 1));
 draw(conference, focus, agenda_selected, event_offset, control_selected, input, notice, follow_live);
 const int key = getch();
 if (key == ERR) continue;
 if (key == '?') {
 (void)edit_text("AI Conference help", "Left/Right: change focus\nUp/Down: select or scroll\nEnd: return to live discussion\nf: toggle live follow\nEnter: open or execute\nSpace: pause/resume\nTab: next focus\ni: focus input\nEsc: leave\nText + Enter: high-priority interruption\n/setup review meeting plan\n/advance (read-only) /execute (user-authorized write turn)\n/auto [run|on|off] /autopilot (configure selected permissions)\n/summary /goal TEXT /rules /rule edit /focus N\n/ask ROLE QUESTION /decision /export [path] /pause /resume /end", "Esc or Enter closes.", true);
 continue;
 }
 if (escape_key(key)) {
 if (!input.empty()) { input.clear(); notice = "Input discarded"; continue; }
 if (confirm("Leave AI Conference", "Current state is saved before leaving.", "Leave")) {
 return;
 }
 continue;
 }
 if (key == KEY_F(1)) { focus = Focus::discussion; continue; }
 if (key == KEY_F(2)) { focus = Focus::agenda; continue; }
 if (key == KEY_F(3)) { focus = Focus::controls; continue; }
 if (key == '\t' || key == KEY_RIGHT) { focus = static_cast<Focus>((static_cast<int>(focus) + 1) % 4); continue; }
 if (key == KEY_BTAB) { focus = static_cast<Focus>((static_cast<int>(focus) + 3) % 4); continue; }
 if (key == KEY_LEFT) { focus = static_cast<Focus>((static_cast<int>(focus) + 3) % 4); continue; }
 if (key == 'i') { focus = Focus::input; continue; }
 if (key == 'f' && focus == Focus::discussion) {
 follow_live = !follow_live;
 if (follow_live) event_offset = live_event_start(conference.events, std::max(1, LINES - 11));
 notice = follow_live ? "Timeline follows live discussion" : "Timeline review mode";
 continue;
 }
 if (key == ' ') {
 if (engine.snapshot().status == ConferenceStatus::running) engine.pause(); else engine.resume();
 notice = "Conference status updated"; continue;
 }
 if (focus == Focus::input) {
 if (enter_key(key)) {
 if (input.empty()) { notice = "Type an interruption before sending"; continue; }
 handle_input(engine, input, notice);
 input.clear(); focus = Focus::discussion;
 } else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && !input.empty()) input.pop_back();
 else if (key >= 32 && key <= 126 && input.size() < 16384) input.push_back(static_cast<char>(key));
 continue;
 }
 if (focus == Focus::agenda) {
 if (key == KEY_UP && agenda_selected > 0) --agenda_selected;
 else if (key == KEY_DOWN && agenda_selected + 1 < static_cast<int>(conference.agenda.size())) ++agenda_selected;
 else if (enter_key(key)) { engine.focus_agenda(static_cast<std::size_t>(agenda_selected)); notice = "Agenda focus updated"; }
 continue;
 }
 if (focus == Focus::discussion) {
 if (key == KEY_UP && event_offset > 0) { --event_offset; follow_live = false; }
 else if (key == KEY_DOWN && event_offset + 1 < static_cast<int>(conference.events.size())) {
 ++event_offset;
 follow_live = event_offset + 1 >= static_cast<int>(conference.events.size());
 }
 else if (key == KEY_PPAGE) event_offset = std::max(0, event_offset - std::max(1, LINES - 8));
 else if (key == KEY_NPAGE) event_offset = std::min(std::max(0, static_cast<int>(conference.events.size()) - 1), event_offset + std::max(1, LINES - 8));
 else if (key == KEY_HOME) event_offset = 0;
 else if (key == KEY_END) { event_offset = live_event_start(conference.events, std::max(1, LINES - 11)); follow_live = true; }
 if (key == KEY_PPAGE || key == KEY_NPAGE || key == KEY_HOME) follow_live = false;
 continue;
 }
 if (focus == Focus::controls) {
 const auto count = static_cast<int>(controls_for(conference).size());
 if (key == KEY_UP && control_selected > 0) --control_selected;
 else if (key == KEY_DOWN && control_selected + 1 < count) ++control_selected;
 else if (enter_key(key)) control(engine, control_selected, notice);
 }
 }
}

} // namespace ask
