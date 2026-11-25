#pragma once

// UC-11 Save/Retrieve Student Data: storage interface. Author: Ahnaf Adib (AA).
//
// Production: Supabase PostgreSQL, with row-level security on owner_id as a second guard.
// Tests: InMemoryStudentDataRepository in tests/test_aa.cpp.

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cramberry {

// The kinds of student data UC-11 saves and retrieves (Assignment, Schedule and
// PlannerSettings in the UML class diagram).
enum class RecordType { Assignment, Schedule, Preferences };

inline std::string recordTypeName(RecordType type) {
    switch (type) {
        case RecordType::Assignment: return "assignment";
        case RecordType::Schedule: return "schedule";
        case RecordType::Preferences: return "preferences";
    }
    return "record";
}

using FieldMap = std::map<std::string, std::string>;

struct StudentRecord {
    std::string id;
    std::string ownerId;  // the student this record belongs to
    RecordType type = RecordType::Assignment;
    FieldMap fields;

    bool operator==(const StudentRecord&) const = default;
};

class IStudentDataRepository {
public:
    virtual ~IStudentDataRepository() = default;

    // selectRecords(). Reads throw std::runtime_error if the database can't be reached.
    virtual std::optional<StudentRecord> findById(const std::string& recordId) const = 0;
    virtual std::vector<StudentRecord> listByOwner(const std::string& ownerId, RecordType type) const = 0;

    // updateRecords(). Writes are atomic: they return nothing / false when the write failed and was
    // rolled back, and the stored data must then be exactly as it was before.
    virtual std::optional<std::string> insert(const std::string& ownerId, RecordType type,
                                              const FieldMap& fields) = 0;
    virtual bool update(const StudentRecord& record) = 0;
};

}  // namespace cramberry
