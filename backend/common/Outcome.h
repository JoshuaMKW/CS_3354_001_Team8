#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cramberry {

// Why a use case could not complete. Routes map each code to an HTTP status.
enum class ErrorCode {
    InvalidInput,  // 400
    Unauthorized,  // 401
    NotFound,      // 404
    Conflict,      // 409
    Unprocessable, // 422: the request was valid but can't be carried out (e.g. no room in the week)
    Unavailable,   // 503: an outside service (Gemini, the database) failed
};

struct Error {
    ErrorCode code = ErrorCode::InvalidInput;
    std::string message;
    std::vector<std::string> details;
};

// Either a value or an Error. A small stand-in for C++23 std::expected.
template <typename T>
class Outcome {
public:
    Outcome(T value) : state_(std::move(value)) {}
    Outcome(Error error) : state_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(state_); }
    explicit operator bool() const { return ok(); }

    T& value() { return std::get<T>(state_); }
    const T& value() const { return std::get<T>(state_); }
    const Error& error() const { return std::get<Error>(state_); }

private:
    std::variant<T, Error> state_;
};

struct Done {};  // value for use cases that return nothing on success

inline Error invalid(std::string message) { return {ErrorCode::InvalidInput, std::move(message), {}}; }
inline Error notFound(std::string message) { return {ErrorCode::NotFound, std::move(message), {}}; }

}  // namespace cramberry
