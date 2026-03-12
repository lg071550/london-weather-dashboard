#pragma once
#include <string>
#include <vector>

class Config {
public:
    explicit Config(const std::string& path);

    std::string wundergroundApiKey() const { return wu_api_key_; }
    std::vector<std::string> metarStations() const { return metar_stations_; }
    int nwpRefreshSeconds() const { return nwp_refresh_; }
    int metarRefreshSeconds() const { return metar_refresh_; }
    int wundergroundRefreshSeconds() const { return wu_refresh_; }
    int hourlyRefreshSeconds() const { return hourly_refresh_; }
    double londonLat() const { return lat_; }
    double londonLon() const { return lon_; }

private:
    std::string wu_api_key_;
    std::vector<std::string> metar_stations_;
    int nwp_refresh_ = 300;
    int metar_refresh_ = 60;
    int wu_refresh_ = 180;
    int hourly_refresh_ = 300;
    double lat_ = 51.5074;
    double lon_ = -0.1278;
};
