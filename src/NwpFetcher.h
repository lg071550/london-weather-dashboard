#pragma once
#include "DataStructs.h"
#include "HttpClient.h"
#include <vector>

class NwpFetcher {
public:
    NwpFetcher() = default;
    std::vector<NwpModelForecast> fetch(HttpClient& http, double lat, double lon);
};
