#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "common/Time.h"

namespace cramberry {
struct DeadlineEvent {
    std::string id;
    std::string title;
    Timestamp dueUtc = 0;
    std::string sourceFile;
    bool operator==(const DeadlineEvent&) const = default;
};

// Supabase adapter can implement this interface; tests use an in-memory implementation.
class IDeadlineRepository {
public:
    virtual ~IDeadlineRepository() = default;
    virtual std::vector<DeadlineEvent> list(const std::string& studentId) const = 0;
    // Implementations must save atomically, or throw without changing prior data.
    virtual void append(const std::string& studentId, const std::vector<DeadlineEvent>& events) = 0;
};

struct SyllabusFile {
    std::string filename;
    std::vector<std::uint8_t> bytes;
};

// Gemini/document parsing integration point. Must throw on extraction failure.
class ISyllabusExtractor {
public:
    virtual ~ISyllabusExtractor() = default;
    virtual std::vector<DeadlineEvent> extract(const SyllabusFile& file) = 0;
};

struct UploadResult {
    bool ok = false;
    std::string message;
    std::vector<DeadlineEvent> imported;
};

class UploadSchedule {
public:
    UploadSchedule(ISyllabusExtractor& extractor, IDeadlineRepository& repository)
        : extractor_(extractor), repository_(repository) {}
    // studentId MUST come from verified server-side authentication, never a form field.
    UploadResult upload(const std::string& studentId, const SyllabusFile& file);
private:
    ISyllabusExtractor& extractor_;
    IDeadlineRepository& repository_;
};
} // namespace cramberry
