#pragma once

#include <string>
#include <vector>

#include "common/Time.h"
#include "data-api/Database.h"

namespace cramberry {

// One attempt to generate a schedule (FR-29: AI answers are checked, and rejected
// answers are recorded instead of being saved).
struct GenerationRun {
    std::string id;
    std::string studentId;
    Timestamp createdAt = 0;
    std::string engine;  // "gemini" or "local"
    bool ok = false;
    int blockCount = 0;
    std::vector<std::string> details;  // violations or warnings
};

class IAgentRepository {
public:
    virtual ~IAgentRepository() = default;
    virtual void record(const GenerationRun& run) = 0;
    virtual std::vector<GenerationRun> recent(const std::string& studentId, int limit) const = 0;
};

class SqlAgentRepository : public IAgentRepository {
public:
    explicit SqlAgentRepository(const data::Database& db) : db_(db) {}

    void record(const GenerationRun& run) override;
    std::vector<GenerationRun> recent(const std::string& studentId, int limit) const override;

private:
    const data::Database& db_;
};

}  // namespace cramberry
