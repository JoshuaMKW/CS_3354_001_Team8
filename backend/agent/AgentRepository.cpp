#include "agent/AgentRepository.h"

#include "common/Json.h"

namespace cramberry {

void SqlAgentRepository::record(const GenerationRun& run) {
    Json::Value details(Json::arrayValue);
    for (const auto& d : run.details) details.append(d);
    db_.exec(
        "INSERT INTO generation_runs (id, student_id, created_at, engine, ok, block_count, details) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        run.id, run.studentId, static_cast<std::int64_t>(run.createdAt), run.engine,
        static_cast<int>(run.ok), run.blockCount, json::write(details));
}

std::vector<GenerationRun> SqlAgentRepository::recent(const std::string& studentId, int limit) const {
    std::vector<GenerationRun> runs;
    for (const auto& row : db_.exec(
             "SELECT id, student_id, created_at, engine, ok, block_count, details FROM generation_runs "
             "WHERE student_id = $1 ORDER BY created_at DESC LIMIT $2",
             studentId, limit)) {
        GenerationRun run;
        run.id = row["id"].as<std::string>();
        run.studentId = row["student_id"].as<std::string>();
        run.createdAt = row["created_at"].as<std::int64_t>();
        run.engine = row["engine"].as<std::string>();
        run.ok = row["ok"].as<int>() != 0;
        run.blockCount = row["block_count"].as<int>();
        if (const auto details = json::parse(row["details"].as<std::string>()); details && details->isArray()) {
            for (const auto& d : *details) run.details.push_back(d.asString());
        }
        runs.push_back(std::move(run));
    }
    return runs;
}

}  // namespace cramberry
