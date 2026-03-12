#pragma once
#include "DataStructs.h"
#include <vector>

class ForecastEngine {
public:
    ForecastEngine() = default;

    
    
    std::vector<EnsembleForecast> compute(const std::vector<NwpModelForecast>& models,
                                          const std::vector<PwsObservation>& pwsObservations = {});
};
