// Implemented by Risha Mothukuru (RM). No AI calls are needed for either use case.
#include "tasks/TaskController.h"
#include <algorithm>
#include <exception>
namespace cramberry {
namespace {
std::optional<TaskTime> parseDate(const std::string& s) {
    if(s.size()!=10 || s[4]!='-' || s[7]!='-') return {};
    for (unsigned i=0;i<s.size();++i)
        if(i!=4 && i!=7 && (s[i]<'0' || s[i]>'9')) return {};
    int y=std::stoi(s.substr(0,4)), m=std::stoi(s.substr(5,2)), d=std::stoi(s.substr(8,2));
    std::chrono::year_month_day date{std::chrono::year{y},std::chrono::month{unsigned(m)},std::chrono::day{unsigned(d)}};
    if(y<1 || !date.ok()) return {};
    return TaskTime{std::chrono::sys_days{date}};
}
}
TaskResult TaskController::updateStatus(const std::string& student,const std::string& id,
                                       const std::string& status, TaskTime now) {
    if(student.empty()) return {false,"Authentication required."};
    if(status!="Not Started" && status!="In Progress" && status!="Completed")
        return {false,"Invalid task status. Choose Not Started, In Progress, or Completed."};
    try {
        auto task=repository_.find(student,id);
        if(!task || task->studentId!=student) return {false,"Task not found."};
        if(status=="Completed") {
            if(task->status!="Completed" || !task->completedAt) task->completedAt=now;
        } else task->completedAt.reset();
        task->status=status;
        if(!repository_.save(*task)) return {false,"Unable to update task status. Please try again."};
        return {true,"Task status updated successfully."};
    } catch(const std::exception&) {
        return {false,"Unable to update task status. Please try again."};
    }
}
WeeklyReport TaskController::weeklyReport(const std::string& student,const std::string& date,TaskTime now) const {
    if(student.empty()) return {false,"Authentication required.",{}, {}};
    auto start=parseDate(date);
    if(!start) return {false,"Invalid week date. Select a valid calendar date.",{}, {}};
    auto end=*start+std::chrono::days{7};
    try {
        WeeklyReport report{true,"",{}, {}};
        for(const auto& t:repository_.list(student)) {
            if(t.studentId!=student) continue;
            if(t.status=="Completed" && t.completedAt && *t.completedAt>=*start && *t.completedAt<end && *t.completedAt<=now)
                report.completed.push_back(t);
            if(t.status!="Completed" && t.due>=*start && t.due<end && t.due<now)
                report.overdue.push_back(t);
        }
        auto order=[](const Task& a,const Task& b){return a.due==b.due ? a.id<b.id : a.due<b.due;};
        std::sort(report.completed.begin(),report.completed.end(),order);
        std::sort(report.overdue.begin(),report.overdue.end(),order);
        if(report.completed.empty() && report.overdue.empty()) report.message="No task activity for this week.";
        return report;
    } catch(const std::exception&) {
        return {false,"Unable to load weekly progress report. Please try again.",{}, {}};
    }
}
}
