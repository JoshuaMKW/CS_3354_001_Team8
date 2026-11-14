#include <gtest/gtest.h>
#include "schedule/UploadSchedule.h"
#include "schedule/ViewSchedule.h"
#include <map>
#include <stdexcept>
using namespace cramberry;
namespace {
struct MemoryDeadlines : IDeadlineRepository {
    std::map<std::string, std::vector<DeadlineEvent>> data;
    bool fail = false;
    std::vector<DeadlineEvent> list(const std::string& student) const override {
        if (fail) throw std::runtime_error("db down");
        auto it = data.find(student);
        return it == data.end() ? std::vector<DeadlineEvent>{} : it->second;
    }
    void append(const std::string& student, const std::vector<DeadlineEvent>& events) override {
        if (fail) throw std::runtime_error("db down");
        auto updated = data[student];
        updated.insert(updated.end(), events.begin(), events.end());
        data[student] = std::move(updated);
    }
};
struct MemorySchedule : IScheduleRepository {
    std::map<std::string, std::vector<StudyBlock>> data;
    std::vector<StudyBlock> getActiveStudyItems(const std::string& student) const override {
        auto it = data.find(student);
        return it == data.end() ? std::vector<StudyBlock>{} : it->second;
    }
    void replace(const std::string& student, std::vector<StudyBlock> blocks) override { data[student] = std::move(blocks); }
};
struct FakeExtractor : ISyllabusExtractor {
    std::vector<DeadlineEvent> reply;
    bool fail = false;
    std::vector<DeadlineEvent> extract(const SyllabusFile&) override {
        if (fail) throw std::runtime_error("AI timeout");
        return reply;
    }
};
SyllabusFile pdf() { return {"CS3345_Fall2026.pdf", {'%', 'P', 'D', 'F', '-', '1', '.', '7'}}; }
}
TEST(JOUpload, ImportsValidDeadlines) {
    MemoryDeadlines db;
    FakeExtractor ai;
    ai.reply = {{"p3", "Project 3", makeTimestamp(2026,11,20,23,59), ""}};
    auto result = UploadSchedule(ai,db).upload("studentA",pdf());
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(db.list("studentA").size(), 1u);
    EXPECT_EQ(db.list("studentA")[0].sourceFile, "CS3345_Fall2026.pdf");
}
TEST(JOUpload, NoDeadlinesLeavesExistingData) {
    MemoryDeadlines db;
    db.data["studentA"] = {{"old", "Existing", 1, ""}};
    FakeExtractor ai;
    EXPECT_TRUE(UploadSchedule(ai,db).upload("studentA",pdf()).ok);
    EXPECT_EQ(db.list("studentA").size(), 1u);
}
TEST(JOUpload, RejectsInvalidFile) {
    MemoryDeadlines db;
    FakeExtractor ai;
    EXPECT_FALSE(UploadSchedule(ai,db).upload("studentA",{"bad.exe",{1,2,3}}).ok);
    EXPECT_TRUE(db.list("studentA").empty());
}
TEST(JOUpload, AiTimeoutPreservesExistingData) {
    MemoryDeadlines db;
    db.data["studentA"] = {{"old", "Existing", 1, ""}};
    FakeExtractor ai;
    ai.fail = true;
    EXPECT_FALSE(UploadSchedule(ai,db).upload("studentA",pdf()).ok);
    EXPECT_EQ(db.list("studentA").size(), 1u);
}
TEST(JOView, ReturnsChronologicalDeadlines) {
    MemoryDeadlines db;
    MemorySchedule schedule;
    db.data["studentA"] = {{"later", "Later", 200, ""}, {"earlier", "Earlier", 100, ""}};
    auto result = ViewSchedule(schedule,db).view("studentA",50,300);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.deadlines.size(),2u);
    EXPECT_EQ(result.deadlines[0].id,"earlier");
}
TEST(JOView, EmptySchedule) {
    MemoryDeadlines db;
    MemorySchedule schedule;
    auto result = ViewSchedule(schedule,db).view("studentA",50,300);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.message,"No events scheduled for this period");
}
TEST(JOView, DoesNotExposeOtherStudentsEvents) {
    MemoryDeadlines db;
    MemorySchedule schedule;
    db.data["studentB"] = {{"private", "Private", 100, ""}};
    auto result = ViewSchedule(schedule,db).view("studentA",50,300);
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.deadlines.empty());
}
TEST(JOView, DatabaseOutageShowsErrorNotEmptyCalendar) {
    MemoryDeadlines db;
    MemorySchedule schedule;
    db.fail = true;
    auto result = ViewSchedule(schedule,db).view("studentA",50,300);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message,"Schedule unavailable; please retry");
}
