#pragma once

#include <zspace/zToolsets/fabrication/slicing/utils/SlicingTiming.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace alice2::slicing {

// Only computation/IO methods use this timer, never draw/update or UI waits.
class SlicerMethodTimer {
public:
    explicit SlicerMethodTimer(const char* name) : m_name(name) {}
    ~SlicerMethodTimer()
    {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_start).count();
        std::ostringstream line;
        line << "[Alice2 timing] " << m_name << " wall=" << std::fixed
             << std::setprecision(3) << ms << " ms\n";
        std::cout << line.str() << std::flush;
    }
    SlicerMethodTimer(const SlicerMethodTimer&) = delete;
    SlicerMethodTimer& operator=(const SlicerMethodTimer&) = delete;
private:
    const char* m_name;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

template<class Action>
decltype(auto) timeSlicerMethod(const char* name, Action&& action)
{
    const SlicerMethodTimer timer(name);
    return std::forward<Action>(action)();
}

inline void printSlicingTimingReport(std::ostream& out)
{
    auto records = zSpace::zTsUtils::getSlicingTimings();
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.totalMilliseconds > b.totalMilliseconds;
    });
    std::ostringstream report;
    report << "[Toolsets timing] Inclusive method totals, largest first. Parallel calls sum worker elapsed;\n"
              "[Toolsets timing] nested totals overlap and must not be added to get pipeline wall time.\n";
    report << std::fixed << std::setprecision(3);
    for (const auto& row : records)
        report << "[Toolsets timing] " << row.name << " calls=" << row.calls
               << (row.workerAggregate ? " worker-sum=" : " wall-sum=")
               << row.totalMilliseconds << " ms max=" << row.maxMilliseconds << " ms\n";
    out << report.str() << std::flush;
}

class SlicerTimingSession {
public:
    SlicerTimingSession() : m_wasEnabled(zSpace::zTsUtils::slicingTimingsEnabled())
    {
        zSpace::zTsUtils::clearSlicingTimings();
        zSpace::zTsUtils::setSlicingTimingsEnabled(true);
    }
    ~SlicerTimingSession()
    {
        zSpace::zTsUtils::setSlicingTimingsEnabled(m_wasEnabled);
        try { printSlicingTimingReport(std::cout); }
        catch (...) { std::cerr << "[Toolsets timing] Could not format timing report.\n"; }
    }
    SlicerTimingSession(const SlicerTimingSession&) = delete;
    SlicerTimingSession& operator=(const SlicerTimingSession&) = delete;
private:
    bool m_wasEnabled;
};

} // namespace alice2::slicing
