#pragma once
#include <string>
#include <stdexcept>

class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient() = default;

    
    std::string get(const std::string& url);
};
