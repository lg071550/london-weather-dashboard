#pragma once
#include "Config.h"
#include "HttpClient.h"
#include "MetarFetcher.h"
#include "TafFetcher.h"
#include "NwpFetcher.h"
#include "WundergroundFetcher.h"
#include "OpenMeteoHourlyFetcher.h"
#include "ForecastEngine.h"
#include "DataStructs.h"

#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <shared_mutex>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

class DataAggregator {
public:
    explicit DataAggregator(std::shared_ptr<Config> config);
    ~DataAggregator();

    void start();
    void stop();
    void forceRefresh();

    void setOnChange(std::function<void()> cb);

    
    bool hasPrimaryMetar() const;
    MetarReport getPrimaryMetar() const;
    bool hasPrimaryStationDayHigh() const;
    float getPrimaryStationDayHigh() const;
    std::string getPrimaryStationId() const;

    std::vector<MetarReport>       getMetarReports() const;
    std::vector<TafForecast>       getTafForecasts() const;
    std::vector<NwpModelForecast>  getNwpForecasts() const;
    std::vector<PwsObservation>    getPwsObservations() const;
    std::vector<HourlyObs>         getHourlyObs() const;
    std::vector<EnsembleForecast>  getEnsembleForecasts() const;

    
    std::string getMetarError() const;
    std::string getTafError() const;
    std::string getNwpError() const;
    std::string getWundergroundError() const;
    std::string getHourlyError() const;
    std::string getNwpChangeSummary() const;

    
    std::string getMetarLastRefresh() const;
    std::string getTafLastRefresh() const;
    std::string getNwpLastRefresh() const;
    std::string getWundergroundLastRefresh() const;
    std::string getHourlyLastRefresh() const;

private:
    std::shared_ptr<Config> config_;

    MetarFetcher              metar_fetcher_;
    TafFetcher                taf_fetcher_;
    NwpFetcher                nwp_fetcher_;
    WundergroundFetcher       wu_fetcher_;
    OpenMeteoHourlyFetcher    hourly_fetcher_;
    ForecastEngine            forecast_engine_;

    
    mutable std::shared_mutex data_mutex_;
    std::string primary_station_id_;
    MetarReport primary_metar_;
    bool has_primary_metar_ = false;
    float primary_station_day_high_c_ = 0.0f;
    std::string primary_station_day_high_date_;
    bool has_primary_station_day_high_ = false;
    std::vector<MetarReport>       metar_reports_;
    std::vector<TafForecast>       taf_forecasts_;
    std::vector<NwpModelForecast>  nwp_forecasts_;
    std::vector<PwsObservation>    pws_observations_;
    std::vector<HourlyObs>         hourly_obs_;
    std::vector<EnsembleForecast>  ensemble_forecasts_;

    std::string metar_error_;
    std::string taf_error_;
    std::string nwp_error_;
    std::string wu_error_;
    std::string hourly_error_;
    std::string nwp_change_summary_;

    std::string metar_last_refresh_;
    std::string taf_last_refresh_;
    std::string nwp_last_refresh_;
    std::string wu_last_refresh_;
    std::string hourly_last_refresh_;

    
    std::atomic<bool> running_{false};
    std::atomic<int>  refresh_gen_{0};
    std::mutex        cv_mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> threads_;
    std::function<void()> on_change_;

    void metarLoop();
    void nwpLoop();
    void wundergroundLoop();
    void hourlyLoop();

    void sleepFor(int seconds);
};
