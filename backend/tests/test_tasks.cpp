// Academic task management: courses, tasks, status (UC-13), filters (FR-24),
// overdue (FR-26), weekly report (UC-14, FR-30) and per-student isolation (NFR-19).

#include <gmock/gmock.h>

#include "tests/ApiFixture.h"

namespace cramberry {
namespace {

using namespace cramberry::testing;

class Tasks : public ApiFixture {
protected:
    std::string alice = addStudent("alice@utd.edu");
    std::string bob = addStudent("bob@utd.edu");
};

TEST_F(Tasks, CreateValidatesInput) {
    Task draft;
    draft.title = "Essay";
    draft.dueAt = kNow + kDay;

    Task noTitle = draft;
    noTitle.title.clear();
    EXPECT_EQ(tasks.createTask(alice, noTitle, kNow).error().code, ErrorCode::InvalidInput);

    Task badPriority = draft;
    badPriority.priority = 7;
    EXPECT_EQ(tasks.createTask(alice, badPriority, kNow).error().code, ErrorCode::InvalidInput);

    Task badEstimate = draft;
    badEstimate.estimatedMinutes = 0;
    EXPECT_EQ(tasks.createTask(alice, badEstimate, kNow).error().code, ErrorCode::InvalidInput);

    Task unknownCourse = draft;
    unknownCourse.courseId = "no-such-course";
    EXPECT_EQ(tasks.createTask(alice, unknownCourse, kNow).error().code, ErrorCode::InvalidInput);

    const auto created = tasks.createTask(alice, draft, kNow);
    ASSERT_TRUE(created.ok());
    EXPECT_EQ(created.value().status, TaskStatus::NotStarted);
    EXPECT_EQ(created.value().createdAt, kNow);
    EXPECT_EQ(tasks.getTask(alice, created.value().id).value().title, "Essay");
}

TEST_F(Tasks, StudentsNeverSeeOrChangeEachOthersTasks) {
    const auto mine = addTask(alice, "Alice's lab", kNow + kDay);

    EXPECT_EQ(tasks.getTask(bob, mine.id).error().code, ErrorCode::NotFound);
    EXPECT_EQ(tasks.setStatus(bob, mine.id, TaskStatus::Completed, kNow).error().code, ErrorCode::NotFound);
    EXPECT_EQ(tasks.deleteTask(bob, mine.id).error().code, ErrorCode::NotFound);
    Task forged = mine;
    forged.title = "Hacked";
    EXPECT_EQ(tasks.updateTask(bob, forged, kNow).error().code, ErrorCode::NotFound);

    EXPECT_TRUE(tasks.listTasks(bob, {}, false, kNow).empty());
    EXPECT_EQ(tasks.getTask(alice, mine.id).value().title, "Alice's lab");
    EXPECT_EQ(tasks.getTask(alice, mine.id).value().status, TaskStatus::NotStarted);
}

TEST_F(Tasks, CompletingSetsCompletedAtAndReopeningClearsIt) {
    const auto task = addTask(alice, "Quiz prep", kNow + kDay);
    const auto done = tasks.setStatus(alice, task.id, TaskStatus::Completed, kNow + kHour);
    ASSERT_TRUE(done.ok());
    EXPECT_EQ(done.value().completedAt, kNow + kHour);

    // Editing a completed task keeps the original completion time.
    Task edited = done.value();
    edited.notes = "Went well";
    EXPECT_EQ(tasks.updateTask(alice, edited, kNow + 2 * kHour).value().completedAt, kNow + kHour);

    const auto reopened = tasks.setStatus(alice, task.id, TaskStatus::InProgress, kNow + 3 * kHour);
    EXPECT_FALSE(reopened.value().completedAt.has_value());
    EXPECT_FALSE(tasks.getTask(alice, task.id).value().completedAt.has_value());
}

TEST_F(Tasks, FiltersByCourseDateStatusAndOverdue) {
    const auto course = tasks.createCourse(alice, {"Software Engineering", "CS 3354", "Fall 2026"});
    ASSERT_TRUE(course.ok());
    Task draft;
    draft.title = "SE homework";
    draft.dueAt = kNow + 2 * kDay;
    draft.courseId = course.value().id;
    ASSERT_TRUE(tasks.createTask(alice, draft, kNow).ok());
    const auto late = addTask(alice, "Late reading", kNow - kDay);
    const auto later = addTask(alice, "Final project", kNow + 20 * kDay);
    ASSERT_TRUE(tasks.setStatus(alice, later.id, TaskStatus::InProgress, kNow).ok());

    TaskFilter byCourse;
    byCourse.courseId = course.value().id;
    ASSERT_EQ(tasks.listTasks(alice, byCourse, false, kNow).size(), 1u);

    TaskFilter thisWeek;
    thisWeek.dueFrom = kNow - kHour;
    thisWeek.dueTo = kNow + 7 * kDay;
    ASSERT_EQ(tasks.listTasks(alice, thisWeek, false, kNow).size(), 1u);

    TaskFilter inProgress;
    inProgress.status = TaskStatus::InProgress;
    ASSERT_EQ(tasks.listTasks(alice, inProgress, false, kNow).size(), 1u);

    const auto overdue = tasks.listTasks(alice, {}, true, kNow);
    ASSERT_EQ(overdue.size(), 1u);
    EXPECT_EQ(overdue[0].id, late.id);

    // Sorted by due date.
    const auto all = tasks.listTasks(alice, {}, false, kNow);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all.front().id, late.id);
    EXPECT_EQ(all.back().id, later.id);
}

TEST_F(Tasks, DeletingACourseKeepsItsTasks) {
    const auto course = tasks.createCourse(alice, {"Algorithms", "CS 4349", ""});
    Task draft;
    draft.title = "Chapter 6";
    draft.dueAt = kNow + kDay;
    draft.courseId = course.value().id;
    const auto task = tasks.createTask(alice, draft, kNow).value();

    EXPECT_EQ(tasks.deleteCourse(bob, course.value().id).error().code, ErrorCode::NotFound);
    ASSERT_TRUE(tasks.deleteCourse(alice, course.value().id).ok());
    EXPECT_TRUE(tasks.listCourses(alice).empty());
    EXPECT_EQ(tasks.getTask(alice, task.id).value().courseId, "");
}

TEST_F(Tasks, WeeklyReportSplitsCompletedOverdueAndRemaining) {
    const Timestamp monday = kNow - 12 * kHour;  // 2026-11-23 00:00
    const auto done = addTask(alice, "Done", monday + kDay);
    ASSERT_TRUE(tasks.setStatus(alice, done.id, TaskStatus::Completed, kNow).ok());
    const auto late = addTask(alice, "Late", kNow - kHour);
    const auto upcoming = addTask(alice, "Upcoming", kNow + 3 * kDay);
    addTask(alice, "Next week", monday + 8 * kDay);

    const auto report = tasks.weeklyReport(alice, monday, kNow).value();
    ASSERT_EQ(report.completed.size(), 1u);
    EXPECT_EQ(report.completed[0].id, done.id);
    ASSERT_EQ(report.overdue.size(), 1u);
    EXPECT_EQ(report.overdue[0].id, late.id);
    ASSERT_EQ(report.remaining.size(), 1u);
    EXPECT_EQ(report.remaining[0].id, upcoming.id);
    EXPECT_EQ(report.averageCompletionMinutes, 0) << "created and completed at kNow";
}

}  // namespace
}  // namespace cramberry
