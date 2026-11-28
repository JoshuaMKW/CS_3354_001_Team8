#include "schedule/UploadSchedule.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <unordered_set>

namespace cramberry {
namespace {
constexpr std::size_t kMaxBytes = 10 * 1024 * 1024;
bool endsWithIgnoreCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    auto start = s.end() - static_cast<std::ptrdiff_t>(suffix.size());
    return std::equal(start, s.end(), suffix.begin(), [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });
}
bool hasPdfMagic(const std::vector<std::uint8_t>& bytes) {
    static constexpr char magic[] = {'%', 'P', 'D', 'F', '-'};
    return bytes.size() >= 5 && std::equal(std::begin(magic), std::end(magic), bytes.begin());
}
}
UploadResult UploadSchedule::upload(const std::string& studentId, const SyllabusFile& file) {
    if (studentId.empty()) return {false, "Authentication required", {}};
    if (file.filename.empty() || file.bytes.empty()) return {false, "Select a nonempty PDF syllabus", {}};
    if (!endsWithIgnoreCase(file.filename, ".pdf") || !hasPdfMagic(file.bytes))
        return {false, "Only valid PDF files are supported", {}};
    if (file.bytes.size() > kMaxBytes) return {false, "PDF exceeds 10 MB limit", {}};
    try {
        auto events = extractor_.extract(file);
        std::unordered_set<std::string> ids;
        for (auto& event : events) {
            if (event.id.empty() || event.title.empty() || event.dueUtc <= 0 || !ids.insert(event.id).second)
                return {false, "AI returned invalid or duplicate deadlines", {}};
            event.sourceFile = file.filename;
        }
        if (events.empty()) return {true, "No deadlines found; existing schedule unchanged", {}};
        repository_.append(studentId, events);
        return {true, "Syllabus deadlines imported", std::move(events)};
    } catch (const std::exception&) {
        return {false, "Syllabus processing failed; existing schedule unchanged", {}};
    } catch (...) {
        return {false, "Syllabus processing failed; existing schedule unchanged", {}};
    }
}
} // namespace cramberry
