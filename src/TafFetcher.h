#pragma once
#include "DataStructs.h"
#include "HttpClient.h"
#include <vector>
#include <string>

class TafFetcher {
public:
    TafFetcher() = default;
    std::vector<TafForecast> fetch(HttpClient& http, const std::vector<std::string>& stations);
};