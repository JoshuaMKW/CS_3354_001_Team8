#pragma once

#include <drogon/orm/DbClient.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace cramberry::data {

// All-or-nothing group of statements (NFR-7, NFR-19: a failed operation must not
// leave a student's data half changed).
//
//   auto tx = db.begin();
//   tx.exec("DELETE FROM study_blocks WHERE student_id = $1", id);
//   tx.exec("INSERT INTO study_blocks ...", ...);
//   tx.commit();   // without this, the destructor rolls everything back
class Transaction {
public:
    using Translator = std::function<std::string(std::string_view)>;

    Transaction(std::shared_ptr<drogon::orm::Transaction> transaction, Translator translate);
    ~Transaction();

    Transaction(Transaction&&) noexcept = default;
    Transaction& operator=(Transaction&&) = delete;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    template <typename... Args>
    drogon::orm::Result exec(std::string_view sql, Args&&... args) {
        try {
            return transaction_->execSqlSync(translate_(sql), std::forward<Args>(args)...);
        } catch (...) {
            failed_ = true;  // the driver has already rolled back
            throw;
        }
    }

    // Commits and waits until the database confirms it. Throws if the commit failed.
    void commit();
    void rollback();

private:
    std::shared_ptr<drogon::orm::Transaction> transaction_;
    Translator translate_;
    bool failed_ = false;
    bool finished_ = false;
};

}  // namespace cramberry::data
