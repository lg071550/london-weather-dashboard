#include "NwpFetcher.h"
#include "Utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstdio>

namespace {
    struct ModelDef {
        std::string label;   
        std::string slug;    
    };

    const std::vector<ModelDef> MODELS = {
        {"ecmwf", "ecmwf_ifs025"},
        {"gfs",   "gfs_seamless"},
        {"icon",  "icon_seamless"},
        {"ukv",   "ukmo_uk_deterministic_2km"}
    };
}

std::vector<NwpModelForecast> NwpFetcher::fetch(HttpClient& http, double lat, double lon) {
    std::vector<NwpModelForecast> results;

    for (const auto& model : MODELS) {
        try {
            char url[512];
            std::snprintf(url, sizeof(url),
                "https://api.open-meteo.com/v1/forecast?"
                "latitude=%.4f&longitude=%.4f"
                "&daily=temperature_2m_max,temperature_2m_min"
                "&timezone=Europe%%2FLondon"
                "&forecast_days=3"
                "&models=%s",
                lat, lon, model.slug.c_str());

            spdlog::debug("NwpFetcher GET {} ({})", url, model.label);
            std::string body = http.get(url);
            auto json = nlohmann::json::parse(body);

            NwpModelForecast forecast;
            forecast.model = model.label;

            
            if (json.contains("generationtime_ms")) {
                forecast.run_time = utils::currentUtcTimeString();
            }

            if (json.contains("daily")) {
                auto& daily = json["daily"];
                auto& times = daily["time"];
                auto& tmax  = daily["temperature_2m_max"];
                auto& tmin  = daily["temperature_2m_min"];

                for (size_t i = 0; i < times.size(); ++i) {
                    std::string date = times[i].get<std::string>();
                    if (i < tmax.size() && !tmax[i].is_null()) {
                        forecast.tmax_by_date[date] = tmax[i].get<float>();
                    }
                    if (i < tmin.size() && !tmin[i].is_null()) {
                        forecast.tmin_by_date[date] = tmin[i].get<float>();
                    }
                }
            }

            forecast.age_minutes = 0; 
            if (!forecast.tmax_by_date.empty()) {
                results.push_back(std::move(forecast));
                spdlog::info("NwpFetcher: {} returned {} days", model.label,
                             results.back().tmax_by_date.size());
            } else {
                spdlog::warn("NwpFetcher: {} returned no data", model.label);
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("NwpFetcher: {} fetch failed: {}", model.label, e.what());
        }
    }

    return results;
}
