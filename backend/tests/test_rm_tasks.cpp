// Risha Mothukuru (RM): executable backend tests for UC-13 and UC-14.
#include <gtest/gtest.h>
#include <map>
#include <stdexcept>
#include "tasks/TaskController.h"
namespace cramberry {
namespace {
TaskTime at(int day,int hour=0) {
    return TaskTime{std::chrono::sys_days{std::chrono::year{2026}/11/day}}+std::chrono::hours{hour};
}
class TaskFake : public ITaskRepository {
public:
    std::map<std::string,Task> rows;
    bool failWrite=false, failRead=false;
    int writes=0;
    std::optional<Task> find(const std::string& student,const std::string& id) const override {
        if(failRead) throw std::runtime_error("database unavailable");
        auto it=rows.find(id);
        if(it==rows.end() || it->second.studentId!=student) return {};
        return it->second;
    }
    bool save(const Task& task) override {
        ++writes;
        if(failWrite) return false;
        rows[task.id]=task;return true;
    }
    std::vector<Task> list(const std::string& student) const override {
        if(failRead) throw std::runtime_error("database unavailable");
        std::vector<Task> result;
        for(const auto& [id,t]:rows) if(t.studentId==student) result.push_back(t);
        return result;
    }
};
class RMTasks : public ::testing::Test {
protected:
    TaskFake db;
    TaskController controller{db};
    TaskTime now=at(22,12);
    void SetUp() override {db.rows["T1"]={"T1","rm","Project Report","Not Started",at(20),{}};}
};
TEST_F(RMTasks, TC_13_01_UpdateAndReload) {
    auto result=controller.updateStatus("rm","T1","In Progress",now);
    ASSERT_TRUE(result.ok);EXPECT_EQ(result.message,"Task status updated successfully.");
    auto reloaded=db.find("rm","T1");ASSERT_TRUE(reloaded);EXPECT_EQ(reloaded->status,"In Progress");
    EXPECT_EQ(reloaded->title,"Project Report");EXPECT_EQ(reloaded->due,at(20));
}
TEST_F(RMTasks, TC_13_02_ReopenCompletedTask) {
    db.rows["T1"].status="Completed";db.rows["T1"].completedAt=at(21);
    ASSERT_TRUE(controller.updateStatus("rm","T1","Not Started",now).ok);
    EXPECT_EQ(db.rows.at("T1").status,"Not Started");EXPECT_FALSE(db.rows.at("T1").completedAt);
}
TEST_F(RMTasks, TC_13_03_InvalidStatus) {
    db.rows["T1"].status="In Progress";
    auto r=controller.updateStatus("rm","T1","Almost Done",now);
    EXPECT_FALSE(r.ok);EXPECT_EQ(r.message,"Invalid task status. Choose Not Started, In Progress, or Completed.");
    EXPECT_EQ(db.rows.at("T1").status,"In Progress");EXPECT_EQ(db.writes,0);
}
TEST_F(RMTasks, TC_13_04_FailedSavePreservesStatus) {
    db.failWrite=true;auto r=controller.updateStatus("rm","T1","Completed",now);
    EXPECT_FALSE(r.ok);EXPECT_EQ(r.message,"Unable to update task status. Please try again.");
    EXPECT_EQ(db.rows.at("T1").status,"Not Started");EXPECT_FALSE(db.rows.at("T1").completedAt);
    db.failWrite=false;EXPECT_TRUE(controller.updateStatus("rm","T1","Completed",now).ok);
}
TEST_F(RMTasks, TC_14_01_WeeklySummary) {
    db.rows.clear();
    db.rows["A"]={"A","rm","A","Completed",at(18),at(17)};
    db.rows["B"]={"B","rm","B","Completed",at(20),at(20)};
    db.rows["C"]={"C","rm","C","In Progress",at(19),{}};
    db.rows["D"]={"D","rm","D","Not Started",at(21),{}};
    db.rows["E"]={"E","rm","E","Not Started",at(22,23)+std::chrono::minutes{59},{}};
    auto r=controller.weeklyReport("rm","2026-11-16",now);
    ASSERT_TRUE(r.ok);ASSERT_EQ(r.completed.size(),2u);ASSERT_EQ(r.overdue.size(),2u);
    EXPECT_EQ(r.completed[0].id,"A");EXPECT_EQ(r.completed[1].id,"B");
    EXPECT_EQ(r.overdue[0].id,"C");EXPECT_EQ(r.overdue[1].id,"D");
}
TEST_F(RMTasks, TC_14_02_EmptyWeek) {
    db.rows.clear();auto r=controller.weeklyReport("rm","2026-11-02",now);
    ASSERT_TRUE(r.ok);EXPECT_TRUE(r.completed.empty());EXPECT_TRUE(r.overdue.empty());
    EXPECT_EQ(r.message,"No task activity for this week.");
}
TEST_F(RMTasks, TC_14_03_InvalidWeekDate) {
    auto r=controller.weeklyReport("rm","2026-02-30",now);
    EXPECT_FALSE(r.ok);EXPECT_EQ(r.message,"Invalid week date. Select a valid calendar date.");
    EXPECT_TRUE(controller.weeklyReport("rm","2026-11-16",now).ok);
}
TEST_F(RMTasks, TC_14_04_ReadFailureAndRecovery) {
    db.failRead=true;auto r=controller.weeklyReport("rm","2026-11-16",now);
    EXPECT_FALSE(r.ok);EXPECT_EQ(r.message,"Unable to load weekly progress report. Please try again.");
    db.failRead=false;r=controller.weeklyReport("rm","2026-11-16",now);
    ASSERT_TRUE(r.ok);EXPECT_EQ(r.overdue.size(),1u);
}
TEST_F(RMTasks, StudentIsolation) {
    EXPECT_FALSE(controller.updateStatus("other","T1","Completed",now).ok);
    EXPECT_EQ(db.writes,0);auto r=controller.weeklyReport("other","2026-11-16",now);
    ASSERT_TRUE(r.ok);EXPECT_TRUE(r.overdue.empty());
}
TEST_F(RMTasks, WeekAndDeadlineBoundaries) {
    db.rows.clear();
    db.rows["A"]={"A","rm","A","Completed",at(16),at(16)};
    db.rows["B"]={"B","rm","B","Completed",at(23),at(23)};
    db.rows["C"]={"C","rm","C","In Progress",now,{}};
    auto r=controller.weeklyReport("rm","2026-11-16",now);
    ASSERT_TRUE(r.ok);ASSERT_EQ(r.completed.size(),1u);EXPECT_EQ(r.completed[0].id,"A");EXPECT_TRUE(r.overdue.empty());
}
}
}
