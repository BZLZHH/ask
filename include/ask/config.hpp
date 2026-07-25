#pragma once

#include <filesystem>
#include <string>

#include "ask/types.hpp"

namespace ask {

class ConfigStore {
 public:
  explicit ConfigStore(std::filesystem::path path = {});

  Config load() const;
  void save(const Config& config) const;
  const std::filesystem::path& path() const { return path_; }

  static Config defaults();
  static std::filesystem::path default_path();
  static std::filesystem::path data_dir();
  static std::string api_key_for(const Provider& provider);

 private:
  std::filesystem::path path_;
};

Json::Value config_to_json(const Config& config);
Config config_from_json(const Json::Value& root);
ModelCapabilities capabilities_for_model(const Provider& provider, const std::string& model);
Json::Value capabilities_to_json(const ModelCapabilities& capabilities);
ModelCapabilities capabilities_from_json(const Json::Value& value,
                                         const ModelCapabilities& fallback);

}  // namespace ask
