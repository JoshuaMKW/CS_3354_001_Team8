#pragma once
#include <string>
#include <vector>
#include "schedule/Schedule.h"
#include "schedule/UploadSchedule.h"

namespace cramberry {
struct ViewScheduleResult {
    bool ok = false;
    std::string message;
    std::vector<StudyBlock> weeklyStudyBlocks;
    std::vector<DeadlineEvent> deadlines;
};
class ViewSchedule {
public:
    ViewSchedule(const IScheduleRepository& schedule, const IDeadlineRepository& deadlines)
        : schedule_(schedule), deadlines_(deadlines) {}
    // UTC inclusive start, exclusive end. Authenticated studentId from server context.
    ViewScheduleResult view(const std::string& studentId, Timestamp fromUtc, Timestamp toUtc) const;
private:
    const IScheduleRepository& schedule_;
    const IDeadlineRepository& deadlines_;
};
} // namespace cramberry
