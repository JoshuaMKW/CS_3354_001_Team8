#include "schedule/NKScheduleEditor.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "schedule/NKScheduleRules.h"

namespace cramberry {

NKScheduleEditor::NKScheduleEditor(IScheduleRepository& schedules) : schedules_(schedules) {}

NKEditResult NKScheduleEditor::previewMove(const std::vector<StudyBlock>& proposal,
                                           const NKMoveRequest& request,
                                           const StudyPreferences& preferences,
                                           const std::vector<Assignment>& assignments) const {
    if (request.studentId.empty()) return {false, {}, {"Authenticated student ID is required"}};
    if (request.assignmentId.empty()) return {false, {}, {"Assignment ID is required"}};
    std::vector<StudyBlock> changed = proposal;
    const auto matches = std::count_if(changed.begin(), changed.end(), [&](const StudyBlock& b) {
        return b.assignmentId == request.assignmentId && b.time == request.originalTime;
    });
    if (matches != 1) return {false, {}, {"Could not uniquely locate the original study block"}};

    for (auto& b : changed) {
        if (b.assignmentId == request.assignmentId && b.time == request.originalTime) {
            b.time = request.updatedTime;
            if (!request.updatedTitle.empty()) b.title = request.updatedTitle;
            break;
        }
    }
    auto errors = validateNKSchedule(changed, preferences, assignments);
    if (!errors.empty()) return {false, {}, std::move(errors)};
    return {true, std::move(changed), {}};
}

NKEditResult NKScheduleEditor::moveAcceptedBlock(const NKMoveRequest& request,
                                                 const StudyPreferences& preferences,
                                                 const std::vector<Assignment>& assignments) {
    if (request.studentId.empty()) return {false, {}, {"Authenticated student ID is required"}};
    try {
        // Repository must enforce ownership; no cross-student schedule is accepted.
        const auto current = schedules_.getActiveStudyItems(request.studentId);
        auto proposed = previewMove(current, request, preferences, assignments);
        if (!proposed.ok) return proposed;
        schedules_.replace(request.studentId, proposed.schedule);
        return proposed;
    } catch (const std::exception&) {
        return {false, {}, {"Could not load or save this student's study schedule"}};
    }
}

}  // namespace cramberry
