// AA tests (Ahnaf Adib): UC-10, UC-11, UC-12, NFR-18 and NFR-19.
// TC-10-01, TC-11-01, TC-12-01, TC-NFR18-01, TC-NFR19-01. See docs/testing/AA/test_cases.md.
// The Task Management System, Schedule System, Supabase and Gemini are replaced by the in-memory
// fakes below, so the tests need no network and never touch real student data.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "agent/ScheduleGenerationService.h"
#include "api/SchedulingRequestController.h"
#include "api/StudentDataController.h"

namespace cramberry {

// Readable GoogleTest failure output.
inline void PrintTo(const FieldError& e, std::ostream* os) { *os << e.field << ": " << e.message; }
inline void PrintTo(const ScheduledEvent& e, std::ostream* os) {
    *os << e.assignmentId << " \"" << e.title << "\" " << formatTimestamp(e.start) << " - "
        << formatTimestamp(e.end);
}

namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// ---------------------------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------------------------

class InMemoryTaskSource : public ITaskDetailsSource {
public:
    std::vector<TaskDetails> rows;

    std::vector<TaskDetails> getTaskDetails(const std::string& studentId,
                                            const std::vector<std::string>& taskIds) const override {
        std::vector<TaskDetails> found;
        for (const auto& row : rows) {
            if (row.studentId == studentId &&
                std::find(taskIds.begin(), taskIds.end(), row.id) != taskIds.end()) {
                found.push_back(row);
            }
        }
        return found;
    }
};

// Records every request it is given instead of scheduling it.
class RecordingScheduleSystem : public IScheduleSystem {
public:
    std::vector<PreparedSchedulingRequest> forwarded;

    std::optional<std::string> forwardRequest(const PreparedSchedulingRequest& request) override {
        forwarded.push_back(request);
        return "req-" + std::to_string(forwarded.size());
    }
};

class InMemoryStudentDataRepository : public IStudentDataRepository {
public:
    std::map<std::string, StudentRecord> rows;
    int writes = 0;  // successful writes

    std::optional<StudentRecord> findById(const std::string& recordId) const override {
        const auto it = rows.find(recordId);
        if (it == rows.end()) return std::nullopt;
        return it->second;
    }
    std::vector<StudentRecord> listByOwner(const std::string& ownerId, RecordType type) const override {
        std::vector<StudentRecord> found;
        for (const auto& [id, row] : rows) {
            if (row.ownerId == ownerId && row.type == type) found.push_back(row);
        }
        return found;
    }
    std::optional<std::string> insert(const std::string& ownerId, RecordType type,
                                      const FieldMap& fields) override {
        const std::string id = "rec-new-" + std::to_string(++nextId_);
        rows[id] = {id, ownerId, type, fields};
        ++writes;
        return id;
    }
    bool update(const StudentRecord& record) override {
        if (!rows.contains(record.id)) return false;
        rows[record.id] = record;
        ++writes;
        return true;
    }

private:
    int nextId_ = 0;
};

// Plays the part of Gemini by returning a fixed reply.
class CannedGeminiClient : public IGeminiClient {
public:
    std::string reply;

    std::string sendPrompt(const PreparedSchedulingRequest&) override { return reply; }
};

class InMemoryGeneratedScheduleRepository : public IGeneratedScheduleRepository {
public:
    std::map<std::string, std::vector<ScheduledEvent>> schedules;
    int saves = 0;  // successful saves

    std::vector<ScheduledEvent> load(const std::string& studentId) const override {
        const auto it = schedules.find(studentId);
        return it == schedules.end() ? std::vector<ScheduledEvent>{} : it->second;
    }
    bool saveSchedule(const std::string& studentId, const std::vector<ScheduledEvent>& events) override {
        schedules[studentId] = events;
        ++saves;
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// Shared test data
// ---------------------------------------------------------------------------------------------

const std::string kStudent = "student-a";
const std::string kOtherStudent = "student-b";
const Timestamp kNow = makeTimestamp(2026, 11, 16, 12, 0);  // Mon 16 Nov 2026, 12:00 UTC

Timestamp nov(unsigned day, int hour, int minute = 0) {
    return makeTimestamp(2026, 11, day, hour, minute);
}

StudyPreferences weekdayEvenings() {
    StudyPreferences prefs;  // defaults: 180 min/day, 60 min sessions, 15 min breaks
    for (Day d : {Day::Mon, Day::Tue, Day::Wed, Day::Thu, Day::Fri}) {
        prefs.studyHours.push_back({d, hm(16), hm(21)});
    }
    return prefs;
}

// ---------------------------------------------------------------------------------------------
// Functional tests
// ---------------------------------------------------------------------------------------------

// TC-10-01 | Valid Request Is Accepted (UC-10, Successful Flow)
// FR-27: the API receives and validates tasks, deadlines and preferences, then forwards them.
TEST(AATests, TC_10_01_ValidRequestIsAccepted) {
    InMemoryTaskSource tasks;
    tasks.rows = {
        {"t-essay", kStudent, "HIST 1301 Essay", nov(20, 23, 59), 240, false},
        {"t-quiz", kStudent, "CS 3354 Quiz 4", nov(18, 9), 90, false},
    };
    RecordingScheduleSystem scheduler;
    SchedulingRequestController controller{tasks, scheduler};
    const SchedulingRequestInput input{
        {{"t-essay", "2026-11-20T23:59", 240}, {"t-quiz", "2026-11-18T09:00", 120}}, weekdayEvenings()};

    const ProcessResult result = controller.processRequest(kStudent, input, kNow);

    EXPECT_EQ(result.httpStatus, 202);
    EXPECT_EQ(result.message, "Building your schedule");
    EXPECT_EQ(result.requestId, "req-1");
    EXPECT_THAT(result.fieldErrors, IsEmpty());

    ASSERT_EQ(scheduler.forwarded.size(), 1u);
    const PreparedSchedulingRequest& sent = scheduler.forwarded[0];
    EXPECT_EQ(sent.studentId, kStudent);
    EXPECT_EQ(sent.submittedAt, kNow);
    EXPECT_EQ(sent.prefs, weekdayEvenings());
    // prepareRequest(): earliest deadline first, and the student's new 120 min estimate wins.
    ASSERT_EQ(sent.tasks.size(), 2u);
    EXPECT_EQ(sent.tasks[0].id, "t-quiz");
    EXPECT_EQ(sent.tasks[0].estimatedMinutes, 120);
    EXPECT_EQ(sent.tasks[1].id, "t-essay");
    EXPECT_EQ(sent.tasks[1].dueUtc, nov(20, 23, 59));
}

// TC-11-01 | Save and Update Own Records (UC-11, Successful Flow)
// FR-28: changes are saved and still there when the record is read again.
TEST(AATests, TC_11_01_SaveAndUpdateOwnRecords) {
    InMemoryStudentDataRepository repo;
    repo.rows = {
        {"rec-a1", {"rec-a1", kStudent, RecordType::Assignment,
                    {{"title", "CS 3354 Project Phase 4"}, {"dueDate", "2026-11-20T23:59"},
                     {"estimatedMinutes", "240"}, {"status", "In Progress"}}}},
        {"rec-a2", {"rec-a2", kStudent, RecordType::Assignment,
                    {{"title", "MATH 2418 HW 6"}, {"dueDate", "2026-11-17T23:59"},
                     {"estimatedMinutes", "60"}, {"status", "Not Started"}}}},
        {"rec-b1", {"rec-b1", kOtherStudent, RecordType::Assignment,
                    {{"title", "PHYS 2325 Lab Report"}, {"dueDate", "2026-11-19T23:59"},
                     {"estimatedMinutes", "120"}, {"status", "Not Started"}}}},
    };
    StudentDataController controller{repo};
    const StudentRecord otherBefore = repo.rows.at("rec-b1");

    const DataResult updated =
        controller.updateRecord(kStudent, "rec-a1", {{"status", "Completed"}, {"estimatedMinutes", "180"}});
    EXPECT_EQ(updated.httpStatus, 200);
    EXPECT_EQ(updated.message, "Saved.");

    // "Reload": read the record back from storage.
    const DataResult reloaded = controller.getRecord(kStudent, "rec-a1");
    ASSERT_EQ(reloaded.records.size(), 1u);
    EXPECT_EQ(reloaded.records[0].fields.at("status"), "Completed");
    EXPECT_EQ(reloaded.records[0].fields.at("estimatedMinutes"), "180");
    EXPECT_EQ(reloaded.records[0].fields.at("title"), "CS 3354 Project Phase 4");

    const DataResult created = controller.createRecord(
        kStudent, RecordType::Assignment, {{"title", "CS 3354 Quiz 5"}, {"dueDate", "2026-11-23T09:00"}});
    EXPECT_EQ(created.httpStatus, 201);
    EXPECT_EQ(created.message, "Saved.");
    ASSERT_EQ(created.records.size(), 1u);
    EXPECT_EQ(created.records[0].ownerId, kStudent);
    EXPECT_EQ(controller.listRecords(kStudent, RecordType::Assignment).records.size(), 3u);

    EXPECT_EQ(repo.writes, 2);
    EXPECT_EQ(repo.rows.at("rec-b1"), otherBefore);
}

// TC-12-01 | Schedule That Breaks the Rules Is Rejected (UC-12, Invalid Input)
// FR-29: well-formed JSON is still rejected when the schedule itself is wrong, and nothing is saved.
TEST(AATests, TC_12_01_ScheduleThatBreaksRulesIsRejected) {
    CannedGeminiClient gemini;
    InMemoryGeneratedScheduleRepository schedules;
    ScheduleGenerationService service{gemini, schedules};
    const std::vector<ScheduledEvent> existing = {{"t-old", "Last week's plan", nov(15, 18), nov(15, 19)}};
    schedules.schedules[kStudent] = existing;

    PreparedSchedulingRequest request;
    request.studentId = kStudent;
    request.submittedAt = kNow;
    request.tasks = {
        {"t-quiz", kStudent, "CS 3354 Quiz 4", nov(18, 9), 120, false},
        {"t-essay", kStudent, "HIST 1301 Essay", nov(20, 23, 59), 240, false},
    };

    gemini.reply = R"({"events": [
      {"assignmentId": "t-quiz",  "title": "Quiz review", "start": "2026-11-18T08:00", "end": "2026-11-18T10:00"},
      {"assignmentId": "t-quiz",  "title": "Cram",        "start": "2026-11-16T18:00", "end": "2026-11-16T19:00"},
      {"assignmentId": "t-essay", "title": "Draft",       "start": "2026-11-16T18:30", "end": "2026-11-16T19:30"},
      {"assignmentId": "t-lab",   "title": "Lab",         "start": "2026-11-17T18:00", "end": "2026-11-17T19:00"},
      {"assignmentId": "t-essay", "title": "Edit",        "start": "2026-11-17T21:00", "end": "2026-11-17T20:00"},
      {"assignmentId": "t-essay", "title": "Too early",   "start": "2026-11-15T10:00", "end": "2026-11-15T11:00"}
    ]})";

    const GenerationResult result = service.generateSchedule(request);

    EXPECT_EQ(result.status, GenerationResult::Status::Rejected);
    EXPECT_EQ(result.message, "The AI returned an invalid schedule, so it wasn't saved. Please try again.");
    EXPECT_THAT(result.reasons, ElementsAre("Event 1 ends after \"CS 3354 Quiz 4\" is due",
                                            "Event 4 refers to unknown assignment \"t-lab\"",
                                            "Event 5 ends before it starts",
                                            "Event 6 starts in the past",
                                            "Event 3 overlaps event 2"));
    EXPECT_EQ(schedules.load(kStudent), existing);
    EXPECT_EQ(schedules.saves, 0);
}

// ---------------------------------------------------------------------------------------------
// Non-functional tests
// ---------------------------------------------------------------------------------------------

// TC-NFR18-01 | Backend API Response Time
// NFR-18: at least 95% of standard backend API requests finish within 2 seconds (AI generation
// excluded). Runs 1,000 mixed UC-10/UC-11/UC-12 requests and checks the 95th percentile.
// Note: in-memory fakes stand in for Supabase, so this measures the backend's own processing time,
// not network or database latency.
TEST(AATests, TC_NFR18_01_BackendRequestsFinishWithin2Seconds) {
    InMemoryTaskSource tasks;
    RecordingScheduleSystem scheduler;
    SchedulingRequestController scheduling{tasks, scheduler};
    InMemoryStudentDataRepository repo;
    StudentDataController data{repo};

    for (int i = 0; i < 40; ++i) {
        const std::string id = "t-" + std::to_string(i);
        tasks.rows.push_back({id, kStudent, "Task " + std::to_string(i), nov(20, 23, 59), 60, false});
        repo.rows[id] = {id, kStudent, RecordType::Assignment,
                         {{"title", "Task " + std::to_string(i)}, {"dueDate", "2026-11-20T23:59"},
                          {"estimatedMinutes", "60"}, {"status", "Not Started"}}};
    }
    SchedulingRequestInput request{{}, weekdayEvenings()};
    for (int i = 0; i < 40; ++i) request.tasks.push_back({"t-" + std::to_string(i), "2026-11-20T23:59", 60});

    PreparedSchedulingRequest prepared{kStudent, tasks.rows, weekdayEvenings(), kNow};
    std::string reply = R"({"events": [)";
    for (int i = 0; i < 40; ++i) {
        char event[200];
        std::snprintf(event, sizeof event,
                      R"(%s{"assignmentId": "t-%d", "start": "2026-11-%02dT%02d:00", "end": "2026-11-%02dT%02d:45"})",
                      i == 0 ? "" : ",", i, 17 + i / 10, 8 + i % 10, 17 + i / 10, 8 + i % 10);
        reply += event;
    }
    reply += "]}";

    constexpr int kRequests = 1000;
    std::vector<double> millis;
    millis.reserve(kRequests);
    for (int i = 0; i < kRequests; ++i) {
        const auto start = std::chrono::steady_clock::now();
        switch (i % 4) {
            case 0: ASSERT_EQ(scheduling.processRequest(kStudent, request, kNow).httpStatus, 202); break;
            case 1: ASSERT_EQ(data.listRecords(kStudent, RecordType::Assignment).httpStatus, 200); break;
            case 2: ASSERT_EQ(data.updateRecord(kStudent, "t-1", {{"status", "In Progress"}}).httpStatus, 200); break;
            case 3: ASSERT_TRUE(validateAIResponse(reply, prepared).valid); break;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        millis.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
    }

    std::sort(millis.begin(), millis.end());
    const double p95 = millis[static_cast<std::size_t>(std::ceil(0.95 * kRequests)) - 1];
    std::cout << "[ NFR-18   ] p95 = " << p95 << " ms, max = " << millis.back() << " ms over "
              << kRequests << " requests\n";
    ::testing::Test::RecordProperty("p95_ms", std::to_string(p95));
    EXPECT_LT(p95, 2000.0);
}

// TC-NFR19-01 | Students Can't Reach Each Other's Data
// NFR-19: database operations prevent access to other students' information.
TEST(AATests, TC_NFR19_01_StudentsCantReachEachOthersData) {
    InMemoryStudentDataRepository repo;
    StudentDataController data{repo};
    const std::vector<std::string> students = {"student-a", "student-b", "student-c"};
    for (const auto& s : students) {
        for (int i = 0; i < 3; ++i) {
            const std::string id = s + "-rec-" + std::to_string(i);
            repo.rows[id] = {id, s, RecordType::Assignment,
                             {{"title", "Private " + id}, {"dueDate", "2026-11-20T23:59"}}};
        }
    }
    const auto before = repo.rows;

    int attempts = 0;
    for (const auto& reader : students) {
        for (const auto& [id, row] : before) {
            if (row.ownerId == reader) continue;
            ++attempts;
            EXPECT_EQ(data.getRecord(reader, id).httpStatus, 403) << reader << " read " << id;
            EXPECT_EQ(data.updateRecord(reader, id, {{"title", "Changed"}}).httpStatus, 403)
                << reader << " updated " << id;
            EXPECT_EQ(data.updateRecord(reader, reader + "-rec-0", {{"ownerId", row.ownerId}}).httpStatus, 400)
                << reader << " tried to hand a record to " << row.ownerId;
        }
        for (const auto& record : data.listRecords(reader, RecordType::Assignment).records) {
            EXPECT_EQ(record.ownerId, reader);
        }
    }
    EXPECT_EQ(attempts, 18);
    EXPECT_EQ(repo.rows, before);
    EXPECT_EQ(repo.writes, 0);

    // UC-10 can't be used to schedule another student's task either.
    InMemoryTaskSource tasks;
    tasks.rows = {{"t-b", "student-b", "B's essay", nov(20, 23, 59), 60, false}};
    RecordingScheduleSystem scheduler;
    SchedulingRequestController scheduling{tasks, scheduler};
    EXPECT_EQ(scheduling.processRequest("student-a", {{{"t-b", "2026-11-20T23:59", 60}}, weekdayEvenings()}, kNow)
                  .httpStatus,
              400);
    EXPECT_TRUE(scheduler.forwarded.empty());

    // Even if a broken query hands back another student's row, UC-10 drops it.
    struct LeakyTaskSource : ITaskDetailsSource {
        std::vector<TaskDetails> rows;
        std::vector<TaskDetails> getTaskDetails(const std::string&,
                                                const std::vector<std::string>&) const override {
            return rows;  // ignores the student ID on purpose
        }
    } leaky;
    leaky.rows = tasks.rows;
    SchedulingRequestController guarded{leaky, scheduler};
    EXPECT_EQ(guarded.processRequest("student-a", {{{"t-b", "2026-11-20T23:59", 60}}, weekdayEvenings()}, kNow)
                  .httpStatus,
              400);
    EXPECT_TRUE(scheduler.forwarded.empty());
}

}  // namespace
}  // namespace cramberry
