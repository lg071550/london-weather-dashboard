#include "DataAggregator.h"
#include "Utils.h"
#include <spdlog/spdlog.h>

namespace {
    int annotateEnsembleChanges(const std::vector<EnsembleForecast>& previous,
                                std::vector<EnsembleForecast>& current) {
        int changed_days = 0;

        for (auto& current_day : current) {
            auto it = std::find_if(previous.begin(), previous.end(), [&](const EnsembleForecast& previous_day) {
                return previous_day.date == current_day.date;
            });

            if (it == previous.end()) {
                continue;
            }

            current_day.has_previous = true;
            current_day.delta_from_previous = current_day.weighted_tmax - it->weighted_tmax;
            current_day.changed_since_previous = std::abs(current_day.delta_from_previous) >= 0.1f;

            if (current_day.changed_since_previous) {
                ++changed_days;
            }
        }

        return changed_days;
    }
}

DataAggregator::DataAggregator(std::shared_ptr<Config> config)
    : config_(std::move(config)) {
    auto stations = config_->metarStations();
    primary_station_id_ = stations.empty() ? "EGLC" : stations.front();
}

DataAggregator::~DataAggregator() {
    stop();
}

void DataAggregator::start() {
    if (running_.exchange(true)) return; 

    threads_.emplace_back(&DataAggregator::metarLoop, this);
    threads_.emplace_back(&DataAggregator::nwpLoop, this);
    threads_.emplace_back(&DataAggregator::wundergroundLoop, this);
    threads_.emplace_back(&DataAggregator::hourlyLoop, this);

    spdlog::info("DataAggregator: started 4 fetcher threads");
}

void DataAggregator::stop() {
    if (!running_.exchange(false)) return; 

    cv_.notify_all(); 

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    spdlog::info("DataAggregator: all threads joined");
}

void DataAggregator::forceRefresh() {
    refresh_gen_++;
    cv_.notify_all();
}

void DataAggregator::setOnChange(std::function<void()> cb) {
    on_change_ = std::move(cb);
}

void DataAggregator::sleepFor(int seconds) {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    int gen = refresh_gen_.load();
    cv_.wait_for(lock, std::chrono::seconds(seconds), [this, gen] {
        return !running_.load() || refresh_gen_.load() != gen;
    });
}



void DataAggregator::metarLoop() {
    while (running_) {
        HttpClient http;
        try {
            auto reports = metar_fetcher_.fetch(http, config_->metarStations());
            auto tafs = taf_fetcher_.fetch(http, config_->metarStations());
            {
                std::unique_lock lock(data_mutex_);
                metar_reports_ = std::move(reports);
                taf_forecasts_ = std::move(tafs);

                 auto primary_it = std::find_if(metar_reports_.begin(), metar_reports_.end(), [&](const MetarReport& report) {
                    return report.station_id == primary_station_id_;
                });
                if (primary_it != metar_reports_.end()) {
                    primary_metar_ = *primary_it;
                    has_primary_metar_ = true;

                    std::string obs_date = primary_metar_.obs_time.size() >= 10
                        ? primary_metar_.obs_time.substr(0, 10)
                        : utils::currentUtcDateString();

                    if (!has_primary_station_day_high_ || primary_station_day_high_date_ != obs_date) {
                        primary_station_day_high_date_ = obs_date;
                        primary_station_day_high_c_ = primary_metar_.temp_c;
                        has_primary_station_day_high_ = true;
                    } else {
                        primary_station_day_high_c_ = std::max(primary_station_day_high_c_, primary_metar_.temp_c);
                    }
                } else {
                    has_primary_metar_ = false;
                }

                metar_error_.clear();
                taf_error_.clear();
                metar_last_refresh_ = utils::currentUtcTimeString();
                taf_last_refresh_ = metar_last_refresh_;
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("Aviation fetch error: {}", e.what());
            std::unique_lock lock(data_mutex_);
            metar_error_ = e.what();
            taf_error_ = e.what();
        }

        if (on_change_) on_change_();
        sleepFor(config_->metarRefreshSeconds());
    }
}

void DataAggregator::nwpLoop() {
    while (running_) {
        HttpClient http;
        try {
            auto forecasts = nwp_fetcher_.fetch(http, config_->londonLat(), config_->londonLon());
            {
                std::unique_lock lock(data_mutex_);
                auto next_ensemble = forecast_engine_.compute(forecasts, pws_observations_);
                int changed_days = annotateEnsembleChanges(ensemble_forecasts_, next_ensemble);
                nwp_forecasts_ = std::move(forecasts);
                ensemble_forecasts_ = std::move(next_ensemble);
                nwp_error_.clear();
                nwp_change_summary_ = changed_days > 0
                    ? ("Δ " + std::to_string(changed_days) + (changed_days == 1 ? " day" : " days"))
                    : "steady";
                nwp_last_refresh_ = utils::currentUtcTimeString();
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("NWP fetch error: {}", e.what());
            std::unique_lock lock(data_mutex_);
            nwp_error_ = e.what();
        }

        if (on_change_) on_change_();
        sleepFor(config_->nwpRefreshSeconds());
    }
}

void DataAggregator::wundergroundLoop() {
    while (running_) {
        if (config_->wundergroundApiKey().empty()) {
            {
                std::unique_lock lock(data_mutex_);
                pws_observations_.clear();
                auto next_ensemble = forecast_engine_.compute(nwp_forecasts_, pws_observations_);
                annotateEnsembleChanges(ensemble_forecasts_, next_ensemble);
                ensemble_forecasts_ = std::move(next_ensemble);
                wu_error_ = "Set wunderground_api_key in config.yaml";
                wu_last_refresh_.clear();
            }
            if (on_change_) on_change_();
            sleepFor(config_->wundergroundRefreshSeconds());
            continue;
        }

        HttpClient http;
        try {
            auto obs = wu_fetcher_.fetch(http, config_->londonLat(), config_->londonLon(),
                                          config_->wundergroundApiKey());
            {
                std::unique_lock lock(data_mutex_);
                pws_observations_ = std::move(obs);
                auto next_ensemble = forecast_engine_.compute(nwp_forecasts_, pws_observations_);
                annotateEnsembleChanges(ensemble_forecasts_, next_ensemble);
                ensemble_forecasts_ = std::move(next_ensemble);
                wu_error_.clear();
                wu_last_refresh_ = utils::currentUtcTimeString();
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("Wunderground fetch error: {}", e.what());
            std::unique_lock lock(data_mutex_);
            wu_error_ = e.what();
        }

        if (on_change_) on_change_();
        sleepFor(config_->wundergroundRefreshSeconds());
    }
}

void DataAggregator::hourlyLoop() {
    while (running_) {
        HttpClient http;
        try {
            auto obs = hourly_fetcher_.fetch(http, config_->londonLat(), config_->londonLon());
            {
                std::unique_lock lock(data_mutex_);
                hourly_obs_ = std::move(obs);
                auto next_ensemble = forecast_engine_.compute(nwp_forecasts_, pws_observations_);
                annotateEnsembleChanges(ensemble_forecasts_, next_ensemble);
                ensemble_forecasts_ = std::move(next_ensemble);
                hourly_error_.clear();
                hourly_last_refresh_ = utils::currentUtcTimeString();
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("Hourly fetch error: {}", e.what());
            std::unique_lock lock(data_mutex_);
            hourly_error_ = e.what();
        }

        if (on_change_) on_change_();
        sleepFor(config_->hourlyRefreshSeconds());
    }
}



bool DataAggregator::hasPrimaryMetar() const {
    std::shared_lock lock(data_mutex_);
    return has_primary_metar_;
}

MetarReport DataAggregator::getPrimaryMetar() const {
    std::shared_lock lock(data_mutex_);
    return primary_metar_;
}

bool DataAggregator::hasPrimaryStationDayHigh() const {
    std::shared_lock lock(data_mutex_);
    return has_primary_station_day_high_;
}

float DataAggregator::getPrimaryStationDayHigh() const {
    std::shared_lock lock(data_mutex_);
    return primary_station_day_high_c_;
}

std::string DataAggregator::getPrimaryStationId() const {
    std::shared_lock lock(data_mutex_);
    return primary_station_id_;
}

std::vector<MetarReport> DataAggregator::getMetarReports() const {
    std::shared_lock lock(data_mutex_);
    return metar_reports_;
}

std::vector<NwpModelForecast> DataAggregator::getNwpForecasts() const {
    std::shared_lock lock(data_mutex_);
    return nwp_forecasts_;
}

std::vector<TafForecast> DataAggregator::getTafForecasts() const {
    std::shared_lock lock(data_mutex_);
    return taf_forecasts_;
}

std::vector<PwsObservation> DataAggregator::getPwsObservations() const {
    std::shared_lock lock(data_mutex_);
    return pws_observations_;
}

std::vector<HourlyObs> DataAggregator::getHourlyObs() const {
    std::shared_lock lock(data_mutex_);
    return hourly_obs_;
}

std::vector<EnsembleForecast> DataAggregator::getEnsembleForecasts() const {
    std::shared_lock lock(data_mutex_);
    return ensemble_forecasts_;
}

std::string DataAggregator::getMetarError() const {
    std::shared_lock lock(data_mutex_);
    return metar_error_;
}

std::string DataAggregator::getNwpError() const {
    std::shared_lock lock(data_mutex_);
    return nwp_error_;
}

std::string DataAggregator::getTafError() const {
    std::shared_lock lock(data_mutex_);
    return taf_error_;
}

std::string DataAggregator::getWundergroundError() const {
    std::shared_lock lock(data_mutex_);
    return wu_error_;
}

std::string DataAggregator::getHourlyError() const {
    std::shared_lock lock(data_mutex_);
    return hourly_error_;
}

std::string DataAggregator::getNwpChangeSummary() const {
    std::shared_lock lock(data_mutex_);
    return nwp_change_summary_;
}

std::string DataAggregator::getMetarLastRefresh() const {
    std::shared_lock lock(data_mutex_);
    return metar_last_refresh_;
}

std::string DataAggregator::getNwpLastRefresh() const {
    std::shared_lock lock(data_mutex_);
    return nwp_last_refresh_;
}

std::string DataAggregator::getTafLastRefresh() const {
    std::shared_lock lock(data_mutex_);
    return taf_last_refresh_;
}

std::string DataAggregator::getWundergroundLastRefresh() const {
    std::shared_lock lock(data_mutex_);
    return wu_last_refresh_;
}

std::string DataAggregator::getHourlyLastRefresh() const {
    std::shared_lock lock(data_mutex_);
    return hourly_last_refresh_;
}
