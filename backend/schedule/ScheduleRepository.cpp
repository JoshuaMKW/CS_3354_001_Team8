#include "schedule/ScheduleRepository.h"

#include "user/Crypto.h"

namespace cramberry {

std::vector<StudyBlock> SqlScheduleRepository::getActiveStudyItems(const std::string& studentId) const {
    std::vector<StudyBlock> blocks;
    for (const auto& row : db_.exec(
             "SELECT assignment_id, title, day, start_minute, end_minute FROM study_blocks "
             "WHERE student_id = $1 ORDER BY day, start_minute, sort_order",
             studentId)) {
        StudyBlock b;
        b.assignmentId = row["assignment_id"].as<std::string>();
        b.title = row["title"].as<std::string>();
        b.time = {static_cast<Day>(row["day"].as<int>()), row["start_minute"].as<int>(),
                  row["end_minute"].as<int>()};
        blocks.push_back(std::move(b));
    }
    return blocks;
}

void SqlScheduleRepository::replace(const std::string& studentId, std::vector<StudyBlock> blocks) {
    auto tx = db_.begin();
    tx.exec("DELETE FROM study_blocks WHERE student_id = $1", studentId);
    int order = 0;
    for (const auto& b : blocks) {
        tx.exec(
            "INSERT INTO study_blocks (id, student_id, assignment_id, title, day, start_minute, "
            "end_minute, sort_order) VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
            crypto::newId(), studentId, b.assignmentId, b.title, static_cast<int>(b.time.day),
            b.time.start, b.time.end, order++);
    }
    tx.commit();
}

}  // namespace cramberry
