#include "WundergroundFetcher.h"
#include "Utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstdio>

std::vector<PwsObservation> WundergroundFetcher::fetch(HttpClient& http,
                                                        double lat, double lon,
                                                        const std::string& apiKey) {
    std::vector<PwsObservation> results;

    if (apiKey.empty()) {
        spdlog::warn("WundergroundFetcher: no API key configured, skipping");
        return results;
    }

    char url[512];
    std::snprintf(url, sizeof(url),
        "https://api.weather.com/v2/pws/observations/nearby?"
        "geocode=%.4f,%.4f&radius=10&units=m&format=json&apiKey=%s",
        lat, lon, apiKey.c_str());

    spdlog::debug("WundergroundFetcher GET {}", url);
    std::string body = http.get(url);
    auto json = nlohmann::json::parse(body);

    if (!json.contains("observations") || !json["observations"].is_array()) {
        spdlog::warn("WundergroundFetcher: no observations array in response");
        return results;
    }

    for (const auto& obs : json["observations"]) {
        try {
            PwsObservation pws;
            pws.station_id = obs.value("stationID", "");
            pws.obs_time   = obs.value("obsTimeLocal", "");

            
            if (obs.contains("metric") && obs["metric"].is_object()) {
                const auto& met = obs["metric"];
                pws.temp_c          = met.value("temp", 0.0f);
                pws.has_temp_high   = met.contains("tempHigh") && !met["tempHigh"].is_null();
                if (pws.has_temp_high) {
                    pws.temp_high_c = met["tempHigh"].get<float>();
                }
                pws.humidity        = static_cast<float>(obs.value("humidity", 0));
                pws.wind_speed_kmh  = met.value("windSpeed", 0.0f);
                pws.precip_total_mm = met.value("precipTotal", 0.0f);
            }

            pws.age_minutes = utils::computeAgeMinutes(pws.obs_time);
            results.push_back(std::move(pws));
        }
        catch (const std::exception& e) {
            spdlog::warn("WundergroundFetcher: failed to parse PWS entry: {}", e.what());
        }
    }

    
    if (results.size() > 8) {
        results.resize(8);
    }

    spdlog::info("WundergroundFetcher: parsed {} PWS observations", results.size());
    return results;
}
