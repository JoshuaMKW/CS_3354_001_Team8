#include "data-api/Transaction.h"

#include <chrono>
#include <future>
#include <stdexcept>

namespace cramberry::data {

Transaction::Transaction(std::shared_ptr<drogon::orm::Transaction> transaction,
                         Translator translate)
    : transaction_(std::move(transaction)), translate_(std::move(translate)) {}

Transaction::~Transaction() {
    if (!transaction_ || finished_ || failed_) return;
    try {
        transaction_->rollback();
    } catch (...) {
        // Never throw from a destructor; dropping the transaction rolls it back anyway.
    }
}

void Transaction::commit() {
    if (finished_) throw std::logic_error("transaction already finished");
    if (failed_) throw std::runtime_error("transaction failed and was rolled back");
    finished_ = true;

    // Drogon commits when the last reference to the transaction goes away and reports
    // the result through the commit callback. Wait for it so the caller only returns
    // once the data is really saved.
    auto done = std::make_shared<std::promise<bool>>();
    auto result = done->get_future();
    transaction_->setCommitCallback([done](bool committed) { done->set_value(committed); });
    transaction_.reset();

    if (result.wait_for(std::chrono::seconds(30)) != std::future_status::ready || !result.get()) {
        throw std::runtime_error("the database did not commit the transaction");
    }
}

void Transaction::rollback() {
    if (finished_ || !transaction_) return;
    finished_ = true;
    if (!failed_) transaction_->rollback();
    transaction_.reset();
}

}  // namespace cramberry::data
