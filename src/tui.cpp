#include "ask/tui.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <clocale>
#include <ctime>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <ncurses.h>

namespace ask {
namespace {

class Screen {
 public:
  Screen() {
    std::setlocale(LC_CTYPE, "");
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
      init_pair(5, COLOR_GREEN, -1);
    }
  }
  ~Screen() { endwin(); }
};

bool is_up(int key) { return key == KEY_UP; }
bool is_down(int key) { return key == KEY_DOWN; }
bool is_left(int key) { return key == KEY_LEFT; }
bool is_right(int key) { return key == KEY_RIGHT; }
bool is_enter(int key) { return key == '\n' || key == KEY_ENTER; }
bool is_escape(int key) { return key == 27; }

std::string clipped(const std::string& text, int width) {
  if (width <= 0) return {};
  if (static_cast<int>(text.size()) <= width) return text;
  if (width <= 3) return text.substr(0, static_cast<std::size_t>(width));
  return text.substr(0, static_cast<std::size_t>(width - 3)) + "...";
}

void heading(const std::string& text, bool dirty = false) {
  erase();
  attron(COLOR_PAIR(1) | A_BOLD);
  mvaddnstr(0, 2, text.c_str(), std::max(0, COLS - 4));
  attroff(COLOR_PAIR(1) | A_BOLD);
  if (dirty) {
    const std::string marker = "Unsaved changes";
    const int marker_column = COLS - static_cast<int>(marker.size()) - 2;
    if (marker_column >= static_cast<int>(text.size()) + 5) {
      attron(COLOR_PAIR(3));
      mvaddnstr(0, marker_column, marker.c_str(), static_cast<int>(marker.size()));
      attroff(COLOR_PAIR(3));
    }
  }
  mvhline(1, 0, ACS_HLINE, COLS);
}

void footer(const std::string& text) {
  attron(A_DIM);
  mvhline(LINES - 2, 0, ACS_HLINE, COLS);
  mvaddnstr(LINES - 1, 1, text.c_str(), std::max(0, COLS - 2));
  attroff(A_DIM);
}

void message_line(const std::string& text, bool error = false) {
  move(LINES - 3, 0);
  clrtoeol();
  attron(COLOR_PAIR(error ? 4 : 3) | (error ? A_BOLD : A_NORMAL));
  mvaddnstr(LINES - 3, 2, text.c_str(), std::max(0, COLS - 4));
  attroff(COLOR_PAIR(error ? 4 : 3) | (error ? A_BOLD : A_NORMAL));
}

int first_visible(int selected, int count, int rows) {
  if (count <= rows) return 0;
  return std::clamp(selected - rows + 1, 0, count - rows);
}

void setting_row(int row, bool selected, const std::string& label, const std::string& value,
                 bool opens = false, bool danger = false) {
  if (selected) attron(COLOR_PAIR(2));
  if (danger && !selected) attron(COLOR_PAIR(4));
  const int label_width = std::clamp(COLS / 2, 22, 36);
  std::ostringstream line;
  line << (selected ? "> " : "  ") << std::left << std::setw(label_width) << clipped(label, label_width - 1)
       << clipped(value, std::max(0, COLS - label_width - 8));
  if (opens) line << "  >";
  mvaddnstr(row, 1, line.str().c_str(), std::max(0, COLS - 2));
  if (danger && !selected) attroff(COLOR_PAIR(4));
  if (selected) attroff(COLOR_PAIR(2));
}

std::optional<std::string> edit_text(const std::string& label, const std::string& initial,
                                     bool secret = false) {
  std::string value = initial;
  std::size_t cursor = value.size();
  for (;;) {
    heading(label);
    mvaddnstr(3, 2, "Edit value", std::max(0, COLS - 4));
    const int width = std::max(1, COLS - 6);
    std::size_t offset = cursor > static_cast<std::size_t>(width - 1)
                             ? cursor - static_cast<std::size_t>(width - 1)
                             : 0;
    auto visible = value.substr(offset, static_cast<std::size_t>(width));
    if (secret) visible.assign(visible.size(), '*');
    attron(A_REVERSE);
    mvaddnstr(5, 2, std::string(static_cast<std::size_t>(width), ' ').c_str(), width);
    mvaddnstr(5, 2, visible.c_str(), width);
    attroff(A_REVERSE);
    footer("Left/Right move cursor  Enter apply  Esc cancel");
    curs_set(1);
    move(5, 2 + static_cast<int>(cursor - offset));
    refresh();
    const int key = getch();
    curs_set(0);
    if (is_enter(key)) return value;
    if (is_escape(key)) return std::nullopt;
    if (key == KEY_LEFT && cursor > 0) --cursor;
    else if (key == KEY_RIGHT && cursor < value.size()) ++cursor;
    else if (key == KEY_HOME) cursor = 0;
    else if (key == KEY_END) cursor = value.size();
    else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && cursor > 0) {
      value.erase(cursor - 1, 1);
      --cursor;
    } else if (key == KEY_DC && cursor < value.size()) {
      value.erase(cursor, 1);
    } else if (key >= 32 && key <= 126 && value.size() < 8191) {
      value.insert(value.begin() + static_cast<std::ptrdiff_t>(cursor), static_cast<char>(key));
      ++cursor;
    }
  }
}

int parse_int(const std::string& value, int fallback, int minimum, int maximum) {
  try {
    std::size_t used = 0;
    const int parsed = std::stoi(value, &used);
    return used == value.size() ? std::clamp(parsed, minimum, maximum) : fallback;
  } catch (...) {
    return fallback;
  }
}

double parse_double(const std::string& value, double fallback, double minimum, double maximum) {
  try {
    std::size_t used = 0;
    const double parsed = std::stod(value, &used);
    return used == value.size() ? std::clamp(parsed, minimum, maximum) : fallback;
  } catch (...) {
    return fallback;
  }
}

std::string ratio_text(double ratio) {
  return std::to_string(static_cast<int>(ratio * 100.0 + 0.5)) + "%";
}

std::string compact_json(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

bool parse_json_object(const std::string& text, Json::Value& output) {
  Json::CharReaderBuilder builder;
  Json::Value value;
  std::string errors;
  std::istringstream input(text);
  if (!Json::parseFromStream(builder, input, &value, &errors) || !value.isObject()) return false;
  output = std::move(value);
  return true;
}

std::string optional_number_text(const std::optional<double>& value) {
  if (!value) return "Provider default";
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << *value;
  return output.str();
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

std::string reasoning_text(const std::string& value) {
  static const std::map<std::string, std::string> labels = {
      {"default", "Provider default"}, {"off", "Off"},       {"auto", "Auto"},
      {"minimal", "Minimal"},        {"low", "Low"},       {"medium", "Medium"},
      {"high", "High"},              {"xhigh", "XHigh"}};
  const auto found = labels.find(value);
  return found == labels.end() ? "Provider default" : found->second;
}

bool valid_provider_id(const std::string& id) {
  return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char ch) {
           return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
         });
}

void sync_default_model(Config& config) {
  const auto* provider = config.find_provider(config.default_provider);
  config.default_model = provider ? provider->default_model : std::string{};
}

bool config_dirty(const Config& original, const Config& working) {
  return config_to_json(original) != config_to_json(working);
}

std::string settings_path(std::initializer_list<std::string_view> levels = {}) {
  std::string path = "ask settings";
  for (const auto level : levels) {
    path += " ➔ ";
    path += level;
  }
  return path;
}

std::optional<int> select_menu(const std::string& title_text,
                               const std::vector<std::string>& options, int selected = 0,
                               const std::string& detail = {}) {
  if (options.empty()) return std::nullopt;
  selected = std::clamp(selected, 0, static_cast<int>(options.size()) - 1);
  for (;;) {
    heading(title_text);
    if (!detail.empty()) mvaddnstr(2, 2, detail.c_str(), std::max(0, COLS - 4));
    const int start_row = detail.empty() ? 3 : 4;
    const int rows = std::max(1, LINES - start_row - 3);
    const int first = first_visible(selected, static_cast<int>(options.size()), rows);
    for (int row = 0; row < rows && first + row < static_cast<int>(options.size()); ++row) {
      const int index = first + row;
      if (index == selected) attron(COLOR_PAIR(2));
      mvaddnstr(start_row + row, 2, ((index == selected ? "> " : "  ") + options[index]).c_str(),
                std::max(0, COLS - 4));
      if (index == selected) attroff(COLOR_PAIR(2));
    }
    footer("Up/Down navigate  Enter select  Esc back");
    refresh();
    const int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < static_cast<int>(options.size())) ++selected;
    else if (is_enter(key) || is_right(key)) return selected;
    else if (is_escape(key) || is_left(key)) return std::nullopt;
  }
}

enum class ExitChoice { continue_editing, discard, save };

ExitChoice exit_dialog() {
  const std::vector<std::string> options = {"Continue editing", "Discard changes", "Save changes"};
  auto choice = select_menu(settings_path({"Unsaved changes"}), options, 0,
                            "Choose what to do with the current settings draft.");
  if (!choice || *choice == 0) return ExitChoice::continue_editing;
  return *choice == 1 ? ExitChoice::discard : ExitChoice::save;
}

bool confirm_dialog(const std::string& title_text, const std::string& detail,
                    const std::string& action) {
  auto choice = select_menu(title_text, {"Cancel", action}, 0, detail);
  return choice && *choice == 1;
}

std::vector<std::pair<std::string, std::string>> model_choices(const Config& config) {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto& provider : config.providers) {
    if (!provider.enabled) continue;
    for (const auto& model : provider.models) result.emplace_back(provider.id, model);
    if (provider.models.empty() && !provider.default_model.empty()) {
      result.emplace_back(provider.id, provider.default_model);
    }
  }
  return result;
}

bool choose_default_model(Config& config) {
  const auto choices = model_choices(config);
  std::vector<std::string> labels;
  int selected = 0;
  for (std::size_t index = 0; index < choices.size(); ++index) {
    labels.push_back(choices[index].first + " / " + choices[index].second);
    if (choices[index].first == config.default_provider && choices[index].second == config.default_model) {
      selected = static_cast<int>(index);
    }
  }
  auto choice = select_menu(settings_path({"Default model"}), labels, selected,
                            "New conversations start with this provider and model.");
  if (!choice) return false;
  const auto& [provider, model] = choices[static_cast<std::size_t>(*choice)];
  const bool changed = provider != config.default_provider || model != config.default_model;
  config.default_provider = provider;
  config.default_model = model;
  if (auto* selected_provider = config.find_provider(provider)) {
    selected_provider->enabled = true;
    selected_provider->default_model = model;
  }
  return changed;
}

void normalize_models(Provider& provider, std::vector<std::string> models) {
  std::set<std::string> seen;
  std::vector<std::string> unique;
  for (auto& model : models) {
    if (!model.empty() && seen.insert(model).second) unique.push_back(std::move(model));
  }
  provider.models = std::move(unique);
  if (std::find(provider.models.begin(), provider.models.end(), provider.default_model) ==
      provider.models.end()) {
    provider.default_model = provider.models.empty() ? std::string{} : provider.models.front();
  }
}

void model_actions(Config& config, std::size_t provider_index, std::size_t model_index) {
  for (;;) {
    auto& provider = config.providers[provider_index];
    if (model_index >= provider.models.size()) return;
    const auto model = provider.models[model_index];
    const std::vector<std::string> actions = {
        model == provider.default_model ? "Default model" : "Set as provider default",
        "Rename model", "Remove model"};
    auto choice = select_menu(settings_path({"Providers", provider.name, "Models", model}),
                              actions, 0,
                              "Provider: " + provider.name);
    if (!choice) return;
    if (*choice == 0) {
      provider.default_model = model;
      if (config.default_provider == provider.id) config.default_model = model;
      return;
    }
    if (*choice == 1) {
      auto value = edit_text(
          settings_path({"Providers", provider.name, "Models", model, "Rename"}), model);
      if (!value || value->empty() || *value == model) continue;
      if (std::find(provider.models.begin(), provider.models.end(), *value) != provider.models.end()) {
        message_line("A model with that ID already exists", true);
        getch();
        continue;
      }
      provider.models[model_index] = *value;
      if (provider.default_model == model) provider.default_model = *value;
      if (config.default_provider == provider.id && config.default_model == model) {
        config.default_model = *value;
      }
      return;
    }
    if (*choice == 2 && confirm_dialog(
                           settings_path({"Providers", provider.name, "Models", model, "Remove"}),
                           "Remove " + model + " from " + provider.name + "?",
                                       "Remove model")) {
      provider.models.erase(provider.models.begin() + static_cast<std::ptrdiff_t>(model_index));
      if (provider.default_model == model) {
        provider.default_model = provider.models.empty() ? std::string{} : provider.models.front();
      }
      if (config.default_provider == provider.id) config.default_model = provider.default_model;
      return;
    }
  }
}

void models_page(Config& config, const Config& original, std::size_t provider_index,
                 ChatClient* client) {
  int selected = 0;
  std::string status;
  bool status_error = false;
  for (;;) {
    auto& provider = config.providers[provider_index];
    const int model_count = static_cast<int>(provider.models.size());
    const int add_index = model_count;
    const int count = model_count + 2;
    selected = std::clamp(selected, 0, count - 1);
    heading(settings_path({"Providers", provider.name, "Models"}),
            config_dirty(original, config));
    mvaddnstr(2, 2, ("Provider default: " +
                         (provider.default_model.empty() ? std::string("Not set") : provider.default_model))
                            .c_str(),
              std::max(0, COLS - 4));
    const int rows = std::max(1, LINES - 7);
    const int first = first_visible(selected, count, rows);
    for (int row = 0; row < rows && first + row < count; ++row) {
      const int index = first + row;
      if (index < model_count) {
        const auto& model = provider.models[static_cast<std::size_t>(index)];
        setting_row(4 + row, index == selected, model,
                    model == provider.default_model ? "Default" : "Configure", true);
      } else if (index == add_index) {
        setting_row(4 + row, index == selected, "Add model", "", true);
      } else {
        setting_row(4 + row, index == selected, "Discover models", "Fetch from provider", true);
      }
    }
    if (!status.empty()) message_line(status, status_error);
    footer("Up/Down navigate  Enter/Right open  Left/Esc back");
    refresh();
    status.clear();
    status_error = false;
    const int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < count) ++selected;
    else if (is_escape(key) || is_left(key)) return;
    else if (is_enter(key) || is_right(key)) {
      if (selected < model_count) {
        model_actions(config, provider_index, static_cast<std::size_t>(selected));
      } else if (selected == add_index) {
        auto value = edit_text(settings_path({"Providers", provider.name, "Models", "Add"}), {});
        if (value && !value->empty()) {
          if (std::find(provider.models.begin(), provider.models.end(), *value) == provider.models.end()) {
            provider.models.push_back(*value);
            if (provider.default_model.empty()) provider.default_model = *value;
            if (config.default_provider == provider.id) config.default_model = provider.default_model;
            selected = static_cast<int>(provider.models.size()) - 1;
            status = "Model added";
          } else {
            status = "Model already exists";
            status_error = true;
          }
        }
      } else if (!client) {
        status = "Model discovery is unavailable";
        status_error = true;
      } else {
        message_line("Discovering models from " + provider.base_url + " ...");
        refresh();
        try {
          auto discovered = client->fetch_models(provider);
          if (discovered.empty()) {
            status = "Provider returned no models; existing list kept";
            status_error = true;
          } else {
            normalize_models(provider, std::move(discovered));
            if (config.default_provider == provider.id) config.default_model = provider.default_model;
            selected = 0;
            status = "Model list updated";
          }
        } catch (const std::exception& error) {
          status = error.what();
          status_error = true;
        }
      }
    }
  }
}

enum class ProviderPageResult { back, deleted };

ProviderPageResult provider_page(Config& config, const Config& original, std::size_t index,
                                 ChatClient* client) {
  static constexpr std::array<std::string_view, 12> labels = {
      "Enabled", "Display name", "Provider ID", "Protocol", "API base URL", "API key environment",
      "Stored API key", "Models", "Context window", "Request timeout", "Extra headers", "Delete provider"};
  int selected = 0;
  std::string status;
  bool status_error = false;
  for (;;) {
    if (index >= config.providers.size()) return ProviderPageResult::back;
    auto& provider = config.providers[index];
    const auto provider_path = settings_path({"Providers", provider.name});
    heading(provider_path, config_dirty(original, config));
    mvaddnstr(2, 2, "Connection, authentication and model settings for this provider.",
              std::max(0, COLS - 4));
    const int count = static_cast<int>(labels.size());
    const int rows = std::max(1, LINES - 7);
    const int first = first_visible(selected, count, rows);
    for (int row = 0; row < rows && first + row < count; ++row) {
      const int field = first + row;
      std::string value;
      bool opens = false;
      bool danger = false;
      switch (field) {
        case 0: value = provider.enabled ? "On" : "Off"; break;
        case 1: value = provider.name; opens = true; break;
        case 2: value = provider.id; opens = true; break;
        case 3: value = provider.protocol; opens = true; break;
        case 4: value = provider.base_url; opens = true; break;
        case 5: value = provider.api_key_env.empty() ? "Not set" : provider.api_key_env; opens = true; break;
        case 6: value = provider.api_key.empty() ? "Not stored" : "Stored"; opens = true; break;
        case 7:
          value = std::to_string(provider.models.size()) + " configured / " +
                  (provider.default_model.empty() ? "no default" : provider.default_model);
          opens = true;
          break;
        case 8: value = std::to_string(provider.context_window) + " tokens"; opens = true; break;
        case 9: value = std::to_string(provider.timeout_seconds) + " seconds"; opens = true; break;
        case 10: value = headers_json(provider.headers); opens = true; break;
        case 11: value = "Permanent action"; opens = true; danger = true; break;
      }
      setting_row(4 + row, field == selected, std::string(labels[static_cast<std::size_t>(field)]),
                  value, opens, danger);
    }
    if (!status.empty()) message_line(status, status_error);
    footer("Up/Down navigate  Left/Right change  Enter open  Esc back");
    refresh();
    status.clear();
    status_error = false;
    const int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < count) ++selected;
    else if (is_escape(key)) return ProviderPageResult::back;
    else if ((is_left(key) || is_right(key) || is_enter(key)) && selected == 0) {
      if (provider.enabled && provider.id == config.default_provider) {
        status = "Choose another default model before disabling this provider";
        status_error = true;
      } else if (!provider.enabled && provider.base_url.empty()) {
        status = "Set an API base URL before enabling this provider";
        status_error = true;
      } else {
        provider.enabled = !provider.enabled;
      }
    } else if (is_left(key) || is_right(key)) {
      const int direction = is_right(key) ? 1 : -1;
      if (selected == 3) {
        const std::array<std::string, 3> protocols = {"openai", "anthropic", "gemini"};
        auto found = std::find(protocols.begin(), protocols.end(), provider.protocol);
        int position = found == protocols.end() ? 0 : static_cast<int>(std::distance(protocols.begin(), found));
        position = (position + direction + static_cast<int>(protocols.size())) % static_cast<int>(protocols.size());
        provider.protocol = protocols[static_cast<std::size_t>(position)];
      } else if (selected == 8) {
        provider.context_window = std::clamp(provider.context_window + direction * 1024, 1024, 10000000);
      } else if (selected == 9) {
        provider.timeout_seconds = std::clamp(provider.timeout_seconds + direction * 5, 1, 3600);
      }
    } else if (is_enter(key)) {
      std::optional<std::string> value;
      if (selected == 1) {
        value = edit_text(provider_path + " ➔ Display name", provider.name);
        if (value) provider.name = value->empty() ? provider.id : *value;
      } else if (selected == 2) {
        value = edit_text(provider_path + " ➔ Provider ID", provider.id);
        if (value && *value != provider.id) {
          if (!valid_provider_id(*value)) {
            status = "Use letters, numbers, '.', '_' or '-' for the provider ID";
            status_error = true;
          } else if (config.find_provider(*value)) {
            status = "Provider ID already exists";
            status_error = true;
          } else {
            const auto old = provider.id;
            provider.id = *value;
            if (config.default_provider == old) config.default_provider = provider.id;
          }
        }
      } else if (selected == 3) {
        const std::vector<std::string> protocols = {"openai", "anthropic", "gemini"};
        auto current = std::find(protocols.begin(), protocols.end(), provider.protocol);
        auto choice = select_menu(provider_path + " ➔ Protocol", protocols,
                                  current == protocols.end() ? 0 : static_cast<int>(std::distance(protocols.begin(), current)));
        if (choice) provider.protocol = protocols[static_cast<std::size_t>(*choice)];
      } else if (selected == 4) {
        value = edit_text(provider_path + " ➔ API base URL", provider.base_url);
        if (value) provider.base_url = *value;
      } else if (selected == 5) {
        value = edit_text(provider_path + " ➔ API key environment", provider.api_key_env);
        if (value) provider.api_key_env = *value;
      } else if (selected == 6) {
        if (provider.api_key.empty()) {
          value = edit_text(provider_path + " ➔ Stored API key ➔ Add", {}, true);
          if (value && !value->empty()) provider.api_key = *value;
        } else {
          auto action = select_menu(provider_path + " ➔ Stored API key",
                                    {"Keep current key", "Replace stored key",
                                     "Clear stored key"},
                                    0, "The current key is hidden.");
          if (action && *action == 1) {
            value = edit_text(provider_path + " ➔ Stored API key ➔ Replace", {}, true);
            if (value && !value->empty()) provider.api_key = *value;
          } else if (action && *action == 2) {
            provider.api_key.clear();
          }
        }
      } else if (selected == 7) {
        models_page(config, original, index, client);
      } else if (selected == 8) {
        value = edit_text(provider_path + " ➔ Context window",
                          std::to_string(provider.context_window));
        if (value) provider.context_window = parse_int(*value, provider.context_window, 1024, 10000000);
      } else if (selected == 9) {
        value = edit_text(provider_path + " ➔ Request timeout",
                          std::to_string(provider.timeout_seconds));
        if (value) provider.timeout_seconds = parse_int(*value, provider.timeout_seconds, 1, 3600);
      } else if (selected == 10) {
        value = edit_text(provider_path + " ➔ Extra headers", headers_json(provider.headers));
        if (value && !parse_headers(*value, provider.headers)) {
          status = "Headers must be a JSON object";
          status_error = true;
        }
      } else if (selected == 11) {
        if (config.providers.size() == 1) {
          status = "At least one provider is required";
          status_error = true;
        } else if (provider.id == config.default_provider) {
          status = "Choose another default model before deleting this provider";
          status_error = true;
        } else if (confirm_dialog(provider_path + " ➔ Delete",
                                  "Delete " + provider.name + " and all its models?",
                                  "Delete provider")) {
          config.providers.erase(config.providers.begin() + static_cast<std::ptrdiff_t>(index));
          return ProviderPageResult::deleted;
        }
      }
    }
  }
}

void providers_page(Config& config, const Config& original, ChatClient* client) {
  int selected = 0;
  for (;;) {
    const int provider_count = static_cast<int>(config.providers.size());
    const int count = provider_count + 1;
    selected = std::clamp(selected, 0, count - 1);
    heading(settings_path({"Providers"}), config_dirty(original, config));
    mvaddnstr(2, 2, "Configure API connections. The default model is selected on the General page.",
              std::max(0, COLS - 4));
    const int rows = std::max(1, LINES - 7);
    const int first = first_visible(selected, count, rows);
    for (int row = 0; row < rows && first + row < count; ++row) {
      const int index = first + row;
      if (index < provider_count) {
        const auto& provider = config.providers[static_cast<std::size_t>(index)];
        std::string value = provider.enabled ? "Enabled" : "Disabled";
        if (provider.id == config.default_provider) value += " / Default";
        setting_row(4 + row, index == selected, provider.name, value, true);
      } else {
        setting_row(4 + row, index == selected, "Add provider", "", true);
      }
    }
    footer("Up/Down navigate  Enter/Right open  Left/Esc back");
    refresh();
    const int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < count) ++selected;
    else if (is_escape(key) || is_left(key)) return;
    else if (is_enter(key) || is_right(key)) {
      if (selected < provider_count) {
        if (provider_page(config, original, static_cast<std::size_t>(selected), client) ==
            ProviderPageResult::deleted) {
          selected = std::min(selected, static_cast<int>(config.providers.size()));
        }
      } else {
        Provider provider;
        provider.id = "custom";
        int suffix = 2;
        while (config.find_provider(provider.id)) provider.id = "custom-" + std::to_string(suffix++);
        provider.name = "Custom provider";
        provider.protocol = "openai";
        provider.enabled = false;
        config.providers.push_back(std::move(provider));
        selected = static_cast<int>(config.providers.size()) - 1;
        const auto initial = config_to_json(config);
        const auto result = provider_page(config, original, static_cast<std::size_t>(selected), client);
        if (result == ProviderPageResult::back && config_to_json(config) == initial) {
          config.providers.erase(config.providers.begin() + selected);
          selected = static_cast<int>(config.providers.size());
        }
      }
    }
  }
}

void ai_call_page(Config& config, const Config& original) {
  static constexpr std::array<std::string_view, 7> labels = {
      "Thinking strength", "Thinking budget", "Temperature", "Top P",
      "Maximum output tokens", "Stream output", "Advanced request JSON"};
  static constexpr std::array<std::string_view, 8> reasoning_values = {
      "default", "off", "auto", "minimal", "low", "medium", "high", "xhigh"};
  int selected = 0;
  std::string status;
  bool status_error = false;
  for (;;) {
    heading(settings_path({"AI call"}), config_dirty(original, config));
    mvaddnstr(2, 2, "Generation and model reasoning controls.", std::max(0, COLS - 4));
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
      std::string value;
      bool opens = false;
      switch (index) {
        case 0: value = reasoning_text(config.settings.reasoning_effort); opens = true; break;
        case 1:
          value = config.settings.thinking_budget_tokens == 0
                      ? "Automatic"
                      : std::to_string(config.settings.thinking_budget_tokens) + " tokens";
          opens = true;
          break;
        case 2: value = optional_number_text(config.settings.temperature); opens = true; break;
        case 3: value = optional_number_text(config.settings.top_p); opens = true; break;
        case 4: value = std::to_string(config.settings.max_output_tokens) + " tokens"; opens = true; break;
        case 5: value = config.settings.stream_output ? "On" : "Off"; break;
        case 6:
          value = config.settings.custom_parameters.empty()
                      ? "Empty object"
                      : clipped(compact_json(config.settings.custom_parameters), 32);
          opens = true;
          break;
      }
      setting_row(4 + index, index == selected,
                  std::string(labels[static_cast<std::size_t>(index)]), value, opens);
    }
    mvaddnstr(13, 2,
              "Provider default omits a parameter; advanced JSON cannot replace request structure.",
              std::max(0, COLS - 4));
    if (!status.empty()) message_line(status, status_error);
    footer("Up/Down navigate  Left/Right change  Enter open/edit  Esc back");
    refresh();
    status.clear();
    status_error = false;
    const int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < static_cast<int>(labels.size())) ++selected;
    else if (is_escape(key)) return;
    else if (is_left(key) || is_right(key)) {
      const int direction = is_right(key) ? 1 : -1;
      if (selected == 0) {
        auto current = std::find(reasoning_values.begin(), reasoning_values.end(),
                                 config.settings.reasoning_effort);
        int position = current == reasoning_values.end()
                           ? 0
                           : static_cast<int>(std::distance(reasoning_values.begin(), current));
        position = (position + direction + static_cast<int>(reasoning_values.size())) %
                   static_cast<int>(reasoning_values.size());
        config.settings.reasoning_effort = reasoning_values[static_cast<std::size_t>(position)];
      } else if (selected == 1) {
        config.settings.thinking_budget_tokens = std::clamp(
            config.settings.thinking_budget_tokens + direction * 256, 0, 1000000);
      } else if (selected == 2) {
        if (!config.settings.temperature) {
          if (direction > 0) config.settings.temperature = 0.0;
        } else {
          const double next = *config.settings.temperature + direction * 0.05;
          if (next < 0.0) config.settings.temperature.reset();
          else config.settings.temperature = std::clamp(next, 0.0, 1.0);
        }
      } else if (selected == 3) {
        if (!config.settings.top_p) {
          if (direction > 0) config.settings.top_p = 0.0;
        } else {
          const double next = *config.settings.top_p + direction * 0.05;
          if (next < 0.0) config.settings.top_p.reset();
          else config.settings.top_p = std::clamp(next, 0.0, 1.0);
        }
      } else if (selected == 4) {
        config.settings.max_output_tokens = std::clamp(
            config.settings.max_output_tokens + direction * 256, 256, 1000000);
      } else if (selected == 5) {
        config.settings.stream_output = !config.settings.stream_output;
      }
    } else if (is_enter(key)) {
      std::optional<std::string> value;
      if (selected == 0) {
        std::vector<std::string> options;
        int current = 0;
        for (int index = 0; index < static_cast<int>(reasoning_values.size()); ++index) {
          options.push_back(reasoning_text(std::string(reasoning_values[static_cast<std::size_t>(index)])));
          if (reasoning_values[static_cast<std::size_t>(index)] == config.settings.reasoning_effort) {
            current = index;
          }
        }
        auto choice = select_menu(settings_path({"AI call", "Thinking strength"}),
                                  options, current,
                                  "Provider support varies; default sends no reasoning override.");
        if (choice) config.settings.reasoning_effort =
            reasoning_values[static_cast<std::size_t>(*choice)];
      } else if (selected == 1) {
        value = edit_text(settings_path({"AI call", "Thinking budget"}),
                          std::to_string(config.settings.thinking_budget_tokens));
        if (value) config.settings.thinking_budget_tokens =
            parse_int(*value, config.settings.thinking_budget_tokens, 0, 1000000);
      } else if (selected == 2 || selected == 3) {
        auto& target = selected == 2 ? config.settings.temperature : config.settings.top_p;
        const auto field = selected == 2 ? "Temperature" : "Top P";
        auto action = select_menu(settings_path({"AI call", field}),
                                  {"Provider default", "Set explicit value"},
                                  target ? 1 : 0, "Explicit values are limited to 0.0–1.0.");
        if (action && *action == 0) {
          target.reset();
        } else if (action && *action == 1) {
          std::ostringstream initial;
          initial << (target ? *target : 0.7);
          value = edit_text(settings_path({"AI call", field, "Value"}), initial.str());
          if (value) {
            const double fallback = target.value_or(0.7);
            target = parse_double(*value, fallback, 0.0, 1.0);
          }
        }
      } else if (selected == 4) {
        value = edit_text(settings_path({"AI call", "Maximum output tokens"}),
                          std::to_string(config.settings.max_output_tokens));
        if (value) config.settings.max_output_tokens =
            parse_int(*value, config.settings.max_output_tokens, 256, 1000000);
      } else if (selected == 5) {
        config.settings.stream_output = !config.settings.stream_output;
      } else if (selected == 6) {
        value = edit_text(settings_path({"AI call", "Advanced request JSON"}),
                          compact_json(config.settings.custom_parameters));
        if (value) {
          Json::Value parsed;
          if (parse_json_object(*value, parsed)) {
            config.settings.custom_parameters = std::move(parsed);
          } else {
            status = "Advanced request parameters must be a valid JSON object";
            status_error = true;
          }
        }
      }
    }
  }
}

enum class RootAction { save, cancel };

RootAction general_page(Config& config, const Config& original, ChatClient* client) {
  static constexpr std::array<std::string_view, 9> labels = {
      "Default model", "Save conversations", "Auto compact ratio", "Maximum tool rounds",
      "System prompt", "AI call", "Providers", "Cancel", "Save changes"};
  int selected = 0;
  for (;;) {
    const bool dirty = config_dirty(original, config);
    heading(settings_path(), dirty);
    mvaddnstr(2, 2, "General", std::max(0, COLS - 4));
    mvaddnstr(9, 2, "Advanced", std::max(0, COLS - 4));
    mvaddnstr(12, 2, "Connections", std::max(0, COLS - 4));
    static constexpr std::array<int, 9> rows = {3, 4, 5, 6, 7, 10, 13, 16, 17};
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
      std::string value;
      bool opens = false;
      switch (index) {
        case 0: value = config.default_provider + " / " + config.default_model; opens = true; break;
        case 1: value = config.settings.save_sessions ? "On" : "Off"; break;
        case 2: value = ratio_text(config.settings.auto_compact_ratio); break;
        case 3: value = std::to_string(config.settings.max_tool_rounds); opens = true; break;
        case 4: value = clipped(config.settings.system_prompt, 34); opens = true; break;
        case 5:
          value = reasoning_text(config.settings.reasoning_effort) + " / " +
                  std::to_string(config.settings.max_output_tokens) + " tokens";
          opens = true;
          break;
        case 6:
          value = std::to_string(config.providers.size()) + " configured / " +
                  std::to_string(std::count_if(config.providers.begin(), config.providers.end(),
                                               [](const Provider& provider) { return provider.enabled; })) +
                  " enabled";
          opens = true;
          break;
        case 7: value = dirty ? "Discard draft" : "Exit settings"; opens = true; break;
        case 8: value = dirty ? "Write config and exit" : "No changes"; opens = true; break;
      }
      setting_row(rows[static_cast<std::size_t>(index)], index == selected,
                  std::string(labels[static_cast<std::size_t>(index)]), value, opens);
    }
    footer("Up/Down navigate  Left/Right change  Enter open/select  Esc exit");
    refresh();
    const int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < static_cast<int>(labels.size())) ++selected;
    else if (is_escape(key)) return RootAction::cancel;
    else if ((is_left(key) || is_right(key) || is_enter(key)) && selected == 1) {
      config.settings.save_sessions = !config.settings.save_sessions;
    } else if ((is_left(key) || is_right(key)) && selected == 2) {
      const double delta = is_right(key) ? 0.05 : -0.05;
      config.settings.auto_compact_ratio =
          std::clamp(config.settings.auto_compact_ratio + delta, 0.20, 0.90);
    } else if ((is_left(key) || is_right(key)) && selected == 3) {
      const int direction = is_right(key) ? 1 : -1;
      config.settings.max_tool_rounds = std::clamp(config.settings.max_tool_rounds + direction, 1, 50);
    } else if (is_enter(key) || is_right(key)) {
      std::optional<std::string> value;
      if (selected == 0) {
        choose_default_model(config);
      } else if (selected == 2) {
        value = edit_text(settings_path({"Auto compact ratio"}),
                          std::to_string(config.settings.auto_compact_ratio));
        if (value) config.settings.auto_compact_ratio =
            parse_double(*value, config.settings.auto_compact_ratio, 0.20, 0.90);
      } else if (selected == 3) {
        value = edit_text(settings_path({"Maximum tool rounds"}),
                          std::to_string(config.settings.max_tool_rounds));
        if (value) config.settings.max_tool_rounds =
            parse_int(*value, config.settings.max_tool_rounds, 1, 50);
      } else if (selected == 4) {
        value = edit_text(settings_path({"System prompt"}), config.settings.system_prompt);
        if (value) config.settings.system_prompt = *value;
      } else if (selected == 5) {
        ai_call_page(config, original);
      } else if (selected == 6) {
        providers_page(config, original, client);
      } else if (selected == 7) {
        return RootAction::cancel;
      } else if (selected == 8) {
        return RootAction::save;
      }
    }
  }
}

std::string date_text(std::int64_t seconds) {
  std::time_t value = static_cast<std::time_t>(seconds);
  std::tm local{};
  localtime_r(&value, &local);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
  return buffer;
}

}  // namespace

bool Tui::configure(ConfigStore& store, ChatClient* client) {
  Screen screen;
  const Config original = store.load();
  Config working = original;
  for (;;) {
    const auto action = general_page(working, original, client);
    if (action == RootAction::save) {
      try {
        sync_default_model(working);
        store.save(working);
        return true;
      } catch (const std::exception& error) {
        heading("Unable to save settings", true);
        mvaddnstr(3, 2, error.what(), std::max(0, COLS - 4));
        footer("Press any key to return");
        refresh();
        getch();
      }
    } else if (!config_dirty(original, working)) {
      return false;
    } else {
      switch (exit_dialog()) {
        case ExitChoice::continue_editing: break;
        case ExitChoice::discard: return false;
        case ExitChoice::save:
          try {
            sync_default_model(working);
            store.save(working);
            return true;
          } catch (const std::exception& error) {
            heading("Unable to save settings", true);
            mvaddnstr(3, 2, error.what(), std::max(0, COLS - 4));
            footer("Press any key to return");
            refresh();
            getch();
          }
          break;
      }
    }
  }
}

std::optional<std::string> Tui::choose_session(SessionStore& store) {
  Screen screen;
  auto sessions = store.list(200);
  if (sessions.empty()) {
    heading("resume conversation");
    mvaddstr(3, 2, "No saved conversations.");
    footer("press any key to return");
    refresh();
    getch();
    return std::nullopt;
  }
  int selected = 0;
  for (;;) {
    heading("resume conversation");
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
    footer("Up/Down navigate  Enter resume  Esc cancel");
    refresh();
    int key = getch();
    if (is_up(key) && selected > 0) --selected;
    else if (is_down(key) && selected + 1 < static_cast<int>(sessions.size())) ++selected;
    else if (is_enter(key)) return sessions[static_cast<std::size_t>(selected)].id;
    else if (is_escape(key)) return std::nullopt;
  }
}

bool Tui::choose_model(const Config& config, std::string& provider, std::string& model) {
  const auto choices = model_choices(config);
  if (choices.empty()) return false;
  std::vector<std::string> labels;
  int selected = 0;
  for (std::size_t index = 0; index < choices.size(); ++index) {
    labels.push_back(choices[index].first + " / " + choices[index].second);
    if (choices[index].first == provider && choices[index].second == model) selected = static_cast<int>(index);
  }
  Screen screen;
  auto choice = select_menu("select provider and model", labels, selected);
  if (!choice) return false;
  provider = choices[static_cast<std::size_t>(*choice)].first;
  model = choices[static_cast<std::size_t>(*choice)].second;
  return true;
}

}  // namespace ask
