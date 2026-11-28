#include "schedule/ViewSchedule.h"
#include <algorithm>
#include <exception>
namespace cramberry {
ViewScheduleResult ViewSchedule::view(const std::string& studentId, Timestamp fromUtc, Timestamp toUtc) const {
    if (studentId.empty()) return {false, "Authentication required", {}, {}};
    if (fromUtc >= toUtc) return {false, "Invalid date range", {}, {}};
    try {
        auto blocks = schedule_.getActiveStudyItems(studentId);
        auto events = deadlines_.list(studentId);
        events.erase(std::remove_if(events.begin(), events.end(), [&](const DeadlineEvent& e) {
            return e.dueUtc < fromUtc || e.dueUtc >= toUtc;
        }), events.end());
        std::sort(events.begin(), events.end(), [](const DeadlineEvent& a, const DeadlineEvent& b) {
            return a.dueUtc == b.dueUtc ? a.id < b.id : a.dueUtc < b.dueUtc;
        });
        return {true, blocks.empty() && events.empty() ? "No events scheduled for this period" : "Schedule loaded",
                std::move(blocks), std::move(events)};
    } catch (const std::exception&) {
        return {false, "Schedule unavailable; please retry", {}, {}};
    } catch (...) {
        return {false, "Schedule unavailable; please retry", {}, {}};
    }
}
} // namespace cramberry
