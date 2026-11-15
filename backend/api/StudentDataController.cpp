// UC-11 Save/Retrieve Student Data. Author: Ahnaf Adib (AA).
// See StudentDataController.h for the flow this implements.

#include "api/StudentDataController.h"

#include <algorithm>
#include <exception>

#include "schedule/SchedulingTypes.h"

namespace cramberry {

namespace {

const char* const kSignIn = "Please sign in to see your data.";
const char* const kNoAccess = "You can't open this record";
const char* const kMissing = "That record doesn't exist.";
const char* const kSaveFailed = "Couldn't save your changes. Your previous data was kept.";
const char* const kDatabaseDown = "Couldn't reach the database. Please try again.";

bool wholeNumberBetween(const std::string& value, int low, int high) {
    const auto n = parseWholeNumber(value);
    return n && *n >= low && *n <= high;
}

// Checks one field. Returns the message to show, or nothing if the value is fine.
std::optional<std::string> checkField(RecordType type, const std::string& name,
                                      const std::string& value) {
    if (name == "id" || name == "ownerId") {
        return "\"" + name + "\" can't be changed";
    }
    switch (type) {
        case RecordType::Assignment:
            if (name == "title") {
                if (value.empty()) return "Title can't be empty";
                if (value.size() > 200) return "Title can't be longer than 200 characters";
                return std::nullopt;
            }
            if (name == "dueDate") {
                if (!parseIsoMinute(value)) return "Due date must look like 2026-11-20T23:59";
                return std::nullopt;
            }
            if (name == "estimatedMinutes") {
                if (!wholeNumberBetween(value, 1, 6000)) {
                    return "Estimated time must be a whole number from 1 to 6000 min";
                }
                return std::nullopt;
            }
            if (name == "status") {
                if (value != "Not Started" && value != "In Progress" && value != "Completed") {
                    return "Status must be Not Started, In Progress or Completed";
                }
                return std::nullopt;
            }
            break;
        case RecordType::Schedule:
            if (name == "weekStart") {
                if (!parseIsoDate(value)) return "Week start must look like 2026-11-16";
                return std::nullopt;
            }
            if (name == "isShared") {
                if (value != "true" && value != "false") return "isShared must be true or false";
                return std::nullopt;
            }
            break;
        case RecordType::Preferences:
            if (name == "studyHoursPerDay") {
                if (!wholeNumberBetween(value, 1, 24)) {
                    return "Study hours per day must be a whole number from 1 to 24";
                }
                return std::nullopt;
            }
            if (name == "preferredStart" || name == "preferredEnd") {
                if (!parseClock(value)) {
                    return std::string(name == "preferredStart" ? "Preferred start" : "Preferred end") +
                           " must look like 18:30";
                }
                return std::nullopt;
            }
            if (name == "breakMinutes") {
                if (!wholeNumberBetween(value, 0, 120)) {
                    return "Break must be a whole number from 0 to 120 min";
                }
                return std::nullopt;
            }
            break;
    }
    return "\"" + name + "\" isn't a valid " + recordTypeName(type) + " field";
}

// Fields a new record must have.
std::optional<std::string> checkRequired(RecordType type, const FieldMap& fields) {
    if (type == RecordType::Assignment) {
        if (!fields.contains("title")) return "Title is required";
        if (!fields.contains("dueDate")) return "Due date is required";
    }
    if (type == RecordType::Schedule && !fields.contains("weekStart")) {
        return "Week start is required";
    }
    return std::nullopt;
}

}  // namespace

StudentDataController::StudentDataController(IStudentDataRepository& repository)
    : repository_(repository) {}

std::optional<std::string> StudentDataController::validateFields(RecordType type,
                                                                 const FieldMap& fields) {
    for (const auto& [name, value] : fields) {
        if (auto error = checkField(type, name, value)) return error;
    }
    if (type == RecordType::Preferences && fields.contains("preferredStart") &&
        fields.contains("preferredEnd") &&
        *parseClock(fields.at("preferredEnd")) <= *parseClock(fields.at("preferredStart"))) {
        return "Preferred end must be after preferred start";
    }
    return std::nullopt;
}

DataResult StudentDataController::getRecord(const std::string& studentId,
                                            const std::string& recordId) const {
    if (studentId.empty()) return {401, kSignIn, {}};
    try {
        // checkAccess(studentId, recordId)
        const auto record = repository_.findById(recordId);
        if (!record) return {404, kMissing, {}};
        if (record->ownerId != studentId) return {403, kNoAccess, {}};
        return {200, "OK", {*record}};
    } catch (const std::exception&) {
        return {503, kDatabaseDown, {}};
    }
}

DataResult StudentDataController::listRecords(const std::string& studentId, RecordType type) const {
    if (studentId.empty()) return {401, kSignIn, {}};
    try {
        auto records = repository_.listByOwner(studentId, type);
        // Second guard: drop anything that isn't this student's, whatever the query returned.
        std::erase_if(records, [&](const StudentRecord& r) { return r.ownerId != studentId; });
        return {200, "OK", std::move(records)};
    } catch (const std::exception&) {
        return {503, kDatabaseDown, {}};
    }
}

DataResult StudentDataController::createRecord(const std::string& studentId, RecordType type,
                                               const FieldMap& fields) {
    if (studentId.empty()) return {401, kSignIn, {}};
    if (auto error = validateFields(type, fields)) return {400, *error, {}};
    if (auto error = checkRequired(type, fields)) return {400, *error, {}};

    std::optional<std::string> id;
    try {
        id = repository_.insert(studentId, type, fields);
    } catch (const std::exception&) {
        id.reset();
    }
    if (!id) return {500, kSaveFailed, {}};
    return {201, "Saved.", {StudentRecord{*id, studentId, type, fields}}};
}

DataResult StudentDataController::updateRecord(const std::string& studentId,
                                               const std::string& recordId,
                                               const FieldMap& changes) {
    if (studentId.empty()) return {401, kSignIn, {}};
    if (changes.empty()) return {400, "There are no changes to save.", {}};

    std::optional<StudentRecord> record;
    try {
        record = repository_.findById(recordId);
    } catch (const std::exception&) {
        return {503, kDatabaseDown, {}};
    }
    // checkAccess(studentId, recordId)
    if (!record) return {404, kMissing, {}};
    if (record->ownerId != studentId) return {403, kNoAccess, {}};

    if (auto error = validateFields(record->type, changes)) return {400, *error, {}};
    StudentRecord updated = *record;
    for (const auto& [name, value] : changes) updated.fields[name] = value;
    if (auto error = validateFields(updated.type, updated.fields)) return {400, *error, {}};

    // updateRecords(studentId, changes)
    bool saved = false;
    try {
        saved = repository_.update(updated);
    } catch (const std::exception&) {
        saved = false;
    }
    if (!saved) return {500, kSaveFailed, {}};
    return {200, "Saved.", {updated}};
}

}  // namespace cramberry
