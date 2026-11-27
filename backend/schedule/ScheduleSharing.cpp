#include "schedule/ScheduleSharing.h"

#include "user/UserController.h"

namespace cramberry {

Outcome<ScheduleShare> ScheduleSharingController::shareWith(const std::string& ownerId,
                                                            const std::string& email,
                                                            Timestamp now) {
    const std::string normalizedEmail = normalizeEmail(email);
    if (const auto error = UserController::validateEmail(normalizedEmail)) return invalid(*error);

    const auto recipient = users_.findByEmail(normalizedEmail);
    if (!recipient) return notFound("A registered account with that email was not found");
    if (recipient->student.id == ownerId) return invalid("You can't share a schedule with yourself");
    if (!users_.findById(ownerId)) return notFound("Account not found");

    if (const auto existing = shares_.findForOwnerAndRecipient(ownerId, recipient->student.id)) {
        return *existing;
    }

    const auto [share, created] = shares_.create(ownerId, recipient->student.id, now);
    if (created) {
        notifications_.send({recipient->student.id, Channel::Desktop, "A schedule was shared with you",
                             std::string("A study schedule was shared with you. Open ") +
                                 "/api/schedule/shared-with-me/" + share.id,
                             now});
    }
    return share;
}

std::vector<ScheduleShare> ScheduleSharingController::listOwnedShares(const std::string& ownerId) const {
    return shares_.listForOwner(ownerId);
}

std::vector<ScheduleShare> ScheduleSharingController::listReceivedShares(
    const std::string& recipientId) const {
    return shares_.listForRecipient(recipientId);
}

Outcome<SharedSchedule> ScheduleSharingController::viewShared(const std::string& recipientId,
                                                              const std::string& shareId) const {
    const auto share = shares_.findForRecipient(shareId, recipientId);
    if (!share) return notFound("Shared schedule not found");
    return SharedSchedule{*share, schedules_.getActiveStudyItems(share->ownerId)};
}

Outcome<Done> ScheduleSharingController::revoke(const std::string& ownerId,
                                                const std::string& shareId) {
    if (!shares_.removeForOwner(ownerId, shareId)) return notFound("Shared schedule not found");
    return Done{};
}

}  // namespace cramberry
