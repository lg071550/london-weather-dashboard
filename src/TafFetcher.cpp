#include "TafFetcher.h"
#include "Utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <ctime>

namespace {
    std::string joinStations(const std::vector<std::string>& stations) {
        std::string ids;
        for (size_t i = 0; i < stations.size(); ++i) {
            if (i > 0) ids += ',';
            ids += stations[i];
        }
        return ids;
    }

    std::string formatZuluTime(long long unix_seconds) {
        std::time_t t = static_cast<std::time_t>(unix_seconds);
        std::tm gm{};
        gmtime_s(&gm, &t);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d%02dZ", gm.tm_hour, gm.tm_min);
        return std::string(buf);
    }

    std::string visibilityString(const nlohmann::json& value) {
        if (value.is_string()) {
            return value.get<std::string>();
        }
        if (value.is_number()) {
            return utils::formatFloat(value.get<float>(), 1) + "SM";
        }
        return "";
    }

    int lowestCeilingFeet(const nlohmann::json& clouds) {
        if (!clouds.is_array()) return -1;
        int lowest = -1;
        for (const auto& cloud : clouds) {
            std::string cover = cloud.value("cover", "");
            if ((cover == "BKN" || cover == "OVC") && cloud.contains("base") && !cloud["base"].is_null()) {
                int base = cloud["base"].get<int>();
                if (lowest < 0 || base < lowest) lowest = base;
            }
        }
        return lowest;
    }
}

std::vector<TafForecast> TafFetcher::fetch(HttpClient& http, const std::vector<std::string>& stations) {
    std::vector<TafForecast> forecasts;
    std::string url = "https://aviationweather.gov/api/data/taf?ids=" + joinStations(stations) + "&format=json";
    spdlog::debug("TafFetcher GET {}", url);

    std::string body = http.get(url);
    auto json = nlohmann::json::parse(body);

    if (!json.is_array()) {
        spdlog::warn("TAF response is not a JSON array");
        return forecasts;
    }

    for (const auto& taf : json) {
        try {
            TafForecast forecast;
            forecast.station_id = taf.value("icaoId", "");
            if (forecast.station_id.empty()) {
                forecast.station_id = taf.value("name", "");
            }
            forecast.issue_time = taf.value("issueTime", "");
            forecast.raw_text = taf.value("rawTAF", "");
            forecast.age_minutes = utils::computeAgeMinutes(forecast.issue_time);

            if (taf.contains("fcsts") && taf["fcsts"].is_array()) {
                for (const auto& fcst : taf["fcsts"]) {
                    std::vector<std::string> hazards;

                    int ceiling = lowestCeilingFeet(fcst.value("clouds", nlohmann::json::array()));
                    if (ceiling > 0 && ceiling <= 1500) {
                        hazards.push_back("ceiling " + std::to_string(ceiling) + "ft");
                    }

                    if (fcst.contains("visib") && !fcst["visib"].is_null()) {
                        std::string vis = visibilityString(fcst["visib"]);
                        bool low_vis = !fcst["visib"].is_string() && fcst["visib"].get<float>() < 5.0f;
                        if (!vis.empty() && (low_vis || vis != "6+")) {
                            hazards.push_back("vis " + vis);
                        }
                    }

                    int gust = fcst.value("wgst", 0);
                    if (gust >= 25) {
                        hazards.push_back("gust " + std::to_string(gust) + "kt");
                    }

                    std::string wx = fcst.value("wxString", "");
                    if (!wx.empty()) {
                        hazards.push_back(wx);
                    }

                    if (hazards.empty()) {
                        continue;
                    }

                    std::string label;
                    if (fcst.contains("probability") && !fcst["probability"].is_null()) {
                        label = "PROB" + std::to_string(fcst["probability"].get<int>());
                    }
                    std::string change = fcst.value("fcstChange", "");
                    if (!change.empty()) {
                        if (!label.empty()) label += " ";
                        label += change;
                    }
                    if (label.empty()) {
                        label = "BASE";
                    }

                    std::ostringstream alert;
                    alert << label << " "
                          << formatZuluTime(fcst.value("timeFrom", 0LL))
                          << "-"
                          << formatZuluTime(fcst.value("timeTo", 0LL))
                          << " ";

                    for (size_t i = 0; i < hazards.size(); ++i) {
                        if (i > 0) alert << ", ";
                        alert << hazards[i];
                    }

                    forecast.alerts.push_back(alert.str());
                }
            }

            forecasts.push_back(std::move(forecast));
        }
        catch (const std::exception& e) {
            spdlog::warn("Failed to parse TAF entry: {}", e.what());
        }
    }

    spdlog::info("TafFetcher: parsed {} forecasts", forecasts.size());
    return forecasts;
}