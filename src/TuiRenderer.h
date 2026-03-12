#pragma once
#include "DataAggregator.h"
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>

class TuiRenderer {
public:
    explicit TuiRenderer(std::shared_ptr<DataAggregator> aggregator);
    void run();   

private:
    std::shared_ptr<DataAggregator> aggregator_;
    ftxui::ScreenInteractive screen_;
    int metar_scroll_ = 0;
    int theme_index_ = 0;

    ftxui::Element render();
    ftxui::Element renderHeader();
    ftxui::Element renderNwpPanel();
    ftxui::Element renderMetarPanel();
    ftxui::Element renderEdgePanel();
    ftxui::Element renderAviationAlertsPanel();
    ftxui::Element renderHourlyPanel();
    ftxui::Element renderTmaxConsensus();
    ftxui::Element renderStatusBar();
    ftxui::Element renderKeyHints();
};
