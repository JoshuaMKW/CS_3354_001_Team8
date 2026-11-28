# Cramberry login system (C++17)

Email + password signup and login, with a hashed-password store, login-attempt log,
brute-force lockout, and cookie sessions. Layout follows the group's backend diagram
(`user/`, `data-api/`, `frontend/`).

## Run it in Visual Studio (CMake)

1. Install vcpkg if you don't have it, and set the `VCPKG_ROOT` environment variable
   (VS 2022 17.6+ opened from "Developer PowerShell for VS" already sets it).
2. File > Open > Folder... and pick this folder. VS reads `CMakePresets.json` and `vcpkg.json`,
   installs the five dependencies (first run takes a few minutes), and configures.
3. Choose the `cramberry_server.exe` startup item and press F5.
4. Browse to http://localhost:8080 and sign up.
5. Tests: Test > Run CTests, or run `auth_tests.exe`.

The SQLite file (`cramberry.db`) is created in the working directory.

## Add it to your existing project instead

**Existing CMake project**
```cmake
add_subdirectory(path/to/cramberry-auth-cpp)
target_link_libraries(your_target PRIVATE cramberry_auth)
```
Set `-DCRAMBERRY_AUTH_BUILD_SERVER=OFF` if you only want the library, and merge the five
packages from `vcpkg.json` into your own manifest.

**Existing .sln / .vcxproj project**
1. Put `vcpkg.json` (merge it with yours) next to the project and run `vcpkg integrate install` once.
2. Copy `backend/user/*` and `backend/data-api/*` into your project and add the `.cpp` files.
3. Project Properties > C/C++ > Additional Include Directories: add the folder that contains `user/` and `data-api/`.
4. Set C++ Language Standard to C++17 and add `_WIN32_WINNT=0x0A00` to Preprocessor Definitions.
5. Copy the wiring from `backend/main.cpp` into your own startup code.
6. Copy `frontend/*` into your frontend folder.

## How the pieces fit

| File | Job |
|---|---|
| `user/Validation` | Email, username, password rules (8+ chars, a number, a special character) |
| `user/Crypto` | Argon2id hashing, random tokens (libsodium) behind `ICrypto` |
| `user/UserRepository` | `IUserRepository` interface + SQLite implementation |
| `user/AuthService` | Signup, login, lockout, sessions. No HTTP or SQL in here |
| `user/UserController` | `/api/signup`, `/api/login`, `/api/logout`, `/api/me` |
| `data-api/Database` | SQLite wrapper with prepared statements and `Transaction` |

To use Supabase instead of SQLite, write another class that implements `IUserRepository`
and pass it to `AuthService`. Nothing else changes.

## Configuration (environment variables)

`CRAMBERRY_PORT` (8080), `CRAMBERRY_DB` (cramberry.db), `CRAMBERRY_FRONTEND` (frontend folder),
`CRAMBERRY_SECURE_COOKIES=1` (set when served over HTTPS).

## Security notes

- Passwords are stored only as salted Argon2id hashes; session tokens only as digests.
- Wrong password and unknown email return the same message, and both cost one hash check.
- 5 failed logins per email within 15 minutes triggers a lockout (`AuthConfig`). A success resets the count.
  This also means someone can lock out a known email on purpose; add per-IP limits if that matters.
- The server listens on 127.0.0.1 only. Put HTTPS in front of it (reverse proxy) before exposing it,
  then set `CRAMBERRY_SECURE_COOKIES=1`.
- `Dashboard.html` is a static file; the data behind it is protected by `/api/me`.

## Not included yet

MFA, password reset, account deletion, and the admin login-log screen.
`login_attempts` already holds what the admin view needs, and `IUserRepository::setStatus` is the hook for deactivation.
