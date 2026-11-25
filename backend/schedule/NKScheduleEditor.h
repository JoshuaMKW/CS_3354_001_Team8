#pragma once

// NK / UC-06: Modify AI-generated schedule (both before and after acceptance).
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "schedule/Schedule.h"

namespace cramberry {

struct NKMoveRequest {
    std::string studentId;  // Set by the authenticated backend, never trusted from chat.
    std::string assignmentId;
    TimeRange originalTime;
    TimeRange updatedTime;
    std::string updatedTitle;  // Empty means keep the original title.
};

struct NKEditResult {
    bool ok = false;
    std::vector<StudyBlock> schedule;
    std::vector<std::string> errors;
};

class NKScheduleEditor {
public:
    explicit NKScheduleEditor(IScheduleRepository& schedules);

    // Validates a preview without saving it: useful before accepting an AI proposal.
    NKEditResult previewMove(const std::vector<StudyBlock>& proposal,
                             const NKMoveRequest& request,
                             const StudyPreferences& preferences,
                             const std::vector<Assignment>& assignments) const;

    // Loads the student's accepted schedule and updates it if all checks pass.
    NKEditResult moveAcceptedBlock(const NKMoveRequest& request,
                                   const StudyPreferences& preferences,
                                   const std::vector<Assignment>& assignments);

private:
    IScheduleRepository& schedules_;
};

}  // namespace cramberry
