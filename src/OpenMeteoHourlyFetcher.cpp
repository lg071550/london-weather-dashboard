#include "OpenMeteoHourlyFetcher.h"
#include "Utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstdio>

std::vector<HourlyObs> OpenMeteoHourlyFetcher::fetch(HttpClient& http,
                                                      double lat, double lon) {
    char url[512];
    std::snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%.4f&longitude=%.4f"
        "&hourly=temperature_2m,precipitation,windspeed_10m,cloudcover"
        "&timezone=Europe%%2FLondon"
        "&forecast_days=3",
        lat, lon);

    spdlog::debug("OpenMeteoHourlyFetcher GET {}", url);
    std::string body = http.get(url);
    auto json = nlohmann::json::parse(body);

    std::vector<HourlyObs> results;

    if (!json.contains("hourly")) {
        spdlog::warn("OpenMeteoHourlyFetcher: no hourly object in response");
        return results;
    }

    const auto& hourly = json["hourly"];
    const auto& times  = hourly["time"];
    const auto& temps  = hourly["temperature_2m"];

    
    const auto& precip = hourly.contains("precipitation")
                             ? hourly["precipitation"]
                             : nlohmann::json::array();

    
    nlohmann::json wind_arr = nlohmann::json::array();
    if (hourly.contains("windspeed_10m"))      wind_arr = hourly["windspeed_10m"];
    else if (hourly.contains("wind_speed_10m")) wind_arr = hourly["wind_speed_10m"];

    
    nlohmann::json cloud_arr = nlohmann::json::array();
    if (hourly.contains("cloudcover"))    cloud_arr = hourly["cloudcover"];
    else if (hourly.contains("cloud_cover")) cloud_arr = hourly["cloud_cover"];

    for (size_t i = 0; i < times.size(); ++i) {
        try {
            HourlyObs obs;

            
            std::string time_str = times[i].get<std::string>();
            if (time_str.size() >= 16) {
                obs.hour_utc = time_str.substr(11, 5);
            } else {
                obs.hour_utc = time_str;
            }

            obs.temp_c    = (i < temps.size() && !temps[i].is_null())
                                ? temps[i].get<float>() : 0.0f;
            obs.precip_mm = (i < precip.size() && !precip[i].is_null())
                                ? precip[i].get<float>() : 0.0f;
            obs.wind_kmh  = (i < wind_arr.size() && !wind_arr[i].is_null())
                                ? wind_arr[i].get<float>() : 0.0f;
            obs.cloud_pct = (i < cloud_arr.size() && !cloud_arr[i].is_null())
                                ? cloud_arr[i].get<int>() : 0;

            results.push_back(std::move(obs));
        }
        catch (const std::exception& e) {
            spdlog::warn("OpenMeteoHourlyFetcher: failed to parse hour {}: {}", i, e.what());
        }
    }

    spdlog::info("OpenMeteoHourlyFetcher: parsed {} hourly entries", results.size());
    return results;
}
