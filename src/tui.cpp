#include "ask/tui.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <ncurses.h>

namespace ask {
namespace {

class Screen {
 public:
  Screen() {
    if (!initscr()) throw std::runtime_error("cannot initialize terminal UI");
    cbreak();
    noecho();
    keypad(stdscr, true);
    curs_set(0);
    use_default_colors();
    if (has_colors()) {
      start_color();
      init_pair(1, COLOR_CYAN, -1);
      init_pair(2, COLOR_BLACK, COLOR_CYAN);
      init_pair(3, COLOR_YELLOW, -1);
      init_pair(4, COLOR_RED, -1);
    }
  }
  ~Screen() { endwin(); }
};

std::string clipped(const std::string& text, int width) {
  if (width <= 0) return {};
  if (static_cast<int>(text.size()) <= width) return text;
  if (width <= 3) return text.substr(0, static_cast<std::size_t>(width));
  return text.substr(0, static_cast<std::size_t>(width - 3)) + "...";
}

void title(const std::string& text) {
  attron(COLOR_PAIR(1) | A_BOLD);
  mvaddnstr(0, 2, text.c_str(), COLS - 4);
  attroff(COLOR_PAIR(1) | A_BOLD);
  mvhline(1, 0, ACS_HLINE, COLS);
}

void footer(const std::string& text) {
  attron(A_DIM);
  mvhline(LINES - 2, 0, ACS_HLINE, COLS);
  mvaddnstr(LINES - 1, 1, text.c_str(), COLS - 2);
  attroff(A_DIM);
}

std::string prompt_line(const std::string& label, const std::string& initial = {}, bool secret = false) {
  move(LINES - 3, 0);
  clrtoeol();
  attron(COLOR_PAIR(3));
  addnstr(label.c_str(), COLS / 2);
  attroff(COLOR_PAIR(3));
  if (!initial.empty() && !secret) {
    addstr(" [");
    addnstr(initial.c_str(), std::max(0, COLS / 2 - 4));
    addstr("]");
  }
  addstr(": ");
  char buffer[8192] = {};
  curs_set(1);
  if (!secret) echo();
  else noecho();
  getnstr(buffer, static_cast<int>(sizeof(buffer) - 1));
  noecho();
  curs_set(0);
  std::string value(buffer);
  return value.empty() ? initial : value;
}

bool confirm(const std::string& message) {
  move(LINES - 3, 0);
  clrtoeol();
  attron(COLOR_PAIR(4) | A_BOLD);
  addnstr((message + " [y/N]").c_str(), COLS - 1);
  attroff(COLOR_PAIR(4) | A_BOLD);
  int key = getch();
  return key == 'y' || key == 'Y';
}

int parse_int(const std::string& value, int fallback, int minimum, int maximum) {
  try {
    return std::clamp(std::stoi(value), minimum, maximum);
  } catch (...) {
    return fallback;
  }
}

double parse_double(const std::string& value, double fallback, double minimum, double maximum) {
  try {
    return std::clamp(std::stod(value), minimum, maximum);
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> split_csv(const std::string& text) {
  std::vector<std::string> result;
  std::istringstream input(text);
  std::string item;
  while (std::getline(input, item, ',')) {
    auto first = item.find_first_not_of(" \t");
    auto last = item.find_last_not_of(" \t");
    if (first != std::string::npos) result.push_back(item.substr(first, last - first + 1));
  }
  return result;
}

std::string join_csv(const std::vector<std::string>& values) {
  std::string result;
  for (const auto& value : values) {
    if (!result.empty()) result += ", ";
    result += value;
  }
  return result;
}

std::string headers_json(const std::map<std::string, std::string>& headers) {
  Json::Value value(Json::objectValue);
  for (const auto& [name, content] : headers) value[name] = content;
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

bool parse_headers(const std::string& text, std::map<std::string, std::string>& output) {
  Json::CharReaderBuilder builder;
  Json::Value value;
  std::string errors;
  std::istringstream input(text);
  if (!Json::parseFromStream(builder, input, &value, &errors) || !value.isObject()) return false;
  output.clear();
  for (const auto& name : value.getMemberNames()) output[name] = value[name].asString();
  return true;
}

Provider edit_provider(Provider provider, bool is_new) {
  provider.id = prompt_line("Provider ID", provider.id);
  provider.name = prompt_line("Display name", provider.name.empty() ? provider.id : provider.name);
  provider.protocol = prompt_line("Protocol (openai/anthropic/gemini)", provider.protocol);
  provider.base_url = prompt_line("API base URL", provider.base_url);
  provider.api_key_env = prompt_line("API key environment variable", provider.api_key_env);
  auto key = prompt_line(is_new ? "API key (blank to omit)" : "New API key (blank keeps current)", {}, true);
  if (!key.empty()) provider.api_key = key;
  provider.models = split_csv(prompt_line("Models (comma separated)", join_csv(provider.models)));
  provider.default_model = prompt_line("Provider default model", provider.default_model);
  provider.context_window = parse_int(prompt_line("Context window", std::to_string(provider.context_window)),
                                      provider.context_window, 1024, 10000000);
  provider.timeout_seconds = parse_int(prompt_line("Timeout seconds", std::to_string(provider.timeout_seconds)),
                                       provider.timeout_seconds, 1, 3600);
  auto raw_headers = prompt_line("Extra headers JSON", headers_json(provider.headers));
  if (!parse_headers(raw_headers, provider.headers)) {
    move(LINES - 3, 0);
    clrtoeol();
    addstr("Invalid headers JSON; previous headers kept. Press any key.");
    getch();
  }
  return provider;
}

std::string date_text(std::int64_t seconds) {
  std::time_t value = static_cast<std::time_t>(seconds);
  std::tm local{};
  localtime_r(&value, &local);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
  return buffer;
}

void render_config(const Config& config, int selected, const std::string& status) {
  erase();
  title("ask provider configuration");
  mvaddstr(2, 2, "Default:");
  attron(A_BOLD);
  addnstr((config.default_provider + " / " + config.default_model).c_str(), COLS - 14);
  attroff(A_BOLD);
  mvaddstr(3, 2, "On  ID                 Protocol     Default model             API base");
  const int rows = std::max(1, LINES - 12);
  int first = std::max(0, selected - rows + 1);
  for (int row = 0; row < rows && first + row < static_cast<int>(config.providers.size()); ++row) {
    const int index = first + row;
    const auto& provider = config.providers[static_cast<std::size_t>(index)];
    if (index == selected) attron(COLOR_PAIR(2));
    std::ostringstream line;
    line << (provider.enabled ? " *  " : "    ") << std::left << std::setw(19) << clipped(provider.id, 18)
         << std::setw(13) << clipped(provider.protocol, 12) << std::setw(26)
         << clipped(provider.default_model, 25) << provider.base_url;
    mvaddnstr(4 + row, 1, line.str().c_str(), COLS - 2);
    if (index == selected) attroff(COLOR_PAIR(2));
  }
  if (!config.providers.empty() && selected >= 0) {
    const auto& provider = config.providers[static_cast<std::size_t>(selected)];
    int detail = LINES - 7;
    mvaddnstr(detail, 2, ("Name: " + provider.name + " | key: " +
                         (provider.api_key_env.empty() ? (provider.api_key.empty() ? "not set" : "stored")
                                                       : "$" + provider.api_key_env))
                            .c_str(),
              COLS - 4);
    mvaddnstr(detail + 1, 2, ("Models: " + join_csv(provider.models)).c_str(), COLS - 4);
    mvaddnstr(detail + 2, 2,
              ("Context: " + std::to_string(provider.context_window) + " | timeout: " +
               std::to_string(provider.timeout_seconds) + "s | headers: " + headers_json(provider.headers))
                  .c_str(),
              COLS - 4);
  }
  if (!status.empty()) {
    attron(COLOR_PAIR(3));
    mvaddnstr(LINES - 3, 2, status.c_str(), COLS - 4);
    attroff(COLOR_PAIR(3));
  }
  footer("arrows navigate  a add  e edit  x delete  space enable  d default  m discover  g global  s save  q cancel");
  refresh();
}

}  // namespace

bool Tui::configure(ConfigStore& store, ChatClient* client) {
  Screen screen;
  Config config = store.load();
  int selected = config.providers.empty() ? -1 : 0;
  std::string status;
  for (;;) {
    selected = config.providers.empty() ? -1 : std::clamp(selected, 0, static_cast<int>(config.providers.size()) - 1);
    render_config(config, selected, status);
    status.clear();
    int key = getch();
    if (key == KEY_UP && selected > 0) --selected;
    else if (key == KEY_DOWN && selected + 1 < static_cast<int>(config.providers.size())) ++selected;
    else if (key == 'q' || key == 27) {
      if (confirm("Discard configuration changes?")) return false;
    } else if (key == 's') {
      try {
        store.save(config);
        return true;
      } catch (const std::exception& error) {
        status = error.what();
      }
    } else if (key == 'a') {
      Provider provider;
      provider.id = "custom";
      provider.name = "Custom provider";
      provider.protocol = "openai";
      provider.base_url = "https://example.com/v1";
      provider = edit_provider(provider, true);
      if (provider.id.empty() || config.find_provider(provider.id)) status = "Provider ID is empty or already exists";
      else {
        config.providers.push_back(std::move(provider));
        selected = static_cast<int>(config.providers.size()) - 1;
      }
    } else if (key == 'e' && selected >= 0) {
      auto old_id = config.providers[static_cast<std::size_t>(selected)].id;
      auto edited = edit_provider(config.providers[static_cast<std::size_t>(selected)], false);
      const auto* conflict = config.find_provider(edited.id);
      if (edited.id.empty() || (conflict && edited.id != old_id)) status = "Provider ID is empty or already exists";
      else {
        config.providers[static_cast<std::size_t>(selected)] = std::move(edited);
        if (config.default_provider == old_id) config.default_provider = config.providers[static_cast<std::size_t>(selected)].id;
      }
    } else if (key == 'x' && selected >= 0) {
      const auto id = config.providers[static_cast<std::size_t>(selected)].id;
      if (config.providers.size() == 1) status = "At least one provider is required";
      else if (confirm("Delete provider " + id + "?")) {
        config.providers.erase(config.providers.begin() + selected);
        if (config.default_provider == id) {
          config.default_provider = config.providers.front().id;
          config.default_model = config.providers.front().default_model;
        }
      }
    } else if (key == ' ' && selected >= 0) {
      auto& provider = config.providers[static_cast<std::size_t>(selected)];
      provider.enabled = !provider.enabled;
    } else if (key == 'd' && selected >= 0) {
      const auto& provider = config.providers[static_cast<std::size_t>(selected)];
      config.default_provider = provider.id;
      config.default_model = provider.default_model;
      status = "Default provider updated";
    } else if (key == 'm' && selected >= 0) {
      if (!client) {
        status = "Model discovery is unavailable";
      } else {
        try {
          auto& provider = config.providers[static_cast<std::size_t>(selected)];
          provider.models = client->fetch_models(provider);
          if (provider.default_model.empty() && !provider.models.empty()) provider.default_model = provider.models.front();
          status = "Discovered " + std::to_string(provider.models.size()) + " models";
        } catch (const std::exception& error) {
          status = error.what();
        }
      }
    } else if (key == 'g') {
      config.settings.auto_compact_ratio = parse_double(
          prompt_line("Auto compact ratio", std::to_string(config.settings.auto_compact_ratio)),
          config.settings.auto_compact_ratio, 0.2, 0.9);
      config.settings.max_tool_rounds = parse_int(
          prompt_line("Maximum tool rounds", std::to_string(config.settings.max_tool_rounds)),
          config.settings.max_tool_rounds, 1, 50);
      config.settings.max_output_tokens = parse_int(
          prompt_line("Maximum output tokens", std::to_string(config.settings.max_output_tokens)),
          config.settings.max_output_tokens, 256, 1000000);
      config.settings.system_prompt = prompt_line("System prompt", config.settings.system_prompt);
    }
  }
}

std::optional<std::string> Tui::choose_session(SessionStore& store) {
  Screen screen;
  auto sessions = store.list(200);
  if (sessions.empty()) {
    erase();
    title("resume conversation");
    mvaddstr(3, 2, "No saved conversations.");
    footer("press any key to return");
    refresh();
    getch();
    return std::nullopt;
  }
  int selected = 0;
  for (;;) {
    erase();
    title("resume conversation");
    const int list_width = std::clamp(COLS / 2, 34, 70);
    const int rows = std::max(1, LINES - 4);
    int first = std::max(0, selected - rows + 1);
    for (int row = 0; row < rows && first + row < static_cast<int>(sessions.size()); ++row) {
      int index = first + row;
      const auto& session = sessions[static_cast<std::size_t>(index)];
      if (index == selected) attron(COLOR_PAIR(2));
      auto line = date_text(session.updated_at) + " " + (session.do_mode ? "DO  " : "ASK ") + session.title;
      mvaddnstr(2 + row, 1, clipped(line, list_width - 2).c_str(), list_width - 2);
      if (index == selected) attroff(COLOR_PAIR(2));
    }
    mvvline(1, list_width, ACS_VLINE, LINES - 3);
    const auto& chosen = sessions[static_cast<std::size_t>(selected)];
    mvaddnstr(2, list_width + 2, chosen.id.c_str(), COLS - list_width - 3);
    mvaddnstr(3, list_width + 2, (chosen.provider + " / " + chosen.model).c_str(), COLS - list_width - 3);
    mvaddnstr(4, list_width + 2, ("cwd: " + chosen.cwd).c_str(), COLS - list_width - 3);
    if (auto full = store.load(chosen.id)) {
      int row = 6;
      const int start = std::max(0, static_cast<int>(full->messages.size()) - 8);
      for (int index = start; index < static_cast<int>(full->messages.size()) && row < LINES - 3; ++index) {
        const auto& message = full->messages[static_cast<std::size_t>(index)];
        attron(A_BOLD);
        mvaddnstr(row++, list_width + 2, (message.role + ":").c_str(), COLS - list_width - 3);
        attroff(A_BOLD);
        std::istringstream lines(message.content);
        std::string line;
        while (row < LINES - 3 && std::getline(lines, line)) {
          mvaddnstr(row++, list_width + 3, clipped(line, COLS - list_width - 4).c_str(), COLS - list_width - 4);
        }
      }
    }
    footer("arrows navigate  enter resume  x delete  q cancel");
    refresh();
    int key = getch();
    if (key == KEY_UP && selected > 0) --selected;
    else if (key == KEY_DOWN && selected + 1 < static_cast<int>(sessions.size())) ++selected;
    else if (key == '\n' || key == KEY_ENTER) return sessions[static_cast<std::size_t>(selected)].id;
    else if (key == 'q' || key == 27) return std::nullopt;
    else if (key == 'x' && confirm("Delete this saved conversation?")) {
      store.remove(sessions[static_cast<std::size_t>(selected)].id);
      sessions.erase(sessions.begin() + selected);
      if (sessions.empty()) return std::nullopt;
      selected = std::min(selected, static_cast<int>(sessions.size()) - 1);
    }
  }
}

bool Tui::choose_model(const Config& config, std::string& provider, std::string& model) {
  struct Choice { std::string provider; std::string model; };
  std::vector<Choice> choices;
  for (const auto& item : config.providers) {
    if (!item.enabled) continue;
    for (const auto& candidate : item.models) choices.push_back({item.id, candidate});
    if (item.models.empty() && !item.default_model.empty()) choices.push_back({item.id, item.default_model});
  }
  if (choices.empty()) return false;
  Screen screen;
  int selected = 0;
  for (std::size_t index = 0; index < choices.size(); ++index) {
    if (choices[index].provider == provider && choices[index].model == model) selected = static_cast<int>(index);
  }
  for (;;) {
    erase();
    title("select provider and model");
    const int rows = std::max(1, LINES - 4);
    int first = std::max(0, selected - rows + 1);
    for (int row = 0; row < rows && first + row < static_cast<int>(choices.size()); ++row) {
      int index = first + row;
      if (index == selected) attron(COLOR_PAIR(2));
      auto line = choices[static_cast<std::size_t>(index)].provider + " / " +
                  choices[static_cast<std::size_t>(index)].model;
      mvaddnstr(2 + row, 2, line.c_str(), COLS - 4);
      if (index == selected) attroff(COLOR_PAIR(2));
    }
    footer("arrows navigate  enter select  q cancel");
    refresh();
    int key = getch();
    if (key == KEY_UP && selected > 0) --selected;
    else if (key == KEY_DOWN && selected + 1 < static_cast<int>(choices.size())) ++selected;
    else if (key == '\n' || key == KEY_ENTER) {
      provider = choices[static_cast<std::size_t>(selected)].provider;
      model = choices[static_cast<std::size_t>(selected)].model;
      return true;
    } else if (key == 'q' || key == 27) return false;
  }
}

}  // namespace ask
