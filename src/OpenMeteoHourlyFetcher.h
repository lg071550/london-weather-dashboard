#pragma once
#include "DataStructs.h"
#include "HttpClient.h"
#include <vector>

class OpenMeteoHourlyFetcher {
public:
    OpenMeteoHourlyFetcher() = default;
    std::vector<HourlyObs> fetch(HttpClient& http, double lat, double lon);
};
