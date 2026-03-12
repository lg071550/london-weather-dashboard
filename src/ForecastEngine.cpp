#include "ForecastEngine.h"
#include "Utils.h"
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>

std::vector<EnsembleForecast> ForecastEngine::compute(
    const std::vector<NwpModelForecast>& models,
    const std::vector<PwsObservation>& pwsObservations) {

    const std::string current_date = utils::currentUtcDateString();
    float wunderground_high = 0.0f;
    bool has_wunderground_high = false;

    for (const auto& obs : pwsObservations) {
        if (!obs.has_temp_high) continue;
        if (!has_wunderground_high || obs.temp_high_c > wunderground_high) {
            wunderground_high = obs.temp_high_c;
            has_wunderground_high = true;
        }
    }

    
    std::set<std::string> all_dates;
    for (const auto& m : models) {
        for (const auto& [date, _] : m.tmax_by_date) {
            all_dates.insert(date);
        }
    }

    std::vector<EnsembleForecast> results;

    for (const auto& date : all_dates) {
        EnsembleForecast ens;
        ens.date = date;
        if (date == current_date && has_wunderground_high) {
            ens.wunderground_high_c = wunderground_high;
            ens.has_wunderground_high = true;
        }

        
        std::vector<float> values;
        for (const auto& m : models) {
            auto it = m.tmax_by_date.find(date);
            if (it != m.tmax_by_date.end()) {
                values.push_back(it->second);
                ens.model_tmax[m.model] = it->second;
            }
        }

        ens.model_count = static_cast<int>(values.size());
        if (ens.model_count == 0) continue;

        
        float sum = std::accumulate(values.begin(), values.end(), 0.0f);
        ens.weighted_tmax = sum / static_cast<float>(ens.model_count);

        
        float vmin = *std::min_element(values.begin(), values.end());
        float vmax = *std::max_element(values.begin(), values.end());
        ens.spread = vmax - vmin;

        
        if (ens.model_count > 1) {
            float mean = ens.weighted_tmax;
            float sq_sum = 0.0f;
            for (float v : values) {
                float diff = v - mean;
                sq_sum += diff * diff;
            }
            float variance = sq_sum / static_cast<float>(ens.model_count);
            ens.sigma = std::sqrt(variance) * 2.5f;
        } else {
            ens.sigma = 0.0f;
        }

        
        ens.models_agree = (ens.spread < 1.5f);

        results.push_back(std::move(ens));
    }

    
    std::sort(results.begin(), results.end(),
              [](const EnsembleForecast& a, const EnsembleForecast& b) {
                  return a.date < b.date;
              });

    return results;
}
