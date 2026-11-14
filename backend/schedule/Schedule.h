#pragma once

#include <string>
#include <vector>

#include "common/Time.h"

namespace cramberry {

// One block on a student's accepted study schedule.
struct StudyBlock {
    std::string title;
    std::string assignmentId;
    TimeRange time;

    bool operator==(const StudyBlock&) const = default;
};

// Schedule storage (Supabase in production, an in-memory fake in tests).
class IScheduleRepository {
public:
    virtual ~IScheduleRepository() = default;
    virtual std::vector<StudyBlock> getActiveStudyItems(const std::string& studentId) const = 0;
    virtual void replace(const std::string& studentId, std::vector<StudyBlock> blocks) = 0;
};

}  // namespace cramberry
