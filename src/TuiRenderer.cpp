#include "TuiRenderer.h"
#include "Utils.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace ftxui;

namespace {
    struct TuiTheme {
        Color title;
        Color primary;
        Color secondary;
        Color text;
        Color ok;
        Color warn;
        Color error;
        Color muted;
        Color header_bg;
        Color status_bg;
        Color hints_bg;
        Color table_border;
    };

    const std::vector<TuiTheme> THEMES = {
        {Color::RGB(136, 192, 208), Color::RGB(129, 161, 193), Color::RGB(163, 190, 140), Color::RGB(163, 190, 140), Color::RGB(236, 239, 244), Color::RGB(241, 250, 140), Color::RGB(255, 85, 85), Color::RGB(76, 86, 106), Color::RGB(46, 52, 64), Color::RGB(59, 66, 82), Color::RGB(67, 76, 94)},
        {Color::RGB(98, 114, 164), Color::RGB(139, 233, 253), Color::RGB(189, 147, 249), Color::RGB(80, 250, 123), Color::RGB(248, 248, 242), Color::RGB(241, 250, 140), Color::RGB(255, 85, 85), Color::RGB(98, 114, 164), Color::RGB(40, 42, 54), Color::RGB(68, 71, 90), Color::RGB(68, 71, 90)},
        {Color::RGB(211, 134, 155), Color::RGB(131, 165, 152), Color::RGB(250, 189, 47), Color::RGB(184, 187, 38), Color::RGB(235, 219, 178), Color::RGB(250, 189, 47), Color::RGB(204, 36, 29), Color::RGB(146, 131, 116), Color::RGB(40, 40, 40), Color::RGB(50, 48, 47), Color::RGB(80, 73, 69)},
        {Color::RGB(94, 129, 172), Color::RGB(143, 188, 187), Color::RGB(180, 142, 173), Color::RGB(163, 190, 140), Color::RGB(242, 244, 248), Color::RGB(235, 203, 139), Color::RGB(191, 97, 106), Color::RGB(129, 161, 193), Color::RGB(36, 41, 51), Color::RGB(47, 54, 64), Color::RGB(59, 66, 82)}
    };

    const std::vector<std::string> THEME_NAMES = {"Nord", "Dracula", "Gruvbox", "Slate"};

    const TuiTheme& currentTheme(int index) {
        return THEMES[static_cast<size_t>(index % static_cast<int>(THEMES.size()))];
    }

    bool containsAny(const std::string& value, const std::vector<std::string>& needles) {
        return std::any_of(needles.begin(), needles.end(), [&](const std::string& needle) {
            return value.find(needle) != std::string::npos;
        });
    }

    std::string formatDelta(float value) {
        std::string prefix = value > 0.0f ? "+" : "";
        return "dT " + prefix + utils::formatFloat(value, 1) + "C";
    }

    std::string formatRefreshAge(const std::string& iso_time) {
        if (iso_time.empty()) {
            return "--";
        }

        std::tm tm = {};
        std::istringstream ss(iso_time);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%SZ");
        if (ss.fail()) {
            ss.clear();
            ss.str(iso_time);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        }
        if (ss.fail()) {
            int age_minutes = utils::computeAgeMinutes(iso_time);
            return age_minutes >= 0 ? std::to_string(age_minutes) + "m" : "--";
        }

        time_t parsed = _mkgmtime(&tm);
        if (parsed == -1) {
            return "--";
        }

        auto parsed_tp = std::chrono::system_clock::from_time_t(parsed);
        auto now = std::chrono::system_clock::now();
        auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - parsed_tp).count();
        if (age_seconds < 0) {
            age_seconds = 0;
        }
        if (age_seconds < 120) {
            return std::to_string(static_cast<int>(age_seconds)) + "s";
        }
        return std::to_string(static_cast<int>(age_seconds / 60)) + "m";
    }
}

TuiRenderer::TuiRenderer(std::shared_ptr<DataAggregator> aggregator)
    : aggregator_(std::move(aggregator))
    , screen_(ScreenInteractive::Fullscreen()) {
    aggregator_->setOnChange([this]() {
        screen_.PostEvent(Event::Custom);
    });
}

void TuiRenderer::run() {
    auto renderer = Renderer([this] { return render(); });
    std::atomic<bool> repaint_running = true;

    std::thread repaint_thread([this, &repaint_running]() {
        while (repaint_running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!repaint_running.load()) {
                break;
            }
            screen_.PostEvent(Event::Custom);
        }
    });

    auto component = CatchEvent(renderer, [this](Event event) -> bool {
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            screen_.Exit();
            return true;
        }
        if (event == Event::ArrowUp) {
            metar_scroll_ = std::max(0, metar_scroll_ - 1);
            return true;
        }
        if (event == Event::ArrowDown) {
            metar_scroll_++;
            return true;
        }
        if (event == Event::Character('r') || event == Event::Character('R')) {
            aggregator_->forceRefresh();
            return true;
        }
        if (event == Event::Character('t') || event == Event::Character('T')) {
            theme_index_ = (theme_index_ + 1) % static_cast<int>(THEMES.size());
            return true;
        }
        return false;
    });

    screen_.Loop(component);
    repaint_running = false;
    repaint_thread.join();
}

ftxui::Element TuiRenderer::render() {
    const auto& theme = currentTheme(theme_index_);

    return vbox({
        renderHeader(),
        renderNwpPanel() | flex_shrink,
        renderMetarPanel() | flex_shrink,
        hbox({
            renderHourlyPanel() | flex,
            separator() | color(theme.muted),
            vbox({
                renderEdgePanel() | flex_shrink,
                renderTmaxConsensus() | flex_shrink,
                renderAviationAlertsPanel() | flex,
            }) | size(WIDTH, GREATER_THAN, 42) | flex_shrink,
        }) | flex,
        renderStatusBar(),
        renderKeyHints(),
    });
}

ftxui::Element TuiRenderer::renderHeader() {
    const auto& theme = currentTheme(theme_index_);
    std::string utc = utils::currentUtcTimeString();
    return hbox({
        text("LONDON WEATHER DASH") | bold | color(theme.title),
        text(" | ") | color(theme.muted),
        text("EGLC") | bold | color(theme.secondary),
        text(" | ") | color(theme.muted),
        filler(),
        text(utc) | bold | color(Color::White),
    }) | bgcolor(theme.header_bg) | bold;
}

ftxui::Element TuiRenderer::renderNwpPanel() {
    const auto& theme = currentTheme(theme_index_);
    auto nwps = aggregator_->getNwpForecasts();
    auto ensemble = aggregator_->getEnsembleForecasts();

    if (nwps.empty()) {
        return window(text(" NWP ENSEMBLE ") | bold | color(theme.title),
                      text("  Fetching...") | color(theme.muted));
    }

    std::vector<std::string> dates;
    for (const auto& e : ensemble) {
        dates.push_back(e.date);
        if (dates.size() >= 5) break;
    }

    std::vector<std::string> header = {"Model"};
    for (const auto& date : dates) {
        header.push_back(date);
    }
    header.push_back("Run");
    header.push_back("Age");

    std::vector<std::vector<std::string>> rows;
    rows.push_back(header);

    for (const auto& m : nwps) {
        std::vector<std::string> row;
        std::string name = m.model;
        for (auto& c : name) c = static_cast<char>(toupper(c));
        row.push_back(name);

        for (const auto& date : dates) {
            auto it = m.tmax_by_date.find(date);
            if (it != m.tmax_by_date.end()) {
                row.push_back(utils::formatFloat(it->second, 1) + "\xC2\xB0" "C");
            } else {
                row.push_back("--");
            }
        }
        row.push_back(m.run_time.size() > 16 ? m.run_time.substr(0, 16) : m.run_time);
        row.push_back(formatRefreshAge(m.run_time));
        rows.push_back(row);
    }

    std::vector<std::string> footer;
    footer.push_back("ENSEMBLE");
    for (size_t i = 0; i < dates.size() && i < ensemble.size(); ++i) {
        const auto& e = ensemble[i];
        footer.push_back(
            utils::formatFloat(e.weighted_tmax, 1) + "\xC2\xB0" "C \xC2\xB1" +
            utils::formatFloat(e.sigma, 1) + " " +
            (e.models_agree ? "\xE2\x9C\x93" : "\xE2\x9C\x97"));
    }
    footer.push_back("spread");
    if (!ensemble.empty()) {
        std::string spreads;
        for (size_t i = 0; i < dates.size() && i < ensemble.size(); ++i) {
            if (!spreads.empty()) spreads += "/";
            spreads += utils::formatFloat(ensemble[i].spread, 1);
        }
        footer.push_back(spreads + "\xC2\xB0" "C");
    } else {
        footer.push_back("--");
    }
    rows.push_back(footer);

    auto table = Table(rows);
    auto all_nwp = table.SelectAll();
    all_nwp.Border(LIGHT);
    auto nwp_header = table.SelectRow(0);
    nwp_header.Decorate(bold);
    nwp_header.SeparatorVertical(LIGHT);
    auto nwp_footer = table.SelectRow(static_cast<int>(rows.size()) - 1);
    nwp_footer.Decorate(bold | color(theme.ok));

    for (size_t model_index = 0; model_index < nwps.size(); ++model_index) {
        size_t ri = model_index + 1;
        for (size_t di = 0; di < dates.size() && di < ensemble.size(); ++di) {
            auto& e = ensemble[di];
            const std::string& model_lower = nwps[model_index].model;
            auto it = e.model_tmax.find(model_lower);
            if (it != e.model_tmax.end()) {
                float diff = std::abs(it->second - e.weighted_tmax);
                if (diff > 1.5f) {
                    auto nwp_cell = table.SelectCell(static_cast<int>(ri), static_cast<int>(di + 1));
                    nwp_cell.Decorate(color(theme.error));
                }
            }
        }
    }

    return window(text(" NWP ENSEMBLE ") | bold | color(theme.title),
                  table.Render());
}

ftxui::Element TuiRenderer::renderMetarPanel() {
    const auto& theme = currentTheme(theme_index_);
    auto metars = aggregator_->getMetarReports();

    if (metars.empty()) {
        return window(text(" METAR ") | bold | color(theme.title),
                      text("  Fetching...") | color(theme.muted));
    }

    std::vector<std::vector<std::string>> rows;
    rows.push_back({"Station", "Time", "Temp", "Dew", "Wind", "Vis", "Sky", "WX", "Age"});

    for (const auto& m : metars) {
        std::string time_short = m.obs_time;
        if (time_short.size() > 16) time_short = time_short.substr(11, 5);

        rows.push_back({
            m.station_id,
            time_short,
            utils::formatFloat(m.temp_c, 1) + "\xC2\xB0" "C",
            utils::formatFloat(m.dewpoint_c, 1) + "\xC2\xB0" "C",
            m.wind_dir_deg + "/" + std::to_string(m.wind_speed_kt) + "kt",
            m.visibility,
            m.sky_condition,
            m.wx_string.empty() ? "--" : m.wx_string,
            formatRefreshAge(m.obs_time)
        });
    }

    int total_data = static_cast<int>(rows.size()) - 1;
    int max_visible = 8;
    metar_scroll_ = std::clamp(metar_scroll_, 0, std::max(0, total_data - max_visible));

    std::vector<std::vector<std::string>> visible;
    visible.push_back(rows[0]);
    int start = metar_scroll_ + 1;
    int end = std::min(start + max_visible, static_cast<int>(rows.size()));
    for (int i = start; i < end; ++i) {
        visible.push_back(rows[i]);
    }

    auto table = Table(visible);
    auto all_metar = table.SelectAll();
    all_metar.Border(LIGHT);
    auto metar_header = table.SelectRow(0);
    metar_header.Decorate(bold);
    metar_header.SeparatorVertical(LIGHT);

    for (int ri = 1; ri < static_cast<int>(visible.size()); ++ri) {
        int orig_idx = metar_scroll_ + ri;
        if (orig_idx < static_cast<int>(rows.size())) {
            const auto& metar = metars[orig_idx - 1];
            int age_minutes = utils::computeAgeMinutes(metar.obs_time);
            if (age_minutes > 90) {
                auto metar_row = table.SelectRow(ri);
                metar_row.Decorate(color(theme.error));
            } else if (age_minutes > 30) {
                auto metar_row = table.SelectRow(ri);
                metar_row.Decorate(color(theme.warn));
            }
        }
    }

    std::string title = " METAR (" + std::to_string(total_data) + " reports) ";
    if (total_data > max_visible) {
        title += "[" + std::to_string(metar_scroll_ + 1) + "-" +
                 std::to_string(std::min(metar_scroll_ + max_visible, total_data)) +
                 "] ";
    }

    return window(text(title) | bold | color(theme.title),
                  table.Render());
}

ftxui::Element TuiRenderer::renderEdgePanel() {
    const auto& theme = currentTheme(theme_index_);
    auto ensemble = aggregator_->getEnsembleForecasts();

    if (!aggregator_->hasPrimaryMetar()) {
        return window(text(" EGLC INFO ") | bold | color(theme.title),
                      text("  Waiting for primary station METAR...") | color(theme.muted));
    }

    auto primary_metar = aggregator_->getPrimaryMetar();
    const bool has_day_high = aggregator_->hasPrimaryStationDayHigh();
    const float day_high = has_day_high ? aggregator_->getPrimaryStationDayHigh() : primary_metar.temp_c;
    const std::string station = aggregator_->getPrimaryStationId();
    const std::string current_date = utils::currentUtcDateString();

    auto today_it = std::find_if(ensemble.begin(), ensemble.end(), [&](const EnsembleForecast& e) {
        return e.date == current_date;
    });

    std::vector<Element> lines;

    std::string latest_line = station + " latest " +
        utils::formatFloat(primary_metar.temp_c, 1) + "\xC2\xB0" "C / dew " +
        utils::formatFloat(primary_metar.dewpoint_c, 1) + "\xC2\xB0" "C / wind " +
        primary_metar.wind_dir_deg + "/" + std::to_string(primary_metar.wind_speed_kt) + "kt";
    if (!primary_metar.wx_string.empty()) {
        latest_line += " / wx " + primary_metar.wx_string;
    }
    if (!primary_metar.sky_condition.empty()) {
        latest_line += " / sky " + primary_metar.sky_condition;
    }
    lines.push_back(text(latest_line) | color(theme.text));

    std::string high_line = station + " high so far " + utils::formatFloat(day_high, 1) + "\xC2\xB0" "C";
    if (today_it != ensemble.end()) {
        float gap_to_consensus = today_it->weighted_tmax - day_high;
        high_line += " / ensemble " + utils::formatFloat(today_it->weighted_tmax, 1) + "\xC2\xB0" "C";
        high_line += " / gap " + utils::formatFloat(gap_to_consensus, 1) + "\xC2\xB0" "C";
    }
    lines.push_back(text(high_line) | color(theme.secondary));

    if (today_it != ensemble.end()) {
        const bool wet_signal = containsAny(primary_metar.wx_string, {"RA", "DZ", "SH", "TS"});
        const bool low_cloud_signal = containsAny(primary_metar.sky_condition, {"OVC", "BKN00", "BKN01", "BKN02"});
        const bool supportive_signal = !wet_signal && !low_cloud_signal;
        const float current_gap = today_it->weighted_tmax - primary_metar.temp_c;
        const float high_gap = today_it->weighted_tmax - day_high;

        std::string bias = "Balanced intraday";
        Color bias_color = theme.secondary;

        if (day_high >= today_it->weighted_tmax + 0.2f) {
            bias = "Target already exceeded";
            bias_color = theme.ok;
        } else if (high_gap <= 0.3f || (current_gap <= 0.7f && supportive_signal)) {
            bias = "Upside risk elevated";
            bias_color = theme.warn;
        } else if (current_gap >= 2.0f && (wet_signal || low_cloud_signal)) {
            bias = "Upside risk limited";
            bias_color = theme.muted;
        }

        lines.push_back(text("Signal: " + bias) | bold | color(bias_color));
    }

    return window(text(" EGLC INFO ") | bold | color(theme.title),
                  vbox(lines));
}

ftxui::Element TuiRenderer::renderAviationAlertsPanel() {
    const auto& theme = currentTheme(theme_index_);
    auto metars = aggregator_->getMetarReports();
    auto tafs = aggregator_->getTafForecasts();
    auto taf_err = aggregator_->getTafError();

    std::vector<std::vector<std::string>> rows;
    rows.push_back({"Type", "Station", "Time", "Alert"});

    for (const auto& metar : metars) {
        if (!metar.is_speci) {
            continue;
        }

        std::string time_short = metar.obs_time;
        if (time_short.size() > 16) time_short = time_short.substr(11, 5);
        rows.push_back({"SPECI", metar.station_id, time_short, metar.raw_text});
    }

    for (const auto& taf : tafs) {
        if (taf.alerts.empty()) {
            continue;
        }

        std::string issue_short = taf.issue_time;
        if (issue_short.size() > 16) issue_short = issue_short.substr(11, 5);

        for (const auto& alert : taf.alerts) {
            rows.push_back({"TAF", taf.station_id, issue_short, alert});
        }
    }

    if (rows.size() == 1) {
        std::string msg = taf_err.empty() ? "No active TAF/SPECI alerts" : ("Error: " + taf_err);
        return window(text(" TAF / SPECI ALERTS ") | bold | color(theme.title),
                      text("  " + msg) | color(taf_err.empty() ? theme.muted : theme.error));
    }

    auto table = Table(rows);
    auto all_alerts = table.SelectAll();
    all_alerts.Border(LIGHT);
    auto alerts_header = table.SelectRow(0);
    alerts_header.Decorate(bold);
    alerts_header.SeparatorVertical(LIGHT);

    for (int ri = 1; ri < static_cast<int>(rows.size()); ++ri) {
        auto row = table.SelectRow(ri);
        if (rows[ri][0] == "SPECI") {
            row.Decorate(color(theme.error));
        } else {
            row.Decorate(color(theme.warn));
        }
    }

    return window(text(" TAF / SPECI ALERTS ") | bold | color(theme.title),
                  table.Render());
}

ftxui::Element TuiRenderer::renderHourlyPanel() {
    const auto& theme = currentTheme(theme_index_);
    auto hours = aggregator_->getHourlyObs();

    if (hours.empty()) {
        return window(text(" HOURLY TIMELINE ") | bold | color(theme.title),
                      text("  Fetching...") | color(theme.muted));
    }

    std::string current_hour = utils::currentUtcHourString();

    int current_idx = -1;
    for (size_t i = 0; i < hours.size(); ++i) {
        if (hours[i].hour_utc == current_hour) {
            current_idx = static_cast<int>(i);
            break;
        }
    }

    constexpr int day_block = 24;
    int day1_start = 0;
    int day1_end = std::min(static_cast<int>(hours.size()), day1_start + day_block);
    int day2_start = day1_end;
    int day2_end = std::min(static_cast<int>(hours.size()), day2_start + day_block);

    auto build_table = [&](int from, int to) -> Element {
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Hour(UTC)", "Temp\xC2\xB0" "C", "Precip mm", "Wind km/h", "Cloud%", "Notes"});

        for (int i = from; i < to; ++i) {
            const auto& h = hours[i];
            std::string notes;
            if (i == current_idx) {
                notes = "NOW";
            }

            rows.push_back({
                h.hour_utc,
                utils::formatFloat(h.temp_c, 1),
                utils::formatFloat(h.precip_mm, 1),
                utils::formatFloat(h.wind_kmh, 0),
                std::to_string(h.cloud_pct),
                notes
            });
        }

        auto table = Table(rows);
        auto all = table.SelectAll();
        all.Border(LIGHT);
        auto header = table.SelectRow(0);
        header.Decorate(bold);
        header.SeparatorVertical(LIGHT);

        if (current_idx >= from && current_idx < to) {
            auto now_row = table.SelectRow(current_idx - from + 1);
            now_row.Decorate(bold | color(theme.primary));
        }

        return table.Render() | flex;
    };

    Element left = build_table(day1_start, day1_end);
    Element right = day2_start < day2_end
        ? build_table(day2_start, day2_end)
        : text("No next-day hours") | color(theme.muted) | flex;

    return window(text(" HOURLY TIMELINE (2 DAYS) ") | bold | color(theme.title),
                  hbox({left, separator() | color(theme.muted), right}) | flex);
}

ftxui::Element TuiRenderer::renderTmaxConsensus() {
    const auto& theme = currentTheme(theme_index_);
    auto ensemble = aggregator_->getEnsembleForecasts();
    const bool has_day_high = aggregator_->hasPrimaryStationDayHigh();
    const float observed_high = has_day_high ? aggregator_->getPrimaryStationDayHigh() : 0.0f;
    const std::string primary_station = aggregator_->getPrimaryStationId();

    if (ensemble.empty()) {
        return window(text(" TMAX CONSENSUS ") | bold | color(theme.title),
                      text("  Fetching...") | color(theme.muted));
    }

    std::vector<Element> lines;

    for (size_t i = 0; i < ensemble.size() && i < 5; ++i) {
        const auto& e = ensemble[i];
        std::string line = e.date + "  " +
            utils::formatFloat(e.weighted_tmax, 1) + "\xC2\xB0" "C \xC2\xB1 " +
            utils::formatFloat(e.sigma, 1) + "\xC2\xB0" "C  [" +
            std::to_string(e.model_count) + " models]";

        if (i == 0 && has_day_high) {
            line += "  " + primary_station + " high so far: " + utils::formatFloat(observed_high, 1) + "\xC2\xB0" "C";
        }

        auto el = text(line);
        if (e.models_agree) {
            el = el | color(theme.ok);
        } else {
            el = el | color(theme.warn);
        }
        lines.push_back(el);
    }

    return window(text(" TMAX CONSENSUS ") | bold | color(theme.title),
                  vbox(lines) | size(HEIGHT, EQUAL, static_cast<int>(lines.size())));
}

ftxui::Element TuiRenderer::renderStatusBar() {
    const auto& theme = currentTheme(theme_index_);
    auto metarRefresh = aggregator_->getMetarLastRefresh();
    auto tafRefresh = aggregator_->getTafLastRefresh();
    auto nwpRefresh = aggregator_->getNwpLastRefresh();
    auto wuRefresh = aggregator_->getWundergroundLastRefresh();
    auto hourlyRefresh = aggregator_->getHourlyLastRefresh();
    auto nwpChange = aggregator_->getNwpChangeSummary();

    auto metarErr = aggregator_->getMetarError();
    auto tafErr = aggregator_->getTafError();
    auto nwpErr = aggregator_->getNwpError();
    auto wuErr = aggregator_->getWundergroundError();
    auto hourlyErr = aggregator_->getHourlyError();

    Elements parts;

    auto addSource = [&](const std::string& name, const std::string& refresh,
                         const std::string& err, const std::string& suffix = "") {
        if (!parts.empty()) parts.push_back(text(" | ") | color(theme.muted));
        if (!err.empty()) {
            parts.push_back(text(name + ": ERR") | color(theme.error));
        } else if (refresh.empty()) {
            parts.push_back(text(name + ": --") | color(theme.muted));
        } else {
            parts.push_back(text(name + ": " + formatRefreshAge(refresh) + suffix) | color(Color::GrayLight));
        }
    };

    addSource("METAR", metarRefresh, metarErr);
    addSource("TAF", tafRefresh, tafErr);
    addSource("NWP", nwpRefresh, nwpErr, nwpChange.empty() ? "" : " " + nwpChange);
    if (!(wuRefresh.empty() && wuErr == "Set wunderground_api_key in config.yaml")) {
        addSource("WU", wuRefresh, wuErr);
    }
    addSource("HOURLY", hourlyRefresh, hourlyErr);

    return hbox(parts) | bgcolor(theme.status_bg);
}

ftxui::Element TuiRenderer::renderKeyHints() {
    const auto& theme = currentTheme(theme_index_);
    std::string name = THEME_NAMES[theme_index_ % static_cast<int>(THEME_NAMES.size())];
    return hbox({
        text("[q]") | bold | color(Color::White),
        text(" quit  ") | color(Color::GrayLight),
        text("[\xE2\x86\x91\xE2\x86\x93]") | bold | color(Color::White),
        text(" scroll METAR  ") | color(Color::GrayLight),
        text("[r]") | bold | color(Color::White),
        text(" refresh  ") | color(Color::GrayLight),
        text("[t]") | bold | color(Color::White),
        text(" theme: ") | color(Color::GrayLight),
        text(name) | bold | color(theme.primary),
    }) | bgcolor(theme.hints_bg);
}
