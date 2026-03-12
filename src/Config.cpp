#include "Config.h"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <filesystem>

Config::Config(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Config file '{}' not found, using defaults", path);
        metar_stations_ = {"EGLC", "EGLL", "EGKB", "EGWU"};
        return;
    }

    try {
        YAML::Node root = YAML::LoadFile(path);

        if (root["wunderground_api_key"]) {
            wu_api_key_ = root["wunderground_api_key"].as<std::string>("");
        }

        if (root["metar_stations"]) {
            for (const auto& node : root["metar_stations"]) {
                metar_stations_.push_back(node.as<std::string>());
            }
        }
        if (metar_stations_.empty()) {
            metar_stations_ = {"EGLC", "EGLL", "EGKB", "EGWU"};
        }

        if (root["refresh_intervals_seconds"]) {
            auto ri = root["refresh_intervals_seconds"];
            if (ri["nwp"])          nwp_refresh_    = ri["nwp"].as<int>(300);
            if (ri["metar"])        metar_refresh_  = ri["metar"].as<int>(60);
            if (ri["wunderground"]) wu_refresh_     = ri["wunderground"].as<int>(180);
            if (ri["hourly"])       hourly_refresh_ = ri["hourly"].as<int>(300);
        }

        if (root["london"]) {
            auto london = root["london"];
            if (london["lat"]) lat_ = london["lat"].as<double>(51.5074);
            if (london["lon"]) lon_ = london["lon"].as<double>(-0.1278);
        }

        spdlog::info("Config loaded from '{}'", path);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to parse config '{}': {}", path, e.what());
        metar_stations_ = {"EGLC", "EGLL", "EGKB", "EGWU"};
    }
}
