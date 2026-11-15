#pragma once

// UC-11 Save/Retrieve Student Data. Author: Ahnaf Adib (AA).
//
// The Backend API System's handler for GET, POST and PATCH /api/data (FR-28, NFR-19). It follows
// the AA sequence diagram:
//   checkAccess(studentId, recordId)
//     [access allowed] read:   selectRecords(studentId)          -> 200 OK(studentData)
//                      update: updateRecords(studentId, changes) -> 200 OK(studentData)
//     [access denied]  403 Forbidden -> "You can't open this record"

#include <optional>
#include <string>
#include <vector>

#include "data/StudentDataRepository.h"

namespace cramberry {

struct DataResult {
    int httpStatus = 500;  // 200, 201, 400, 401, 403, 404, 500 or 503
    std::string message;
    std::vector<StudentRecord> records;  // the data to show, on success
};

class StudentDataController {
public:
    explicit StudentDataController(IStudentDataRepository& repository);

    // studentId comes from the verified Supabase Auth session, never from the request body.
    DataResult getRecord(const std::string& studentId, const std::string& recordId) const;
    DataResult listRecords(const std::string& studentId, RecordType type) const;
    DataResult createRecord(const std::string& studentId, RecordType type, const FieldMap& fields);
    DataResult updateRecord(const std::string& studentId, const std::string& recordId,
                            const FieldMap& changes);

    // Returns the error message to show, or nothing if every field is valid for this record type.
    static std::optional<std::string> validateFields(RecordType type, const FieldMap& fields);

private:
    IStudentDataRepository& repository_;
};

}  // namespace cramberry
