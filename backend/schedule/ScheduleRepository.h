#pragma once

#include <mutex>

#include "data-api/Database.h"
#include "schedule/Schedule.h"

namespace cramberry {

// The student's accepted study schedule (Schedule in the UML class diagram).
class SqlScheduleRepository : public IScheduleRepository {
public:
    explicit SqlScheduleRepository(const data::Database& db) : db_(db) {}

    std::vector<StudyBlock> getActiveStudyItems(const std::string& studentId) const override;
    // Swaps the whole schedule in one transaction: readers see the old one or the new one.
    void replace(const std::string& studentId, std::vector<StudyBlock> blocks) override;

private:
    const data::Database& db_;
    // Two replaces running at once (a request plus a background re-plan) would each
    // delete, then both insert, leaving duplicate blocks on PostgreSQL. One server
    // process serves all requests, so a process-wide lock is enough.
    std::mutex replaceMutex_;
};

}  // namespace cramberry
