#include "schedule/ScheduleController.h"

#include <algorithm>
#include <map>

#include "common/Json.h"
#include "schedule/NKScheduleEditor.h"
#include "user/Crypto.h"

namespace cramberry {

namespace {

// "2026-11-21T12:10:00Z" -> "20261121T121000Z"
std::string icsTime(Timestamp t) {
    std::string iso = json::formatIsoTimestamp(t);
    std::erase(iso, '-');
    std::erase(iso, ':');
    return iso;
}

std::string icsText(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\' || c == ';' || c == ',') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c != '\r') {
            out += c;
        }
    }
    return out;
}

// RFC 5545 lines are at most 75 bytes; longer ones continue on lines starting with a space.
void addLine(std::string& ics, const std::string& line) {
    std::size_t start = 0;
    std::size_t width = 75;
    while (line.size() - start > width) {
        std::size_t cut = start + width;
        while (cut > start && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;  // keep UTF-8 whole
        ics += line.substr(start, cut - start) + "\r\n ";
        start = cut;
        width = 74;  // the leading space counts
    }
    ics += line.substr(start) + "\r\n";
}

}  // namespace

Outcome<PlanningWeek> ScheduleController::week(const std::string& studentId, Timestamp now) const {
    const auto student = users_.findById(studentId);
    if (!student) return notFound("Account not found");
    return planningWeek(now, student->utcOffsetMinutes);
}

std::vector<StudyBlock> ScheduleController::current(const std::string& studentId) const {
    return schedules_.getActiveStudyItems(studentId);
}

Outcome<std::vector<StudyBlock>> ScheduleController::moveBlock(const std::string& studentId,
                                                               const BlockMove& move, Timestamp now) {
    const auto prefs = settings_.find(studentId);
    if (!prefs) return Error{ErrorCode::Unprocessable, "Set your study preferences first", {}};
    const auto weekOutcome = week(studentId, now);
    if (!weekOutcome) return weekOutcome.error();

    // Validate against the tasks the current blocks were planned for, so tasks added or
    // finished since the last generation don't block a simple move.
    const auto blocks = schedules_.getActiveStudyItems(studentId);
    std::map<std::string, Task> tasksById;
    for (const auto& t : tasks_.listTasks(studentId, {})) tasksById[t.id] = t;

    std::vector<Task> planned;
    std::vector<Assignment> orphans;
    for (const auto& b : blocks) {
        const bool seen =
            std::any_of(planned.begin(), planned.end(), [&](const Task& t) { return t.id == b.assignmentId; }) ||
            std::any_of(orphans.begin(), orphans.end(), [&](const Assignment& a) { return a.id == b.assignmentId; });
        if (seen) continue;
        if (auto it = tasksById.find(b.assignmentId); it != tasksById.end()) {
            planned.push_back(it->second);
        } else {
            orphans.push_back({b.assignmentId, b.title, Day::Sun, kMinutesPerDay, 0});  // task deleted since
        }
    }
    auto assignments = toAssignments(planned, weekOutcome.value());
    assignments.insert(assignments.end(), orphans.begin(), orphans.end());

    NKScheduleEditor editor(schedules_);
    const auto result = editor.moveAcceptedBlock(
        {studentId, move.assignmentId, move.from, move.to, move.title}, *prefs, assignments);
    if (!result.ok) {
        return Error{ErrorCode::Unprocessable, "That change doesn't fit your study preferences", result.errors};
    }
    return result.schedule;
}

void ScheduleController::clear(const std::string& studentId) { schedules_.replace(studentId, {}); }

Outcome<std::string> ScheduleController::exportIcs(const std::string& studentId, Timestamp now) const {
    const auto weekOutcome = week(studentId, now);
    if (!weekOutcome) return weekOutcome.error();
    const PlanningWeek& w = weekOutcome.value();
    const std::string stamp = icsTime(now);

    std::string ics;
    addLine(ics, "BEGIN:VCALENDAR");
    addLine(ics, "VERSION:2.0");
    addLine(ics, "PRODID:-//Cramberry//Study Planner//EN");
    addLine(ics, "CALSCALE:GREGORIAN");
    addLine(ics, "X-WR-CALNAME:Cramberry study plan");
    for (const auto& b : schedules_.getActiveStudyItems(studentId)) {
        const Timestamp start = w.toUtc(b.time.day, b.time.start);
        addLine(ics, "BEGIN:VEVENT");
        addLine(ics, "UID:" + crypto::sha256Hex(studentId + b.assignmentId + std::to_string(start)).substr(0, 24) +
                         "@cramberry");
        addLine(ics, "DTSTAMP:" + stamp);
        addLine(ics, "DTSTART:" + icsTime(start));
        addLine(ics, "DTEND:" + icsTime(w.toUtc(b.time.day, b.time.end)));
        addLine(ics, "SUMMARY:" + icsText("Study: " + b.title));
        addLine(ics, "END:VEVENT");
    }
    for (const auto& t : tasks_.listTasks(studentId, {})) {
        if (t.status == TaskStatus::Completed || t.dueAt < now) continue;
        addLine(ics, "BEGIN:VEVENT");
        addLine(ics, "UID:" + t.id + "@cramberry");
        addLine(ics, "DTSTAMP:" + stamp);
        addLine(ics, "DTSTART:" + icsTime(t.dueAt));
        addLine(ics, "SUMMARY:" + icsText("Due: " + t.title));
        if (!t.notes.empty()) addLine(ics, "DESCRIPTION:" + icsText(t.notes));
        addLine(ics, "END:VEVENT");
    }
    addLine(ics, "END:VCALENDAR");
    return ics;
}

Outcome<Dashboard> ScheduleController::dashboard(const std::string& studentId, Timestamp now) const {
    const auto student = users_.findById(studentId);
    if (!student) return notFound("Account not found");

    Dashboard d;
    d.student = *student;
    d.week = planningWeek(now, student->utcOffsetMinutes);
    d.today = d.week.toLocal(now).value_or(std::make_pair(Day::Mon, 0)).first;
    d.hasPreferences = settings_.find(studentId).has_value();
    d.schedule = schedules_.getActiveStudyItems(studentId);
    for (const auto& b : d.schedule) {
        if (b.time.day == d.today) d.todaysBlocks.push_back(b);
    }

    for (const auto& t : tasks_.listTasks(studentId, {})) {
        switch (t.status) {
            case TaskStatus::NotStarted: ++d.notStarted; break;
            case TaskStatus::InProgress: ++d.inProgress; break;
            case TaskStatus::Completed:
                if (t.completedAt && *t.completedAt >= d.week.startUtc) ++d.completedThisWeek;
                continue;
        }
        if (t.isOverdue(now)) {
            d.overdue.push_back(t);
        } else if (t.dueAt < now + 14 * kMinutesPerDay) {
            d.upcoming.push_back(t);
        }
    }
    std::stable_sort(d.upcoming.begin(), d.upcoming.end(), [](const Task& a, const Task& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.dueAt < b.dueAt;
    });
    if (d.upcoming.size() > 10) d.upcoming.resize(10);
    return d;
}

}  // namespace cramberry
