#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/Outcome.h"
#include "common/Time.h"
#include "notifications/Notification.h"
#include "schedule/Schedule.h"
#include "user/User.h"

namespace cramberry {

struct ScheduleShare {
    std::string id;
    std::string ownerId;
    std::string ownerName;
    std::string recipientId;
    std::string recipientEmail;
    Timestamp createdAt = 0;
};

struct SharedSchedule {
    ScheduleShare share;
    std::vector<StudyBlock> blocks;
};

class IScheduleShareRepository {
public:
    virtual ~IScheduleShareRepository() = default;
    virtual std::optional<ScheduleShare> findForOwnerAndRecipient(
        const std::string& ownerId, const std::string& recipientId) const = 0;
    virtual std::optional<ScheduleShare> findForRecipient(
        const std::string& shareId, const std::string& recipientId) const = 0;
    virtual std::vector<ScheduleShare> listForOwner(const std::string& ownerId) const = 0;
    virtual std::vector<ScheduleShare> listForRecipient(const std::string& recipientId) const = 0;
    virtual std::pair<ScheduleShare, bool> create(const std::string& ownerId,
                                                  const std::string& recipientId,
                                                  Timestamp createdAt) = 0;
    virtual bool removeForOwner(const std::string& ownerId, const std::string& shareId) = 0;
};

class ScheduleSharingController {
public:
    ScheduleSharingController(IScheduleShareRepository& shares, const IScheduleRepository& schedules,
                              const IUserRepository& users, INotificationGateway& notifications)
        : shares_(shares), schedules_(schedules), users_(users), notifications_(notifications) {}

    Outcome<ScheduleShare> shareWith(const std::string& ownerId, const std::string& email,
                                     Timestamp now);
    std::vector<ScheduleShare> listOwnedShares(const std::string& ownerId) const;
    std::vector<ScheduleShare> listReceivedShares(const std::string& recipientId) const;
    Outcome<SharedSchedule> viewShared(const std::string& recipientId,
                                       const std::string& shareId) const;
    Outcome<Done> revoke(const std::string& ownerId, const std::string& shareId);

private:
    IScheduleShareRepository& shares_;
    const IScheduleRepository& schedules_;
    const IUserRepository& users_;
    INotificationGateway& notifications_;
};

}  // namespace cramberry
