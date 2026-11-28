# RE live test results: UC-2 User logging in

| Item | Value |
| :--- | :--- |
| Date | Fri 27 Nov 2026, 23:28 CST |
| Tester | Ahnaf Adib (AA) |
| Test author | James Reid Thompson (RE) |
| Machine | Darwin 27.0.0 (arm64) |
| Server | `cramberry_server` from `backend/out/build`, port 8104 |
| Database | Supabase hosted PostgreSQL (db.<project>.supabase.co:5432) |
| AI | `anthropic/claude-haiku-5.5` through OpenRouter |
| Client | Python 3 `urllib`, one request at a time |

**7 run, 7 Pass, 0 Fail.**

| Test case ID | Requirement / use case | Tester | Inputs | Expected result | Actual result | Pass/Fail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TC-02-01 Signing up creates the account and signs the student in | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | POST /api/auth/register with name "Name N", email "Temp-RE-1795843681-01@Cramberry.TEST " (mixed case, trailing space), password "Password12". Then GET /api/users/me with the returned token. | The account is created with the email stored in lower case and trimmed. A session token is returned and signs the student in. The password is stored as a hash, not as typed. | Register returned 201 with email 'temp-re-1795843681-01@cramberry.test' and a token of 64 characters. /api/users/me returned 200. Supabase has 1 students row and the stored hash starts 'pbkdf2_sha25...'. | **Pass** |
| TC-02-02 A weak password is refused and no account is created | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | POST /api/auth/register with name "Someone", email "temp-re-1795843681-02@cramberry.test", password "password" (8 letters, no number). | The sign-up is refused as invalid input and no account is stored. | Register returned 400 with 'Password must contain at least one letter and one number'. Supabase has 0 students rows for the email. | **Pass** |
| TC-02-03 An email that is already registered can't be used again, whatever its case | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | POST /api/auth/register with name "Name N", email "temp-re-1795843681-03@cramberry.test", password "Password12". Then again with name "Other", email "temp-re-1795843681-03@CRAMBERRY.test", password "Password34". | The second sign-up is refused as a conflict. Only the first account exists. | First register returned 201. Second returned 409 with 'An account with this email already exists'. Supabase has 1 students row for the email, named 'Name N'. | **Pass** |
| TC-02-04 The right email and password sign the student in | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | Sign up "temp-re-1795843681-04@cramberry.test" with password "Password12". Then POST /api/auth/login with the same email and password, and GET /api/users/me with the returned token. | Login succeeds, returns the student with that email and a session token. | Login returned 200 with email 'temp-re-1795843681-04@cramberry.test' and a token of 64 characters. /api/users/me returned 200, and Supabase has sessions row for the token. | **Pass** |
| TC-02-05 A wrong password and an unknown email get the same answer | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | Sign up "temp-re-1795843681-05@cramberry.test" with password "Password12". Then POST /api/auth/login with that email and "Wrong1234", and with "temp-re-1795843681-05-nobody@cramberry.test" (no account) and "Password12". | Both logins are refused as unauthorized with the same message, so the app doesn't reveal which emails have accounts. | Wrong password returned 401 {'error': 'Email or password is incorrect'}. Unknown email returned 401 {'error': 'Email or password is incorrect'}. | **Pass** |
| TC-02-06 After logging out, the session no longer works | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | Sign up "temp-re-1795843681-06@cramberry.test" with password "Password12". GET /api/users/me with its token, POST /api/auth/logout with the token, then GET /api/users/me with the same token again. | The session works until logout. After logout the same token is refused. | /api/users/me returned 200 before logout. Logout returned 204. /api/users/me then returned 401, and the sessions row was gone. | **Pass** |
| TC-02-07 Made-up tokens are refused, and a session expires after 7 days | FR-1 "Log in system with unique logins for users", UC-2 User logging in | Ahnaf Adib (AA) | Sign up "temp-re-1795843681-07@cramberry.test" with password "Password12". GET /api/users/me with token "not-a-token" and with "Authorization: Bearer " (empty token). This session's expires_at in the Supabase sessions table was set to 2026-11-28 05:30 UTC, as if it was created 7 days less 2 minutes ago, and GET /api/users/me was called. Then it was set to 2026-11-28 05:27 UTC, as if created 7 days and 1 minute ago, and GET /api/users/me was called again. | Made-up and empty tokens are refused. A session works until it is 7 days old and is refused after. | "not-a-token" returned 401 and the empty token returned 401. The new session's expiry was 10080 minutes after sign-up. Two minutes before expiry it returned 200, and one minute after it returned 401. | **Pass** |

## Checks

**TC-02-01** (Pass)
- ok: register -> 201
- ok: returned email is temp-re-1795843681-01@cramberry.test
- ok: a token is returned
- ok: GET /api/users/me with the token -> 200 and the same student id
- ok: one students row with the lower-case email
- ok: password_hash is not the password and does not contain it
- ok: a sessions row for the token belongs to the student

**TC-02-02** (Pass)
- ok: register -> 400 (invalid input)
- ok: no students row for the email

**TC-02-03** (Pass)
- ok: first register -> 201
- ok: second register with the same email in another case -> 409
- ok: still exactly one students row for the email
- ok: that row is the first account (name "Name N")

**TC-02-04** (Pass)
- ok: register -> 201
- ok: login -> 200
- ok: returned email is temp-re-1795843681-04@cramberry.test
- ok: a token is returned
- ok: GET /api/users/me with the login token -> 200
- ok: a sessions row for the login token belongs to the student

**TC-02-05** (Pass)
- ok: register -> 201
- ok: the unknown email has no students row
- ok: wrong password -> 401
- ok: unknown email -> 401
- ok: both responses have the same body
- ok: no token in either response

**TC-02-06** (Pass)
- ok: register -> 201
- ok: GET /api/users/me before logout -> 200
- ok: sessions row exists before logout
- ok: logout -> 204
- ok: GET /api/users/me with the same token after logout -> 401
- ok: sessions row is gone after logout

**TC-02-07** (Pass)
- ok: register -> 201
- ok: GET /api/users/me with token "not-a-token" -> 401
- ok: GET /api/users/me with an empty bearer token -> 401
- ok: the new session's expires_at is 7 days (10080 minutes) after sign-up
- ok: session 2 minutes short of 7 days old -> 200
- ok: session 1 minute past 7 days old -> 401
