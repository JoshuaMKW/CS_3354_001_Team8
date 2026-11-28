#pragma once

#include "data-api/Database.h"
#include "schedule/ScheduleSharing.h"

namespace cramberry {

class SqlScheduleShareRepository : public IScheduleShareRepository {
public:
    explicit SqlScheduleShareRepository(const data::Database& db) : db_(db) {}

    std::optional<ScheduleShare> findForOwnerAndRecipient(
        const std::string& ownerId, const std::string& recipientId) const override;
    std::optional<ScheduleShare> findForRecipient(
        const std::string& shareId, const std::string& recipientId) const override;
    std::vector<ScheduleShare> listForOwner(const std::string& ownerId) const override;
    std::vector<ScheduleShare> listForRecipient(const std::string& recipientId) const override;
    std::pair<ScheduleShare, bool> create(const std::string& ownerId,
                                          const std::string& recipientId,
                                          Timestamp createdAt) override;
    bool removeForOwner(const std::string& ownerId, const std::string& shareId) override;

private:
    const data::Database& db_;
};

}  // namespace cramberry
