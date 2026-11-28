#!/usr/bin/env python3
"""JO (Jonathan Lee) live tests for Assignment 5: UC-3 upload syllabus and UC-4 view schedule.

Runs the eight cases of backend/tests/test_jo_schedule.cpp against the running app. UC-3 cases
drive the Upload Syllabus page (/schedule/create/upload) and UC-4 cases drive the View Schedule
page (/schedule) in headless Chromium, against a Vite dev server this script starts on port 3105.
Saved data is then read back from Supabase. The server has no syllabus parser: the page finds
the dates in the browser and saves each one with POST /api/tasks.

Servers started by the script:
    8105  cramberry_server on backend/.env, behind the Vite dev server on 3105
    8115  cramberry_server whose database connection runs through a local relay. TC-04-04 cuts
          the relay to make a database outage while the server keeps running. (A server started
          with an unreachable CRAMBERRY_DATABASE_URL never starts listening, so it can't serve
          the page.)

Every case signs up its own temporary student (temp-jo-...@cramberry.test) and deletes it.

Usage, from the repo root:
    pip install psycopg[binary] playwright==1.49.1      # Chromium build 1148
    TESTER="Ahnaf Adib (AA)" python3 backend/tests/live/jo_live_tests.py
"""

import atexit
import datetime as dt
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request

from playwright.sync_api import sync_playwright

from live_common import ENV, ROOT, Report, call, db, delete_student, new_student, start_server

API_PORT, OUTAGE_PORT, FRONT_PORT = 8105, 8115, 3105
FRONT = f"http://localhost:{FRONT_PORT}"
OFFSET = -360
TZ = "America/Chicago"
SYLLABUS = "CS3345_Fall2026.txt"

UC3 = ('UC-3 User uploads class syllabus. "Students should be able to import deadlines and assignments '
       'by uploading their course syllabus"')
UC4 = 'UC-4 View Schedule. "Student views their generated schedule in the calendar system"'
UC4_SECURE = UC4 + '. "Secure access to student\'s schedule & classes (JO)"'

PREFS = {
    "studyHours": [{"day": d, "start": "07:00", "end": "23:00"}
                   for d in ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")],
    "sessionMinutes": 60, "breakMinutes": 15, "maxStudyMinutesPerDay": 600, "personalTime": [],
}


def local_now():
    return dt.datetime.now(dt.timezone.utc).replace(tzinfo=None) + dt.timedelta(minutes=OFFSET)


def to_utc(local):
    return local - dt.timedelta(minutes=OFFSET)


def iso(local):
    return to_utc(local).strftime("%Y-%m-%dT%H:%M:00Z")


def epoch_minutes(local):
    return int(to_utc(local).replace(tzinfo=dt.timezone.utc).timestamp()) // 60


def shown(local):
    return local.strftime("%a %d %b %Y %H:%M") + " local time (UTC-6)"


def next_day_in_week():
    """Tomorrow, or today on a Sunday, since the planner only covers the rest of this week."""
    now = local_now()
    return (now + dt.timedelta(days=1 if now.weekday() < 6 else 0)).date()


def end_of_week():
    now = local_now()
    sunday = now.date() + dt.timedelta(days=6 - now.weekday())
    return dt.datetime.combine(sunday, dt.time(23, 59))


def at(day, hour, minute):
    return dt.datetime.combine(day, dt.time(hour, minute))


def start_frontend():
    env = dict(os.environ, PORT=str(FRONT_PORT), CRAMBERRY_API_URL=f"http://127.0.0.1:{API_PORT}")
    log = open(os.path.join(tempfile.gettempdir(), f"cramberry_vite_{FRONT_PORT}.log"), "w")
    proc = subprocess.Popen(["npx", "vite", "--port", str(FRONT_PORT)], cwd=ROOT / "frontend", env=env,
                            stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    atexit.register(lambda: os.killpg(proc.pid, signal.SIGTERM))
    for _ in range(150):
        if proc.poll() is not None:
            sys.exit(f"vite exited with code {proc.returncode}; see {log.name}")
        try:
            with urllib.request.urlopen(FRONT + "/", timeout=2) as r:
                if r.status == 200:
                    return
        except OSError:
            pass
        time.sleep(0.2)
    sys.exit(f"vite did not answer on {FRONT} within 30 s; see {log.name}")


class DatabaseRelay:
    """TCP relay between a server and Supabase. cut() drops every connection and refuses new ones."""

    def __init__(self, host, port):
        self.target = (host, port)
        self.listener = socket.create_server(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.sockets = []
        self.up = True
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            try:
                client, _ = self.listener.accept()
            except OSError:
                return
            if not self.up:
                client.close()
                continue
            upstream = socket.create_connection(self.target)
            self.sockets += [client, upstream]
            for a, b in ((client, upstream), (upstream, client)):
                threading.Thread(target=self._pipe, args=(a, b), daemon=True).start()

    @staticmethod
    def _pipe(a, b):
        try:
            while data := a.recv(65536):
                b.sendall(data)
        except OSError:
            pass
        for s in (a, b):
            try:
                s.close()
            except OSError:
                pass

    def cut(self):
        self.up = False
        for s in self.sockets:
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.sockets = []

    def restore(self):
        self.up = True


def relayed_database_url(relay):
    url = urllib.parse.urlparse(ENV["CRAMBERRY_DATABASE_URL"])
    netloc = url.netloc.rsplit("@", 1)
    return url._replace(netloc=f"{netloc[0]}@127.0.0.1:{relay.port}").geturl()


def task_rows(student_id):
    with db() as conn:
        return conn.execute("SELECT title, kind, due_at, notes FROM tasks WHERE student_id = %s ORDER BY due_at",
                            (student_id,)).fetchall()


def signed_in_page(browser, student):
    context = browser.new_context(viewport={"width": 1280, "height": 900}, timezone_id=TZ)
    context.add_init_script(f"localStorage.setItem('cramberry.token', {student['token']!r})")
    return context, context.new_page()


def alerts(page):
    return [t.strip().split("\n")[0] for t in page.locator("[role=alert]").all_inner_texts() if t.strip()]


def said(texts):
    return " ".join(f'"{t}"' for t in texts) or "no error"


def titles(rows):
    return ", ".join(f'"{r[0]}"' for r in rows) or "none"


def open_upload(page):
    page.goto(f"{FRONT}/schedule/create/upload")
    page.wait_for_selector("h1:has-text('Upload syllabus')", timeout=30_000)


def choose_file(page, name, data, mime="text/plain"):
    page.set_input_files("input[type=file]", files=[{"name": name, "mimeType": mime, "buffer": data}])
    page.wait_for_function(
        "() => document.querySelector('.us-file .muted').textContent !== 'No file chosen'"
        " || document.querySelector('[role=alert]')", timeout=5_000)


def find_deadlines(page):
    page.click("button:has-text('Find deadlines')")
    page.wait_for_selector(".us-rows, .hint:has-text('No deadlines found')", timeout=5_000)


def page_says(page, text):
    return text in page.inner_text("body")


def syllabus_text(due):
    return (f"CS 3345 Data Structures and Algorithms, Fall 2026\n"
            f"Project 3 due {due:%b} {due.day} by 11:59 pm\n")


# ---------------------------------------------------------------- UC-3

def tc_03_01(browser):
    s = new_student(API, "jo-0301")
    try:
        due = at(next_day_in_week(), 23, 59)
        text = syllabus_text(due)
        context, page = signed_in_page(browser, s)
        open_upload(page)
        choose_file(page, SYLLABUS, text.encode())
        find_deadlines(page)
        page.click("button:has-text('Add 1 task')")
        page.wait_for_selector(".us-done, [role=alert]", timeout=15_000)
        done = page.locator(".us-done").inner_text().split("\n")[0].strip() if page.locator(".us-done").count() else ""
        errors = alerts(page)
        context.close()
        rows = task_rows(s["id"])
        row = rows[0] if len(rows) == 1 else None
        inputs = (f"Upload Syllabus page, no course. File {SYLLABUS} with the line \"Project 3 due {due:%b} {due.day} by 11:59 pm\", "
                  f"that is {shown(due)}. Clicked Find deadlines, then Add 1 task.")
        checks = [
            ("page reports the upload succeeded with no error", done.startswith("Added 1 task") and not errors),
            ("exactly one task is saved for the student", len(rows) == 1),
            ("saved task is Project 3 due at the syllabus date and time",
             row is not None and row[0] == "Project 3" and row[2] == epoch_minutes(due)),
            (f"saved task records the source file {SYLLABUS}",
             row is not None and (SYLLABUS in row[0] or SYLLABUS in (row[3] or ""))),
        ]
        actual = f"Page said {said([done] if done else errors)}. Database has {len(rows)} task(s)."
        if row:
            saved_due = dt.datetime.fromtimestamp(row[2] * 60, dt.timezone.utc).replace(tzinfo=None) + dt.timedelta(minutes=OFFSET)
            actual += f" Saved \"{row[0]}\" due {shown(saved_due)} with notes \"{row[3]}\"."
        return inputs, checks, actual
    finally:
        delete_student(API, s)


def existing_task(student):
    due = at(next_day_in_week(), 21, 0)
    status, body, _ = call(API, "POST", "/api/tasks", {"title": "Existing", "dueAt": iso(due)}, student["token"])
    assert status == 201, (status, body)
    return due


def tc_03_02(browser):
    s = new_student(API, "jo-0302")
    try:
        due = existing_task(s)
        before = task_rows(s["id"])
        text = ("CS 3345 Data Structures and Algorithms, Fall 2026\n"
                "Office hours are Tuesdays and Thursdays after class in ECSS 2.410.\n"
                "Grades come from homework, two exams and a final project.\n")
        context, page = signed_in_page(browser, s)
        open_upload(page)
        choose_file(page, SYLLABUS, text.encode())
        find_deadlines(page)
        none_found = page_says(page, "No deadlines found")
        errors = alerts(page)
        context.close()
        after = task_rows(s["id"])
        inputs = (f"Student already has one task \"Existing\" due {shown(due)}. Upload Syllabus page, file "
                  f"{SYLLABUS} with three lines and no dated deadline. Clicked Find deadlines.")
        checks = [
            ("upload finishes without an error", not errors),
            ("page says no deadlines were found", none_found),
            ("the existing task is still the only task, unchanged", len(after) == 1 and after == before),
        ]
        actual = (f"Page {'said' if none_found else 'did not say'} \"No deadlines found\" and showed {said(errors)}. "
                  f"Database has {len(after)} task(s), {titles(after)}, {'unchanged' if after == before else 'changed'}.")
        return inputs, checks, actual
    finally:
        delete_student(API, s)


def tc_03_03(browser):
    s = new_student(API, "jo-0303")
    try:
        context, page = signed_in_page(browser, s)
        open_upload(page)
        choose_file(page, "bad.exe", bytes([1, 2, 3]), "application/octet-stream")
        errors = alerts(page)
        chosen = page.locator(".us-file .muted").inner_text().strip()
        find = page.locator("button:has-text('Find deadlines')")
        clicked = find.is_enabled()
        if clicked:
            find_deadlines(page)
            add = page.locator(".summary button.primary")
            if add.count() and add.is_enabled():
                add.click()
                page.wait_for_timeout(2_000)
        context.close()
        rows = task_rows(s["id"])
        inputs = ("Upload Syllabus page, file bad.exe with the bytes 01 02 03, set on the file input. "
                  "Then Find deadlines if the page allowed it.")
        checks = [
            ("page rejects bad.exe with an error message", bool(errors)),
            ("bad.exe is not taken as the syllabus", chosen != "bad.exe"),
            ("no task is saved", rows == []),
        ]
        actual = (f"Page showed {said(errors)} and listed \"{chosen}\" as the chosen file. Find deadlines was "
                  f"{'enabled' if clicked else 'disabled'}. Database has {len(rows)} task(s).")
        return inputs, checks, actual
    finally:
        delete_student(API, s)


def tc_03_04(browser):
    s = new_student(API, "jo-0304")
    try:
        existing_due = existing_task(s)
        before = task_rows(s["id"])
        due = at(next_day_in_week(), 23, 59)
        context, page = signed_in_page(browser, s)
        timed_out = []

        def time_out_saves(route):
            if route.request.method == "POST":
                timed_out.append(route.request.url)
                route.abort("timedout")
            else:
                route.continue_()

        page.route("**/api/tasks", time_out_saves)
        open_upload(page)
        choose_file(page, SYLLABUS, syllabus_text(due).encode())
        find_deadlines(page)
        page.click("button:has-text('Add 1 task')")
        page.wait_for_selector(".us-done, [role=alert]", timeout=15_000)
        errors = alerts(page)
        done = page.locator(".us-done").count() > 0
        context.close()
        after = task_rows(s["id"])
        inputs = (f"Student already has one task \"Existing\" due {shown(existing_due)}. Upload Syllabus page, "
                  f"file {SYLLABUS} with \"Project 3 due {due:%b} {due.day} by 11:59 pm\". The upload has no AI "
                  f"step, so the save request it sends (POST /api/tasks) was made to time out in the browser. "
                  f"Clicked Find deadlines, then Add 1 task.")
        checks = [
            ("the save request was sent and timed out", len(timed_out) == 1),
            ("page reports the upload failed", bool(errors) and not done),
            ("the existing task is still the only task, unchanged", len(after) == 1 and after == before),
        ]
        actual = (f"{len(timed_out)} save request timed out. Page showed {said(errors)}"
                  f"{' and a success message' if done else ''}. Database has {len(after)} task(s), "
                  f"{titles(after)}, {'unchanged' if after == before else 'changed'}.")
        return inputs, checks, actual
    finally:
        delete_student(API, s)


# ---------------------------------------------------------------- UC-4

def plan(student, tasks):
    tok = student["token"]
    assert call(API, "PUT", "/api/settings/preferences", PREFS, tok)[0] == 200
    for title, due in tasks:
        status, body, _ = call(API, "POST", "/api/tasks", {"title": title, "dueAt": iso(due), "estimatedMinutes": 60}, tok)
        assert status == 201, (status, body)
    status, body, _ = call(API, "POST", "/api/agent/generate", {}, tok, timeout=90)
    return status, body


def open_schedule(page):
    page.goto(f"{FRONT}/schedule")
    page.wait_for_selector(".vs-cal, [role=alert]", timeout=60_000)


def tc_04_01(browser):
    s = new_student(API, "jo-0401")
    try:
        now, later = local_now(), end_of_week()
        earlier = (now + (later - now) / 2).replace(minute=0, second=0, microsecond=0)
        gen_status, gen = plan(s, [("Later", later), ("Earlier", earlier)])
        context, page = signed_in_page(browser, s)
        open_schedule(page)
        errors = alerts(page)
        listed = page.locator(".vs-tasks .vs-task-main strong").all_inner_texts()
        drawn = page.locator(".vs-cal .block.study").count()
        context.close()
        lo, hi = now.date(), later.date() + dt.timedelta(days=2)
        api_status, api_tasks, _ = call(API, "GET", f"/api/tasks?from={lo}&to={hi}", None, s["token"])
        api_titles = [t["title"] for t in api_tasks] if api_status == 200 else api_tasks
        inputs = (f"Created \"Later\" due {shown(later)}, then \"Earlier\" due {shown(earlier)}, 60 min each. "
                  f"Study hours 07:00 to 23:00 every day. Generated the plan, then opened View Schedule. "
                  f"Also GET /api/tasks?from={lo}&to={hi}.")
        checks = [
            ("schedule loads without an error", not errors and gen_status == 200),
            ("View Schedule lists both deadlines, Earlier first", listed == ["Earlier", "Later"]),
            ("the API lists both deadlines in the range, Earlier first", api_titles == ["Earlier", "Later"]),
        ]
        actual = (f"Generate returned {gen_status} with engine {gen.get('engine') if isinstance(gen, dict) else gen} "
                  f"and the page drew {drawn} block(s). Page listed {', '.join(listed) or 'nothing'}, the API listed "
                  f"{', '.join(api_titles) if isinstance(api_titles, list) else api_titles}, and the page showed {said(errors)}.")
        return inputs, checks, actual
    finally:
        delete_student(API, s)


def tc_04_02(browser):
    s = new_student(API, "jo-0402")
    try:
        status, sched, _ = call(API, "GET", "/api/schedule", None, s["token"])
        context, page = signed_in_page(browser, s)
        open_schedule(page)
        errors = alerts(page)
        empty = page.locator(".vs-empty")
        message = empty.inner_text().split("\n")[0].strip() if empty.count() else ""
        context.close()
        inputs = "New student with no tasks and no schedule opened View Schedule (GET /api/schedule behind it)."
        checks = [
            ("schedule loads without an error", status == 200 and not errors),
            ("page shows \"No events scheduled for this period\"", message == "No events scheduled for this period"),
        ]
        actual = (f"GET /api/schedule returned {status} with {len(sched.get('blocks', [])) if isinstance(sched, dict) else sched} "
                  f"block(s). Page showed \"{message}\" and {said(errors)}.")
        return inputs, checks, actual
    finally:
        delete_student(API, s)


def tc_04_03(browser):
    a, b = new_student(API, "jo-0403"), new_student(API, "jo-b")
    try:
        due = at(next_day_in_week(), 22, 0)
        gen_status, _ = plan(b, [("Private", due)])
        with db() as conn:
            b_tasks = conn.execute("SELECT count(*) FROM tasks WHERE student_id = %s AND title = 'Private'",
                                   (b["id"],)).fetchone()[0]
            b_blocks = conn.execute("SELECT count(*) FROM study_blocks WHERE student_id = %s", (b["id"],)).fetchone()[0]
        context, page = signed_in_page(browser, a)
        open_schedule(page)
        errors = alerts(page)
        drawn = page.locator(".vs-cal .block.study").count()
        listed = page.locator(".vs-tasks .vs-task-main strong").all_inner_texts()
        private_on_page = page_says(page, "Private")
        context.close()
        _, a_tasks, _ = call(API, "GET", "/api/tasks", None, a["token"])
        _, a_sched, _ = call(API, "GET", "/api/schedule", None, a["token"])
        inputs = (f"Student B created \"Private\" due {shown(due)} and generated a plan. "
                  f"Student A, who has no data, then opened View Schedule.")
        checks = [
            ("student B's deadline and study blocks exist in the database", b_tasks == 1 and b_blocks > 0 and gen_status == 200),
            ("schedule loads for student A without an error", not errors),
            ("student A's view lists no deadlines and draws no blocks", listed == [] and drawn == 0),
            ("\"Private\" appears nowhere on student A's page", not private_on_page),
            ("API gives student A no tasks and no blocks", a_tasks == [] and a_sched.get("blocks") == []),
        ]
        actual = (f"B has {b_tasks} \"Private\" task and {b_blocks} block(s) in the database. A's page drew {drawn} "
                  f"block(s), listed {', '.join(listed) or 'no deadlines'} and {'showed' if private_on_page else 'did not show'} "
                  f"\"Private\". The API gave A {len(a_tasks)} task(s) and {len(a_sched.get('blocks', []))} block(s).")
        return inputs, checks, actual
    finally:
        delete_student(API, a)
        delete_student(API, b)


def tc_04_04(browser):
    s = new_student(OUTAGE, "jo-0404")
    try:
        relay.cut()
        status, body, _ = call(OUTAGE, "GET", "/api/schedule", None, s["token"])
        context, page = signed_in_page(browser, s)

        def to_outage_server(route):
            route.fulfill(response=route.fetch(url=route.request.url.replace(FRONT, OUTAGE), timeout=60_000))

        page.route(lambda url: urllib.parse.urlparse(url).path.startswith("/api/"), to_outage_server)
        open_schedule(page)
        errors = alerts(page)
        calendar = page.locator(".vs-cal").count()
        empty_message = page.locator(".vs-empty").count()
        retry = page.locator("[role=alert] button:has-text('Retry')").count()
        context.close()
        relay.restore()
        inputs = (f"Student signed up on the second server (port {OUTAGE_PORT}). Its database connection was then "
                  f"cut, and the student opened View Schedule with every /api request going to that server.")
        checks = [
            ("schedule request fails instead of returning an empty schedule", status >= 500),
            ("page shows an error, not an empty calendar", bool(errors) and calendar == 0 and empty_message == 0),
            ("page shows \"Schedule unavailable; please retry\"",
             any("Schedule unavailable; please retry" in e for e in errors)),
        ]
        actual = (f"GET /api/schedule returned {status} \"{body.get('error') if isinstance(body, dict) else body}\". "
                  f"Page showed {said(errors)} {'with' if retry else 'without'} a Retry button, "
                  f"{'a calendar' if calendar else 'no calendar'} and {'the' if empty_message else 'no'} empty-week message.")
        return inputs, checks, actual
    finally:
        relay.restore()
        for _ in range(10):
            if delete_student(OUTAGE, s) == 204:
                break
            time.sleep(1)


CASES = [
    ("TC-03-01", "Valid syllabus imports its deadline", UC3,
     "Upload succeeds. Exactly one task, Project 3 due at 23:59 on the syllabus date, is saved for the student. "
     "The saved deadline records the syllabus file it came from.", tc_03_01),
    ("TC-03-02", "Syllabus with no deadlines leaves existing data", UC3,
     "Upload succeeds with no error. The student's one existing task is still there and unchanged. "
     "No task is added.", tc_03_02),
    ("TC-03-03", "Invalid file is rejected", UC3,
     "Upload of bad.exe is rejected. No task is saved.", tc_03_03),
    ("TC-03-04", "Timeout during upload keeps existing data", UC3,
     "Upload reports a failure. The student's one existing task is still there and unchanged. "
     "No task is added.", tc_03_04),
    ("TC-04-01", "Deadlines are shown in date order", UC4,
     "Schedule loads. Both deadlines are listed, Earlier first, then Later.", tc_04_01),
    ("TC-04-02", "Empty schedule shows a no-events message", UC4,
     "Schedule loads without an error and shows \"No events scheduled for this period\".", tc_04_02),
    ("TC-04-03", "Another student's events are not shown", UC4_SECURE,
     "Schedule loads for student A and lists no deadlines. Student B's private deadline is not shown.", tc_04_03),
    ("TC-04-04", "Database outage shows an error, not an empty calendar", UC4,
     "Schedule does not load. The page shows \"Schedule unavailable; please retry\" instead of an empty calendar.",
     tc_04_04),
]


def leftover_students():
    with db() as conn:
        return [r[0] for r in conn.execute("SELECT email FROM students WHERE email LIKE 'temp-jo-%'").fetchall()]


if __name__ == "__main__":
    report = Report("JO", "Jonathan Lee (JO)", "JO live test results: UC-3 and UC-4")
    API = start_server(API_PORT, CRAMBERRY_AUTO_REPLAN=0)
    db_url = urllib.parse.urlparse(ENV["CRAMBERRY_DATABASE_URL"])
    relay = DatabaseRelay(db_url.hostname, db_url.port or 5432)
    OUTAGE = start_server(OUTAGE_PORT, CRAMBERRY_AUTO_REPLAN=0, CRAMBERRY_DATABASE_URL=relayed_database_url(relay))
    start_frontend()
    with sync_playwright() as pw:
        browser = pw.chromium.launch()
        for tc, title, requirement, expected, run in CASES:
            try:
                inputs, checks, actual = run(browser)
            except Exception as e:  # a crash is a failed case, not a stopped run
                inputs, checks, actual = "See the script.", [("case ran to completion", False)], f"Crashed with {e!r}."
            report.record(tc, title, requirement, inputs, expected, checks, actual)
        browser.close()
    left = leftover_students()
    print(f"temporary students left: {left or 'none'}")
    sys.exit(report.write(
        f"`http://127.0.0.1:{API_PORT}` with `CRAMBERRY_AUTO_REPLAN=0`, and a second copy on "
        f"`http://127.0.0.1:{OUTAGE_PORT}` whose database link TC-04-04 cuts",
        [("Frontend", f"Vite dev server on `{FRONT}`, proxying `/api` to port {API_PORT}"),
         ("Browser", f"Headless Chromium through Playwright for Python, time zone {TZ} (UTC-6)"),
         ("Temporary students left", ", ".join(left) or "none")]))
