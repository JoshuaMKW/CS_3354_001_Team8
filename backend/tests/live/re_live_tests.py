#!/usr/bin/env python3
"""RE (James Reid Thompson) live tests for Assignment 5: UC-2 User logging in, FR-1.

Runs TC-02-01 to TC-02-07 from tests/test_re_login.cpp against a cramberry_server it starts
on port 8104, connected to Supabase, and checks the students and sessions rows directly.
Each case signs up its own temp-re-* students and deletes every one of them, including
accounts the case expects the server to refuse. TC-02-07 moves one session's expires_at
into the past in the database instead of waiting 7 days.

Usage (from the repo root, with a Python that has psycopg installed):
    TESTER="Ahnaf Adib (AA)" python3 backend/tests/live/re_live_tests.py
"""

import datetime as dt
import hashlib
import sys
import time
import urllib.error
import urllib.request

from live_common import RUN_ID, Report, call, db, start_server

PORT = 8104
REQ = 'FR-1 "Log in system with unique logins for users", UC-2 User logging in'
PASSWORD = "Password12"

base = None
created = {}


def email_for(n):
    return f"temp-re-{RUN_ID}-{n}@cramberry.test"


def register(name, email, password):
    status, body, _ = call(base, "POST", "/api/auth/register",
                           {"name": name, "email": email, "password": password, "utcOffsetMinutes": -360})
    created.setdefault(email.strip().lower(), set()).add(password)
    return status, body


def student_rows(email):
    with db() as conn:
        return conn.execute("SELECT id, name, email, password_hash FROM students WHERE email = %s",
                            (email.strip().lower(),)).fetchall()


def session_row(token):
    with db() as conn:
        return conn.execute("SELECT student_id, expires_at FROM sessions WHERE token_hash = %s",
                            (hashlib.sha256(token.encode()).hexdigest(),)).fetchone()


def me(token):
    return call(base, "GET", "/api/users/me", token=token)


def me_with_raw_header(value):
    req = urllib.request.Request(base + "/api/users/me", headers={"Authorization": value})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code


def cleanup():
    """Deletes every account this run tried to create, through the API, then confirms in Supabase."""
    for email, passwords in created.items():
        for password in passwords:
            try:
                status, body, _ = call(base, "POST", "/api/auth/login", {"email": email, "password": password})
                if status == 200:
                    code = call(base, "DELETE", "/api/users/me", {"password": password}, body["token"])[0]
                    print(f"  deleted {email} -> {code}")
            except OSError as e:
                print(f"  server unreachable while deleting {email} ({e!r})")
        left = student_rows(email)
        if left:
            # The server is gone, so remove the account the way DELETE /api/users/me does.
            with db() as conn:
                for (sid, *_rest) in left:
                    conn.execute("DELETE FROM schedule_shares WHERE owner_student_id = %s OR recipient_student_id = %s",
                                 (sid, sid))
                    for table in ("sessions", "generation_runs", "notifications", "study_blocks", "tasks",
                                  "courses", "notification_settings", "study_preferences"):
                        conn.execute(f"DELETE FROM {table} WHERE student_id = %s", (sid,))
                    conn.execute("DELETE FROM students WHERE id = %s", (sid,))
            print(f"  deleted {email} directly in Supabase")
        print(f"  {email} rows left in students: {len(student_rows(email))}")
    created.clear()


def now_minutes():
    return int(time.time() // 60)


def tc_02_01(report):
    typed = f"Temp-RE-{RUN_ID}-01@Cramberry.TEST "
    email = email_for("01")
    status, body = register("Name N", typed, PASSWORD)
    token = body.get("token", "") if isinstance(body, dict) else ""
    student = body.get("student", {}) if isinstance(body, dict) else {}
    me_status, me_body, _ = me(token) if token else (None, None, 0)
    rows = student_rows(email)
    stored_hash = rows[0][3] if rows else None
    session = session_row(token) if token else None
    checks = [
        ("register -> 201", status == 201),
        (f"returned email is {email}", student.get("email") == email),
        ("a token is returned", bool(token)),
        ("GET /api/users/me with the token -> 200 and the same student id",
         me_status == 200 and isinstance(me_body, dict) and me_body.get("id") == student.get("id")),
        ("one students row with the lower-case email", len(rows) == 1),
        ("password_hash is not the password and does not contain it",
         stored_hash is not None and stored_hash != PASSWORD and PASSWORD not in stored_hash),
        ("a sessions row for the token belongs to the student",
         session is not None and session[0] == student.get("id")),
    ]
    report.record(
        "TC-02-01", "Signing up creates the account and signs the student in", REQ,
        f'POST /api/auth/register with name "Name N", email "{typed}" (mixed case, trailing space), '
        f'password "{PASSWORD}". Then GET /api/users/me with the returned token.',
        "The account is created with the email stored in lower case and trimmed. A session token is "
        "returned and signs the student in. The password is stored as a hash, not as typed.",
        checks,
        f"Register returned {status} with email {student.get('email')!r} and a token of {len(token)} characters. "
        f"/api/users/me returned {me_status}. Supabase has {len(rows)} students row and the stored hash "
        f"starts {stored_hash[:12] + '...' if stored_hash else None!r}.")


def tc_02_02(report):
    email = email_for("02")
    status, body = register("Someone", email, "password")
    rows = student_rows(email)
    message = body.get("error") if isinstance(body, dict) else body
    checks = [
        ("register -> 400 (invalid input)", status == 400),
        ("no students row for the email", rows == []),
    ]
    report.record(
        "TC-02-02", "A weak password is refused and no account is created", REQ,
        f'POST /api/auth/register with name "Someone", email "{email}", password "password" '
        "(8 letters, no number).",
        "The sign-up is refused as invalid input and no account is stored.",
        checks,
        f"Register returned {status} with {message!r}. Supabase has {len(rows)} students rows for the email.")


def tc_02_03(report):
    email = email_for("03")
    first_status, _ = register("Name N", email, PASSWORD)
    other = email.replace("@cramberry.test", "@CRAMBERRY.test")
    status, body = register("Other", other, "Password34")
    rows = student_rows(email)
    message = body.get("error") if isinstance(body, dict) else body
    checks = [
        ("first register -> 201", first_status == 201),
        ("second register with the same email in another case -> 409", status == 409),
        ("still exactly one students row for the email", len(rows) == 1),
        ('that row is the first account (name "Name N")', len(rows) == 1 and rows[0][1] == "Name N"),
    ]
    report.record(
        "TC-02-03", "An email that is already registered can't be used again, whatever its case", REQ,
        f'POST /api/auth/register with name "Name N", email "{email}", password "{PASSWORD}". Then again '
        f'with name "Other", email "{other}", password "Password34".',
        "The second sign-up is refused as a conflict. Only the first account exists.",
        checks,
        f"First register returned {first_status}. Second returned {status} with {message!r}. Supabase has "
        f"{len(rows)} students row for the email, named {rows[0][1] if rows else None!r}.")


def tc_02_04(report):
    email = email_for("04")
    reg_status, _ = register("Name N", email, PASSWORD)
    status, body, _ = call(base, "POST", "/api/auth/login", {"email": email, "password": PASSWORD})
    token = body.get("token", "") if isinstance(body, dict) else ""
    student = body.get("student", {}) if isinstance(body, dict) else {}
    me_status = me(token)[0] if token else None
    session = session_row(token) if token else None
    checks = [
        ("register -> 201", reg_status == 201),
        ("login -> 200", status == 200),
        (f"returned email is {email}", student.get("email") == email),
        ("a token is returned", bool(token)),
        ("GET /api/users/me with the login token -> 200", me_status == 200),
        ("a sessions row for the login token belongs to the student",
         session is not None and session[0] == student.get("id")),
    ]
    report.record(
        "TC-02-04", "The right email and password sign the student in", REQ,
        f'Sign up "{email}" with password "{PASSWORD}". Then POST /api/auth/login with the same email and '
        "password, and GET /api/users/me with the returned token.",
        "Login succeeds, returns the student with that email and a session token.",
        checks,
        f"Login returned {status} with email {student.get('email')!r} and a token of {len(token)} characters. "
        f"/api/users/me returned {me_status}, and Supabase {'has' if session else 'has no'} sessions row for the token.")


def tc_02_05(report):
    email = email_for("05")
    nobody = email_for("05-nobody")
    reg_status, _ = register("Name N", email, PASSWORD)
    nobody_rows_before = student_rows(nobody)
    wrong_status, wrong_body, _ = call(base, "POST", "/api/auth/login", {"email": email, "password": "Wrong1234"})
    none_status, none_body, _ = call(base, "POST", "/api/auth/login", {"email": nobody, "password": PASSWORD})
    checks = [
        ("register -> 201", reg_status == 201),
        ("the unknown email has no students row", nobody_rows_before == []),
        ("wrong password -> 401", wrong_status == 401),
        ("unknown email -> 401", none_status == 401),
        ("both responses have the same body", wrong_body == none_body),
        ("no token in either response",
         not (isinstance(wrong_body, dict) and "token" in wrong_body)
         and not (isinstance(none_body, dict) and "token" in none_body)),
    ]
    report.record(
        "TC-02-05", "A wrong password and an unknown email get the same answer", REQ,
        f'Sign up "{email}" with password "{PASSWORD}". Then POST /api/auth/login with that email and '
        f'"Wrong1234", and with "{nobody}" (no account) and "{PASSWORD}".',
        "Both logins are refused as unauthorized with the same message, so the app doesn't reveal which "
        "emails have accounts.",
        checks,
        f"Wrong password returned {wrong_status} {wrong_body!r}. Unknown email returned {none_status} {none_body!r}.")


def tc_02_06(report):
    email = email_for("06")
    status, body = register("Name N", email, PASSWORD)
    token = body.get("token", "") if isinstance(body, dict) else ""
    before = me(token)[0]
    row_before = session_row(token)
    logout = call(base, "POST", "/api/auth/logout", token=token)[0]
    after = me(token)[0]
    row_after = session_row(token)
    checks = [
        ("register -> 201", status == 201),
        ("GET /api/users/me before logout -> 200", before == 200),
        ("sessions row exists before logout", row_before is not None),
        ("logout -> 204", logout == 204),
        ("GET /api/users/me with the same token after logout -> 401", after == 401),
        ("sessions row is gone after logout", row_after is None),
    ]
    report.record(
        "TC-02-06", "After logging out, the session no longer works", REQ,
        f'Sign up "{email}" with password "{PASSWORD}". GET /api/users/me with its token, POST '
        "/api/auth/logout with the token, then GET /api/users/me with the same token again.",
        "The session works until logout. After logout the same token is refused.",
        checks,
        f"/api/users/me returned {before} before logout. Logout returned {logout}. /api/users/me then "
        f"returned {after}, and the sessions row was {'gone' if row_after is None else 'still there'}.")


def tc_02_07(report):
    email = email_for("07")
    status, body = register("Name N", email, PASSWORD)
    token = body.get("token", "") if isinstance(body, dict) else ""
    garbage = me("not-a-token")[0]
    empty = me_with_raw_header("Bearer ")
    row = session_row(token)
    issued = now_minutes()
    lifetime = row[1] - issued if row else None
    th = hashlib.sha256(token.encode()).hexdigest()

    def set_expiry(minutes_from_now):
        value = now_minutes() + minutes_from_now
        with db() as conn:
            conn.execute("UPDATE sessions SET expires_at = %s WHERE token_hash = %s", (value, th))
        return value

    just_inside_at = set_expiry(2)
    just_inside = me(token)[0]
    expired_at = set_expiry(-1)
    expired = me(token)[0]

    def utc(minutes):
        return dt.datetime.fromtimestamp(minutes * 60, dt.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    checks = [
        ("register -> 201", status == 201),
        ('GET /api/users/me with token "not-a-token" -> 401', garbage == 401),
        ("GET /api/users/me with an empty bearer token -> 401", empty == 401),
        ("the new session's expires_at is 7 days (10080 minutes) after sign-up",
         lifetime is not None and 10079 <= lifetime <= 10080),
        ("session 2 minutes short of 7 days old -> 200", just_inside == 200),
        ("session 1 minute past 7 days old -> 401", expired == 401),
    ]
    report.record(
        "TC-02-07", "Made-up tokens are refused, and a session expires after 7 days", REQ,
        f'Sign up "{email}" with password "{PASSWORD}". GET /api/users/me with token "not-a-token" and with '
        '"Authorization: Bearer " (empty token). This session\'s '
        f"expires_at in the Supabase sessions table was set to {utc(just_inside_at)}, as if it was created "
        f"7 days less 2 minutes ago, and GET /api/users/me was called. Then it was set to {utc(expired_at)}, "
        "as if created 7 days and 1 minute ago, and GET /api/users/me was called again.",
        "Made-up and empty tokens are refused. A session works until it is 7 days old and is refused after.",
        checks,
        f'"not-a-token" returned {garbage} and the empty token returned {empty}. The new session\'s expiry was '
        f"{lifetime} minutes after sign-up. Two minutes before expiry it returned {just_inside}, and one "
        f"minute after it returned {expired}.")


def main():
    global base
    base = start_server(PORT)
    report = Report("RE", "James Reid Thompson (RE)", "RE live test results: UC-2 User logging in")
    for test in (tc_02_01, tc_02_02, tc_02_03, tc_02_04, tc_02_05, tc_02_06, tc_02_07):
        try:
            test(report)
        except Exception as e:  # a crash is a failed case, not a stopped run
            report.record(test.__name__.upper().replace("_", "-"), "Crashed", REQ, "", "", [("ran", False)],
                          f"The test script crashed with {e!r}.")
        finally:
            cleanup()
    with db() as conn:
        left = conn.execute("SELECT email FROM students WHERE email LIKE 'temp-re-%'").fetchall()
    print(f"temp-re-* students left in Supabase: {len(left)} {left}")
    code = report.write(f"port {PORT}")
    sys.exit(code or (1 if left else 0))


if __name__ == "__main__":
    main()
