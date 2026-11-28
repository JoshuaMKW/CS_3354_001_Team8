#include "schedule/ScheduleShareRepository.h"

#include <stdexcept>

#include "user/Crypto.h"

namespace cramberry {

namespace {

ScheduleShare toShare(const drogon::orm::Row& row) {
    ScheduleShare share;
    share.id = row["id"].as<std::string>();
    share.ownerId = row["owner_student_id"].as<std::string>();
    share.ownerName = row["owner_name"].as<std::string>();
    share.recipientId = row["recipient_student_id"].as<std::string>();
    share.recipientEmail = row["recipient_email"].as<std::string>();
    share.createdAt = row["created_at"].as<std::int64_t>();
    return share;
}

constexpr const char* kShareSelect =
    "SELECT ss.id, ss.owner_student_id, owner.name AS owner_name, "
    "ss.recipient_student_id, recipient.email AS recipient_email, ss.created_at "
    "FROM schedule_shares ss "
    "JOIN students owner ON owner.id = ss.owner_student_id "
    "JOIN students recipient ON recipient.id = ss.recipient_student_id ";

}  // namespace

std::optional<ScheduleShare> SqlScheduleShareRepository::findForOwnerAndRecipient(
    const std::string& ownerId, const std::string& recipientId) const {
    const auto rows = db_.exec(std::string(kShareSelect) +
                                   "WHERE ss.owner_student_id = $1 AND ss.recipient_student_id = $2",
                               ownerId, recipientId);
    if (rows.empty()) return std::nullopt;
    return toShare(rows[0]);
}

std::optional<ScheduleShare> SqlScheduleShareRepository::findForRecipient(
    const std::string& shareId, const std::string& recipientId) const {
    const auto rows = db_.exec(std::string(kShareSelect) +
                                   "WHERE ss.id = $1 AND ss.recipient_student_id = $2",
                               shareId, recipientId);
    if (rows.empty()) return std::nullopt;
    return toShare(rows[0]);
}

std::vector<ScheduleShare> SqlScheduleShareRepository::listForOwner(const std::string& ownerId) const {
    std::vector<ScheduleShare> result;
    for (const auto& row : db_.exec(std::string(kShareSelect) +
                                        "WHERE ss.owner_student_id = $1 ORDER BY ss.created_at DESC",
                                    ownerId)) {
        result.push_back(toShare(row));
    }
    return result;
}

std::vector<ScheduleShare> SqlScheduleShareRepository::listForRecipient(
    const std::string& recipientId) const {
    std::vector<ScheduleShare> result;
    for (const auto& row : db_.exec(std::string(kShareSelect) +
                                        "WHERE ss.recipient_student_id = $1 ORDER BY ss.created_at DESC",
                                    recipientId)) {
        result.push_back(toShare(row));
    }
    return result;
}

std::pair<ScheduleShare, bool> SqlScheduleShareRepository::create(const std::string& ownerId,
                                                                  const std::string& recipientId,
                                                                  Timestamp createdAt) {
    const std::string id = crypto::newId();
    const auto inserted = db_.exec(
        "INSERT INTO schedule_shares (id, owner_student_id, recipient_student_id, created_at) "
        "VALUES ($1, $2, $3, $4) ON CONFLICT (owner_student_id, recipient_student_id) DO NOTHING",
        id, ownerId, recipientId, static_cast<std::int64_t>(createdAt));
    const auto share = findForOwnerAndRecipient(ownerId, recipientId);
    if (!share) throw std::runtime_error("Created schedule share could not be read");
    return {*share, inserted.affectedRows() > 0};
}

bool SqlScheduleShareRepository::removeForOwner(const std::string& ownerId,
                                                const std::string& shareId) {
    return db_.exec("DELETE FROM schedule_shares WHERE owner_student_id = $1 AND id = $2",
                    ownerId, shareId)
               .affectedRows() > 0;
}

}  // namespace cramberry
