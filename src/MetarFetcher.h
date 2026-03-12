#pragma once
#include "DataStructs.h"
#include "HttpClient.h"
#include <vector>
#include <string>

class MetarFetcher {
public:
    MetarFetcher() = default;
    std::vector<MetarReport> fetch(HttpClient& http, const std::vector<std::string>& stations);
};
