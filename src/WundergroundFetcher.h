#pragma once
#include "DataStructs.h"
#include "HttpClient.h"
#include <vector>
#include <string>

class WundergroundFetcher {
public:
    WundergroundFetcher() = default;
    std::vector<PwsObservation> fetch(HttpClient& http, double lat, double lon,
                                       const std::string& apiKey);
};
