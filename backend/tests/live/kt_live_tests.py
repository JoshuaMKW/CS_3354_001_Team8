#!/usr/bin/env python3
"""KT (Kevin Tran) live tests for Assignment 5: UC-8 Export Schedule and UC-9 Share / View Schedule.

Runs TC-08-01 to TC-08-04 and TC-09-01 to TC-09-06, defined in
backend/tests/test_schedule_acceptance.cpp, against cramberry_server on port 8103 connected
to Supabase, and checks the rows in the database. TC-08-04 starts a second server on port
8113 whose database connection goes through a local TCP relay, then cuts the relay so the
export fails for real. Every test uses temporary students (temp-kt-*) and deletes them.

Run from the repository root (needs psycopg):
    TESTER="Ahnaf Adib (AA)" python3 backend/tests/live/kt_live_tests.py
"""

import contextlib
import datetime as dt
import pathlib
import socket
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from live_common import ENV, PASSWORD, Report, call, db, delete_student, new_student, start_server  # noqa: E402

PORT, FAIL_PORT = 8103, 8113
UTC = dt.timezone.utc
UC8 = "UC-8 Export Schedule. Add function to download or export schedule as a file"
UC9 = "UC-9 Share / View Schedule. Add a function to share / view schedules with others"
REAL_TIME = "NFR. When sharing schedules to others it should update in real-time"
SAFE_ERROR = "Something went wrong on our side. Please try again."

BASE = None


def request(base, method, path, token=None):
    """Like live_common.call, but keeps the raw text and the response headers."""
    req = urllib.request.Request(base + path, method=method)
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status, {k.lower(): v for k, v in r.headers.items()}, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, {k.lower(): v for k, v in e.headers.items()}, e.read().decode()


@contextlib.contextmanager
def students(*tags):
    made = []
    try:
        for tag in tags:
            made.append(new_student(BASE, tag, utc_offset_minutes=0))
        yield made
    finally:
        for s in made:
            delete_student(BASE, s)


def slot_days(*starts):
    """Consecutive days of this week (UTC, Mon to Sun) whose study slots all start at least 15 min from now."""
    now = dt.datetime.now(UTC)
    monday = (now - dt.timedelta(days=now.weekday())).date()
    week = [monday + dt.timedelta(days=i) for i in range(7)]
    for i in range(len(week) - len(starts) + 1):
        days = week[i:i + len(starts)]
        if all(dt.datetime.combine(d, dt.time(*map(int, s.split(":"))), UTC) > now + dt.timedelta(minutes=15)
               for d, s in zip(days, starts)):
            return days
    raise RuntimeError(f"no {len(starts)} consecutive days left this week with slots at {', '.join(starts)}")


def name(day):
    return day.strftime("%a")


def label(day, start, end):
    return f"{day:%a %d %b} {start} to {end} UTC"


def iso(day, clock):
    return f"{day.isoformat()}T{clock}:00Z"


def plan(s, hours, tasks, day=None):
    """Creates the tasks, saves study preferences and generates the week's plan. Returns (tasks, blocks)."""
    made = []
    for title, due in tasks:
        status, task, _ = call(BASE, "POST", "/api/tasks", {"title": title, "dueAt": due, "estimatedMinutes": 60},
                               s["token"])
        assert status == 201, (status, task)
        made.append(task)
    prefs = {"studyHours": [{"day": name(d), "start": a, "end": b} for d, a, b in hours],
             "sessionMinutes": 60, "breakMinutes": 15, "maxStudyMinutesPerDay": 180, "personalTime": []}
    assert call(BASE, "PUT", "/api/settings/preferences", prefs, s["token"])[0] == 200
    # Saving preferences always starts a background rebuild, even with CRAMBERRY_AUTO_REPLAN=0.
    # Wait for it, or it can land after the explicit generate below and overwrite it.
    deadline = time.monotonic() + 60
    while not any(r["ok"] for r in call(BASE, "GET", "/api/agent/runs", None, s["token"])[1]):
        assert time.monotonic() < deadline, "background rebuild after saving preferences did not finish"
        time.sleep(0.3)
    status, body, _ = call(BASE, "POST", "/api/agent/generate", {"day": name(day)} if day else {}, s["token"])
    assert status == 200, (status, body)
    return made, schedule(s)


def schedule(s):
    return call(BASE, "GET", "/api/schedule", None, s["token"])[1]["blocks"]


def slim(blocks):
    return [(b["title"], b["assignmentId"], b["day"], b["start"], b["end"]) for b in blocks]


def has_block(blocks, task, day, start, end):
    return [(b["assignmentId"], b["day"], b["start"], b["end"]) for b in blocks] == [(task["id"], name(day), start, end)]


def block_rows(student_id):
    with db() as conn:
        return conn.execute("SELECT assignment_id, title, day, start_minute, end_minute FROM study_blocks "
                            "WHERE student_id = %s ORDER BY sort_order", (student_id,)).fetchall()


def share_rows(owner_id):
    with db() as conn:
        return conn.execute("SELECT id, recipient_student_id FROM schedule_shares WHERE owner_student_id = %s",
                            (owner_id,)).fetchall()


def inbox(s):
    return call(BASE, "GET", "/api/notifications", None, s["token"])[1]


def share(owner, email):
    return call(BASE, "POST", "/api/schedule/shares", {"email": email}, owner["token"])


def view(s, created):
    """Opens the share at the viewEndpoint the share response gave."""
    return call(BASE, "GET", created["viewEndpoint"], None, s["token"])


class Relay:
    """A TCP relay between a server and the database, so a test can cut the connection."""

    def __init__(self, host, port):
        self.upstream = (host, port)
        self.listener = socket.create_server(("127.0.0.1", 0))
        self.listener.settimeout(0.2)
        self.port = self.listener.getsockname()[1]
        self.sockets, self.open = [], True
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while self.open:
            try:
                client, _ = self.listener.accept()
            except (socket.timeout, OSError):
                continue
            try:
                server = socket.create_connection(self.upstream, timeout=10)
            except OSError:
                client.close()
                continue
            client.settimeout(None)
            server.settimeout(None)
            self.sockets += [client, server]
            for a, b in ((client, server), (server, client)):
                threading.Thread(target=self._pump, args=(a, b), daemon=True).start()

    @staticmethod
    def _pump(src, dst):
        try:
            while data := src.recv(65536):
                dst.sendall(data)
        except OSError:
            pass
        for sock in (src, dst):
            with contextlib.suppress(OSError):
                sock.shutdown(socket.SHUT_RDWR)

    def cut(self):
        self.open = False
        self.listener.close()
        for sock in self.sockets:
            with contextlib.suppress(OSError):
                sock.shutdown(socket.SHUT_RDWR)
            sock.close()


def relayed_database_url():
    parts = urllib.parse.urlsplit(ENV["CRAMBERRY_DATABASE_URL"])
    relay = Relay(parts.hostname, parts.port or 5432)
    userinfo = parts.netloc.rsplit("@", 1)[0]
    return relay, urllib.parse.urlunsplit(parts._replace(netloc=f"{userinfo}@127.0.0.1:{relay.port}"))


def tc_08_01():
    (d,) = slot_days("18:00")
    with students("kt") as (owner,):
        due = (dt.datetime.now(UTC) + dt.timedelta(days=3)).strftime("%Y-%m-%dT%H:%M:00Z")
        (essay,), blocks = plan(owner, [(d, "18:00", "19:00")], [("Essay", due)])
        status, headers, ics = request(BASE, "GET", "/api/schedule/export.ics", owner["token"])
        start = f"DTSTART:{d:%Y%m%d}T180000Z\r\n"
        checks = [
            (f"owner's schedule has one Essay block on {label(d, '18:00', '19:00')}",
             has_block(blocks, essay, d, "18:00", "19:00") and blocks[0]["title"] == "Essay"),
            ("export returns 200 as a text/calendar attachment",
             status == 200 and headers.get("content-type", "").startswith("text/calendar")
             and "attachment" in headers.get("content-disposition", "")),
            ("file has BEGIN:VCALENDAR", "BEGIN:VCALENDAR\r\n" in ics),
            (f"file has {start.strip()}", start in ics),
            ("file has SUMMARY:Study: Essay", "SUMMARY:Study: Essay\r\n" in ics),
            ("file has SUMMARY:Due: Essay", "SUMMARY:Due: Essay\r\n" in ics),
            ("file has END:VCALENDAR", "END:VCALENDAR\r\n" in ics),
        ]
        inputs = (f"Owner with study hours {label(d, '18:00', '19:00')}, task \"Essay\" due {due}, 60 min. "
                  "Schedule generated, then GET /api/schedule/export.ics, the call the Schedule page's export button makes.")
        actual = (f"Returned {status} as {headers.get('content-type')} with {ics.count('BEGIN:VEVENT')} events. "
                  f"The file has {', '.join(l for l in ics.splitlines() if l.startswith(('DTSTART', 'SUMMARY')))}.")
        return inputs, checks, actual


def tc_08_02():
    with students("kt") as (owner,):
        status, _, ics = request(BASE, "GET", "/api/schedule/export.ics", owner["token"])
        rows = block_rows(owner["id"])
        checks = [
            ("owner has no study blocks in Supabase", rows == []),
            ("export returns 200", status == 200),
            ("file has BEGIN:VCALENDAR", "BEGIN:VCALENDAR\r\n" in ics),
            ("file has END:VCALENDAR", "END:VCALENDAR\r\n" in ics),
            ("file has no BEGIN:VEVENT", "BEGIN:VEVENT" not in ics),
        ]
        inputs = "New owner with no tasks and no schedule. GET /api/schedule/export.ics with the owner's token."
        actual = (f"Returned {status} with a {len(ics.splitlines())} line calendar and {ics.count('BEGIN:VEVENT')} events. "
                  f"The owner has {len(rows)} study_blocks rows.")
        return inputs, checks, actual


def tc_08_03():
    with students("kt") as (owner,):
        _, login, _ = call(BASE, "POST", "/api/auth/login", {"email": owner["email"], "password": PASSWORD})
        logout = call(BASE, "POST", "/api/auth/logout", None, login["token"])[0]
        no_token = request(BASE, "GET", "/api/schedule/export.ics")
        old_token = request(BASE, "GET", "/api/schedule/export.ics", login["token"])
        checks = [
            ("logout returns 204", logout == 204),
            ("export with no token returns 401", no_token[0] == 401),
            ("export with the logged-out token returns 401", old_token[0] == 401),
            ("no calendar is returned", "BEGIN:VCALENDAR" not in no_token[2] + old_token[2]),
        ]
        inputs = ("Owner signs in and logs out (POST /api/auth/logout). Then GET /api/schedule/export.ics "
                  "with no token, and again with the logged-out token.")
        actual = (f"Logout returned {logout}. Export with no token returned {no_token[0]} {no_token[2]}, "
                  f"and with the logged-out token {old_token[0]} {old_token[2]}.")
        return inputs, checks, actual


def tc_08_04():
    (d,) = slot_days("16:00")
    with students("kt") as (owner,):
        due = (dt.datetime.now(UTC) + dt.timedelta(days=3)).strftime("%Y-%m-%dT%H:%M:00Z")
        (essay,), before = plan(owner, [(d, "16:00", "17:00")], [("Essay", due)])
        rows_before = block_rows(owner["id"])
        relay, url = relayed_database_url()
        fail_base = start_server(FAIL_PORT, CRAMBERRY_DATABASE_URL=url, CRAMBERRY_AUTO_REPLAN=0)
        reachable = request(fail_base, "GET", "/api/health")[0]
        relay.cut()
        status, _, body = request(fail_base, "GET", "/api/schedule/export.ics", owner["token"])
        log = (pathlib.Path(tempfile.gettempdir()) / f"cramberry_server_{FAIL_PORT}.log").read_text()
        after = schedule(owner)
        rows_after = block_rows(owner["id"])
        leaks = [w for w in ("postgres", "supabase", "connection", "timeout", "exception", "sql", "127.0.0.1")
                 if w in body.lower()]
        checks = [
            (f"owner's schedule has one Essay block on {label(d, '16:00', '17:00')}",
             has_block(before, essay, d, "16:00", "17:00")),
            ("second server answered health 200 before the cut", reachable == 200),
            ("export returns 500", status == 500),
            ("response has no calendar", "BEGIN:VCALENDAR" not in body),
            ("response is the generic error with no internal details", SAFE_ERROR in body and not leaks),
            ("schedule from the main server is unchanged", slim(after) == slim(before)),
            ("study_blocks rows in Supabase are unchanged", rows_after == rows_before),
        ]
        inputs = (f"Owner with study hours {label(d, '16:00', '17:00')}, task \"Essay\" due {due}, schedule generated. "
                  f"A second server on port {FAIL_PORT} reaches Supabase through a local relay. The relay is cut, "
                  f"then GET /api/schedule/export.ics goes to port {FAIL_PORT}. The schedule is read back on port {PORT}.")
        actual = (f"Export returned {status} {body}, and the server log {'has' if 'Database error' in log else 'has no'} "
                  f"database error line. The main server still shows {len(after)} block, {slim(after)}.")
        return inputs, checks, actual


def tc_09_01():
    (d,) = slot_days("15:00")
    with students("kt", "kt-b") as (owner, recipient):
        due = iso(d, "23:59")
        (task,), blocks = plan(owner, [(d, "15:00", "16:00")], [("Study session", due)])
        status, body, _ = share(owner, recipient["email"].upper())
        share_id = body.get("shareId") if isinstance(body, dict) else None
        got = inbox(recipient)
        checks = [
            (f"owner's schedule has one block on {label(d, '15:00', '16:00')}", has_block(blocks, task, d, "15:00", "16:00")),
            ("share returns 201", status == 201),
            ("share's recipientId is the recipient", isinstance(body, dict) and body.get("recipientId") == recipient["id"]),
            ("schedule_shares row in Supabase links owner and recipient", share_rows(owner["id"]) == [(share_id, recipient["id"])]),
            ("recipient has exactly one notification", len(got) == 1),
            ("owner has no notification", inbox(owner) == []),
            ("notification body has the share ID", len(got) == 1 and share_id is not None and share_id in got[0]["body"]),
        ]
        inputs = (f"Owner with a \"Study session\" block {label(d, '15:00', '16:00')}. "
                  f"POST /api/schedule/shares with the recipient's email in capitals ({recipient['email'].upper()}).")
        actual = (f"Share returned {status} with recipientId {'equal' if isinstance(body, dict) and body.get('recipientId') == recipient['id'] else 'not equal'} "
                  f"to the recipient's ID. The recipient has {len(got)} notification, \"{got[0]['body'] if got else 'none'}\".")
        return inputs, checks, actual


def tc_09_02():
    (d,) = slot_days("15:00")
    with students("kt", "kt-b") as (owner, recipient):
        (task,), blocks = plan(owner, [(d, "15:00", "16:00")], [("Study session", iso(d, "23:59"))])
        _, created, _ = share(owner, recipient["email"])
        status, shown, _ = view(recipient, created)
        shown_blocks = shown.get("blocks", []) if isinstance(shown, dict) else []
        checks = [
            (f"owner's schedule has one block on {label(d, '15:00', '16:00')}", has_block(blocks, task, d, "15:00", "16:00")),
            ("recipient's view returns 200", status == 200),
            ("view names the owner", isinstance(shown, dict) and shown.get("ownerId") == owner["id"]),
            ("view has exactly the owner's block", slim(shown_blocks) == slim(blocks)),
        ]
        inputs = (f"Owner with a \"Study session\" block {label(d, '15:00', '16:00')} shares with the recipient. "
                  "Recipient calls GET on the viewEndpoint from the share response, /api/schedule/shared-with-me/{shareId}.")
        actual = f"The recipient's view returned {status} {shown}. The owner's schedule has {slim(blocks)}."
        return inputs, checks, actual


def tc_09_03():
    a, b = slot_days("15:00", "16:00")
    with students("kt", "kt-b") as (owner, recipient):
        hours = [(a, "15:00", "16:00"), (b, "16:00", "17:00")]
        (task,), blocks = plan(owner, hours, [("Study session", iso(b, "23:59"))], day=a)
        _, created, _ = share(owner, recipient["email"])
        first_status, first, _ = view(recipient, created)
        move = {"assignmentId": task["id"], "from": {"day": name(a), "start": "15:00", "end": "16:00"},
                "to": {"day": name(b), "start": "16:00", "end": "17:00"}}
        moved = call(BASE, "PATCH", "/api/schedule/blocks", move, owner["token"])[0]
        status, second, _ = view(recipient, created)
        first_blocks = first.get("blocks", []) if isinstance(first, dict) else []
        second_blocks = second.get("blocks", []) if isinstance(second, dict) else []
        checks = [
            (f"owner's schedule has one block on {label(a, '15:00', '16:00')}", has_block(blocks, task, a, "15:00", "16:00")),
            (f"first view shows one block on {label(a, '15:00', '16:00')}", has_block(first_blocks, task, a, "15:00", "16:00")),
            ("owner's move returns 200", moved == 200),
            ("refreshed view returns 200", status == 200),
            (f"refreshed view shows one block on {label(b, '16:00', '17:00')}", has_block(second_blocks, task, b, "16:00", "17:00")),
        ]
        inputs = (f"Owner with study hours {label(a, '15:00', '16:00')} and {label(b, '16:00', '17:00')}, "
                  f"\"Study session\" planned on {name(a)} only, shared with the recipient. Recipient views it. "
                  f"Owner moves the block to {label(b, '16:00', '17:00')} with PATCH /api/schedule/blocks. Recipient views it again.")
        actual = (f"The first view returned {first_status} {first}. The move returned {moved}, "
                  f"and the refreshed view returned {status} {second}.")
        return inputs, checks, actual


def tc_09_04():
    with students("kt") as (owner,):
        with db() as conn:
            missing_registered = conn.execute("SELECT count(*) FROM students WHERE lower(email) = 'missing@utd.edu'"
                                              ).fetchone()[0]
        bad = share(owner, "not-an-email")
        missing = share(owner, "missing@utd.edu")
        own = share(owner, owner["email"])
        checks = [
            ("missing@utd.edu is not a registered account", missing_registered == 0),
            ("\"not-an-email\" returns 400", bad[0] == 400),
            ("missing@utd.edu returns 404", missing[0] == 404),
            ("owner's own email returns 400", own[0] == 400),
            ("no schedule_shares row was created", share_rows(owner["id"]) == []),
            ("no notification was sent to the owner", inbox(owner) == []),
        ]
        inputs = (f"POST /api/schedule/shares as the owner with \"not-an-email\", then \"missing@utd.edu\", "
                  f"then the owner's own email ({owner['email']}).")
        actual = (f"\"not-an-email\" returned {bad[0]} {bad[1]}, \"missing@utd.edu\" {missing[0]} {missing[1]}, "
                  f"and the owner's email {own[0]} {own[1]}. The owner has {len(share_rows(owner['id']))} share rows.")
        return inputs, checks, actual


def tc_09_05():
    with students("kt", "kt-b", "kt-c") as (owner, recipient, outsider):
        _, created, _ = share(owner, recipient["email"])
        by_owner = view(owner, created)
        by_outsider = view(outsider, created)
        no_token = call(BASE, "GET", created["viewEndpoint"])
        by_recipient = view(recipient, created)[0]
        checks = [
            ("owner opening the share link returns 404", by_owner[0] == 404),
            ("another student opening it returns 404", by_outsider[0] == 404),
            ("no token returns 401", no_token[0] == 401),
            ("no schedule is returned to any of them",
             not any(isinstance(r[1], dict) and "blocks" in r[1] for r in (by_owner, by_outsider, no_token))),
        ]
        inputs = ("Owner shares with the recipient. GET /api/schedule/shared-with-me/{shareId} as the owner, "
                  "as a third student, and with no token.")
        actual = (f"The owner got {by_owner[0]} {by_owner[1]}, the outsider {by_outsider[0]} {by_outsider[1]}, "
                  f"and no token {no_token[0]} {no_token[1]}. The recipient got {by_recipient} on the same link.")
        return inputs, checks, actual


def tc_09_06():
    a, b = slot_days("15:00", "15:00")
    with students("kt", "kt-b") as (owner, recipient):
        hours = [(a, "15:00", "16:00"), (b, "15:00", "16:00")]
        (bio, phys), blocks = plan(owner, hours, [("Biology Study Session", iso(a, "20:00")),
                                                  ("Physics Study Session", iso(b, "23:59"))])
        _, created, _ = share(owner, recipient["email"])
        first_status, first, _ = view(recipient, created)
        removed = call(BASE, "DELETE", "/api/schedule/blocks",
                       {"assignmentId": bio["id"], "day": name(a), "start": "15:00", "end": "16:00"}, owner["token"])[0]
        status, second, _ = view(recipient, created)
        kept = [x for x in blocks if x["assignmentId"] == phys["id"]]
        first_blocks = first.get("blocks", []) if isinstance(first, dict) else []
        second_blocks = second.get("blocks", []) if isinstance(second, dict) else []
        checks = [
            (f"owner's schedule has Biology {label(a, '15:00', '16:00')} and Physics {label(b, '15:00', '16:00')}",
             {(x["assignmentId"], x["day"]) for x in blocks} == {(bio["id"], name(a)), (phys["id"], name(b))}
             and len(blocks) == 2),
            ("first view shows 2 blocks", len(first_blocks) == 2),
            ("owner's delete returns 200", removed == 200),
            ("refreshed view returns 200", status == 200),
            ("refreshed view shows only the Physics block", slim(second_blocks) == slim(kept) and len(kept) == 1),
        ]
        inputs = (f"Owner with study hours {label(a, '15:00', '16:00')} and {label(b, '15:00', '16:00')}. "
                  f"Tasks \"Biology Study Session\" due {iso(a, '20:00')} and \"Physics Study Session\" due {iso(b, '23:59')}, "
                  "schedule generated and shared. Owner deletes the Biology block with DELETE /api/schedule/blocks. "
                  "Recipient views the share before and after.")
        actual = (f"The first view returned {first_status} {first}. The delete returned {removed}, "
                  f"and the refreshed view returned {status} {second}.")
        return inputs, checks, actual


CASES = [
    (tc_08_01, "TC-08-01", "Exports a populated schedule", UC8,
     "Export succeeds. The file has BEGIN:VCALENDAR, a DTSTART at the block's start time in UTC, "
     "SUMMARY:Study: Essay, SUMMARY:Due: Essay and END:VCALENDAR."),
    (tc_08_02, "TC-08-02", "Exports an empty schedule without failing", UC8,
     "Export succeeds. The file has BEGIN:VCALENDAR and END:VCALENDAR and no BEGIN:VEVENT."),
    (tc_08_03, "TC-08-03", "Rejects export when logged out", UC8,
     "The request is refused with 401 Unauthorized and the export is not run."),
    (tc_08_04, "TC-08-04", "Export failure returns a safe error and keeps the schedule", UC8,
     "The export returns 500 with no calendar and no internal error details. The saved schedule is unchanged."),
    (tc_09_01, "TC-09-01", "Shares a schedule with a registered recipient", UC9,
     "The share is created for the recipient, matched without regard to case. "
     "Exactly one notification is sent, to the recipient, and it contains the share ID."),
    (tc_09_02, "TC-09-02", "Recipient views the shared schedule", UC9,
     "The recipient sees the share with the owner's ID and exactly the owner's study block."),
    (tc_09_03, "TC-09-03", "Shared schedule reflects the owner's edit on refresh", f"{UC9}. {REAL_TIME}",
     "The first view shows the block at its first time. After the owner moves it, the refreshed view shows one block at the new time."),
    (tc_09_04, "TC-09-04", "Rejects an invalid or unregistered recipient", UC9,
     "An invalid email and the owner's own email are rejected as invalid input. An unregistered email returns not found. "
     "No notification is sent."),
    (tc_09_05, "TC-09-05", "Denies unauthorized and unauthenticated shared schedule access", UC9,
     "The owner and an outsider get not found for the share. A request with no sign-in gets 401 and the view is not run."),
    (tc_09_06, "TC-09-06", "Deleted event disappears from the shared schedule", f"{UC9}. {REAL_TIME}",
     "The shared view shows 2 blocks. After the owner deletes the Biology block, it shows only the Physics block."),
]


def main():
    global BASE
    report = Report("KT", "Kevin Tran (KT)", "KT live test results: UC-8 and UC-9")
    BASE = start_server(PORT, CRAMBERRY_AUTO_REPLAN=0)
    for fn, tc, title, requirement, expected in CASES:
        try:
            inputs, checks, actual = fn()
        except Exception as e:  # a crash is a failed case, not a stopped run
            inputs, checks, actual = "Setup did not finish.", [("test ran to completion", False)], f"Crashed with {e!r}."
        report.record(tc, title, requirement, inputs, expected, checks, actual)
    with db() as conn:
        left_any = conn.execute("SELECT count(*) FROM students WHERE email LIKE 'temp-kt-%'").fetchone()[0]
    print(f"Temporary KT students left in Supabase: {left_any}")
    servers = (f"port {PORT}, plus port {FAIL_PORT} for TC-08-04 with its database connection through a local relay "
               "that the test cuts. Both with `CRAMBERRY_AUTO_REPLAN=0`, students on UTC offset 0")
    code = report.write(servers)
    sys.exit(code or (1 if left_any else 0))


if __name__ == "__main__":
    main()
