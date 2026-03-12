#include "MetarFetcher.h"
#include "Utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

std::vector<MetarReport> MetarFetcher::fetch(HttpClient& http,
                                              const std::vector<std::string>& stations) {
    
    std::string ids;
    for (size_t i = 0; i < stations.size(); ++i) {
        if (i > 0) ids += ',';
        ids += stations[i];
    }

    std::string url = "https://aviationweather.gov/api/data/metar?ids=" + ids + "&format=json";
    spdlog::debug("MetarFetcher GET {}", url);

    std::string body = http.get(url);
    auto json = nlohmann::json::parse(body);

    std::vector<MetarReport> reports;

    if (!json.is_array()) {
        spdlog::warn("METAR response is not a JSON array");
        return reports;
    }

    for (const auto& m : json) {
        try {
            MetarReport r;
            r.station_id     = m.value("icaoId", "");
            r.raw_text       = m.value("rawOb", "");
            r.obs_time       = m.value("reportTime", "");
            r.is_speci       = m.value("metarType", "") == "SPECI" || r.raw_text.rfind("SPECI", 0) == 0;
            r.temp_c         = m.value("temp", 0.0f);
            r.dewpoint_c     = m.value("dewp", 0.0f);

            
            if (m.contains("wdir")) {
                if (m["wdir"].is_string()) {
                    r.wind_dir_deg = m["wdir"].get<std::string>();
                } else {
                    r.wind_dir_deg = std::to_string(m["wdir"].get<int>());
                }
            }

            r.wind_speed_kt  = m.value("wspd", 0);

            
            if (m.contains("visib")) {
                if (m["visib"].is_string()) {
                    r.visibility = m["visib"].get<std::string>();
                } else {
                    r.visibility = utils::formatFloat(m["visib"].get<float>(), 0);
                }
            }

            
            if (m.contains("clouds") && m["clouds"].is_array()) {
                std::string sky;
                for (const auto& cloud : m["clouds"]) {
                    if (!sky.empty()) sky += " ";
                    std::string cover = cloud.value("cover", "");
                    sky += cover;
                    if (cloud.contains("base") && !cloud["base"].is_null()) {
                        int base = cloud["base"].get<int>();
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "%03d", base);
                        sky += buf;
                    }
                }
                r.sky_condition = sky;
            }

            
            if (m.contains("wxString") && !m["wxString"].is_null()) {
                r.wx_string = m["wxString"].get<std::string>();
            }

            r.age_minutes = utils::computeAgeMinutes(r.obs_time);
            reports.push_back(std::move(r));
        }
        catch (const std::exception& e) {
            spdlog::warn("Failed to parse METAR entry: {}", e.what());
        }
    }

    spdlog::info("MetarFetcher: parsed {} reports", reports.size());
    return reports;
}
