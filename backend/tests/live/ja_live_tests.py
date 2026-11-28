#!/usr/bin/env python3
"""JA (Joshua Alston) live tests for Assignment 5: UC-15 Manage Study Preferences and
UC-16 Update Notification Settings (TC-15-01 to TC-15-04, TC-16-01 to TC-16-04 in
docs/testing/JA/test_cases.md).

Starts its own cramberry_server on port 8101 (Supabase from backend/.env) and a Vite dev
server for the frontend on port 3101, then drives the Study Planner settings, Create
Schedule, View Schedule and Notifications pages in Chromium through Playwright for Python.
Rows are checked in Supabase directly. Every test signs up its own temporary student
(temp-ja-...) and deletes it at the end.

TC-16-02 and TC-16-04 wait in real time for the once-a-minute reminder check, so a run
takes about 45 minutes. The other tests run while those two wait.

Usage (from the repo root; needs psycopg and playwright==1.49.1, and npm install in frontend/):
    TESTER="Ahnaf Adib (AA)" python3 backend/tests/live/ja_live_tests.py
Set SKIP_REMINDER_WAITS=1 to leave out TC-16-02 and TC-16-04 and finish in a few minutes.
"""

import atexit
import datetime as dt
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from live_common import ROOT, Report, call, db, delete_student, new_student, start_server  # noqa: E402

from playwright.sync_api import TimeoutError as PlaywrightTimeout, sync_playwright  # noqa: E402

API_PORT, WEB_PORT = 8101, 3101
WEB = f"http://localhost:{WEB_PORT}"
OFFSET = -360
DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
DAY_NAMES = dict(zip(DAYS, ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]))
SAVED_PREFS = "Saved. Cramberry is rebuilding your schedule."

students = []


def start_frontend(api_base):
    env = dict(os.environ, PORT=str(WEB_PORT), CRAMBERRY_API_URL=api_base)
    log = open(pathlib.Path(tempfile.gettempdir()) / f"cramberry_vite_{WEB_PORT}.log", "w")
    proc = subprocess.Popen(["npx", "vite", "--port", str(WEB_PORT)], cwd=ROOT / "frontend", env=env,
                            stdout=log, stderr=subprocess.STDOUT)
    atexit.register(proc.terminate)
    for _ in range(150):
        if proc.poll() is not None:
            sys.exit(f"Vite exited with code {proc.returncode}; see {log.name}")
        try:
            with urllib.request.urlopen(WEB + "/login", timeout=2) as r:
                if r.status == 200:
                    return
        except OSError:
            pass
        time.sleep(0.2)
    sys.exit(f"Vite did not answer on {WEB} within 30 s; see {log.name}")


def student(tag):
    s = new_student(API, f"ja-{tag}", utc_offset_minutes=OFFSET)
    students.append(s)
    return s


def local_now():
    return dt.datetime.now(dt.timezone.utc).replace(tzinfo=None) + dt.timedelta(minutes=OFFSET)


def week_day_date(day):
    now = local_now()
    return (now - dt.timedelta(days=now.weekday())).date() + dt.timedelta(days=DAYS.index(day))


def days_ahead(start):
    """Days left this week (student's local time) whose `start` o'clock is still at least 10 min away."""
    h, m = map(int, start.split(":"))
    limit = local_now() + dt.timedelta(minutes=10)
    return [d for d in DAYS if dt.datetime.combine(week_day_date(d), dt.time(h, m)) > limit]


def utc_iso(day, clock):
    h, m = map(int, clock.split(":"))
    local = dt.datetime.combine(week_day_date(day), dt.time(h, m))
    return (local - dt.timedelta(minutes=OFFSET)).strftime("%Y-%m-%dT%H:%M:%SZ")


def day_label(day):
    return f"{DAY_NAMES[day]} {week_day_date(day):%d %b}"


def mins(clock):
    h, m = clock.split(":")
    return int(h) * 60 + int(m)


def parse_utc(iso):
    return dt.datetime.strptime(iso[:16], "%Y-%m-%dT%H:%M").replace(tzinfo=dt.timezone.utc)


def open_page(browser, s, path):
    ctx = browser.new_context(viewport={"width": 1280, "height": 900}, timezone_id="America/Chicago")
    page = ctx.new_page()
    page.goto(WEB + "/login")
    page.evaluate("t => localStorage.setItem('cramberry.token', t)", s["token"])
    page.goto(WEB + path)
    return page


def settle_runs(s, quiet=6, limit=90):
    """Waits until background re-planning for this student has stopped adding generation_runs."""
    last, since, start = None, time.time(), time.time()
    while time.time() - start < limit:
        with db() as conn:
            n = conn.execute("SELECT count(*) FROM generation_runs WHERE student_id = %s", (s["id"],)).fetchone()[0]
        if n != last:
            last, since = n, time.time()
        elif time.time() - since >= quiet:
            return n
        time.sleep(1)
    return last


def prefs_row(sid):
    with db() as conn:
        row = conn.execute("SELECT preferences, updated_at FROM study_preferences WHERE student_id = %s",
                           (sid,)).fetchone()
    return (json.loads(row[0]), row[1]) if row else (None, None)


def other_prefs_rows(sid, bystander):
    """Rows of every other student except other scripts' temporary ones, which change while this runs."""
    with db() as conn:
        return conn.execute(
            "SELECT p.student_id, p.preferences, p.updated_at FROM study_preferences p "
            "JOIN students s ON s.id = p.student_id WHERE p.student_id <> %s "
            "AND (s.email NOT LIKE 'temp-%%' OR p.student_id = %s) ORDER BY p.student_id", (sid, bystander)).fetchall()


def notif_row(sid):
    with db() as conn:
        return conn.execute(
            "SELECT enabled, email, desktop, sms, phone_number, deadline_reminders, deadline_lead_minutes, updated_at "
            "FROM notification_settings WHERE student_id = %s", (sid,)).fetchone()


def reminders(sid, title):
    with db() as conn:
        return conn.execute("SELECT channel, subject, body, sent_at FROM notifications WHERE student_id = %s "
                            "AND (subject LIKE %s OR body LIKE %s) ORDER BY sent_at",
                            (sid, f"%{title}%", f"%{title}%")).fetchall()


def inbox_items(page):
    page.wait_for_selector('section[aria-label="Inbox"] :is(ul.ac-inbox, p.hint)')
    return [t.strip() for t in page.locator('section[aria-label="Inbox"] li').all_inner_texts()]


def ranges(items):
    return sorted((r["day"], r["start"], r["end"]) for r in items)


def toggle(page, name):
    return page.locator("button.ac-toggle").filter(has_text=name)


def checked(locator):
    return locator.get_attribute("aria-checked") == "true"


def attempt(fn, *args):
    try:
        return fn(*args)
    except Exception as e:  # a crash is a failed test, not a stopped run
        return "See the script.", "See the test case.", [("test ran to the end", False)], \
            f"The test stopped with {type(e).__name__}: {e}"


# ---------------------------------------------------------------- UC-15

def tc_15_01(browser):
    s, other = student("15-01"), student("15-01-other")
    call(API, "PUT", "/api/settings/preferences",
         {"studyHours": [{"day": "Tue", "start": "09:00", "end": "12:00"}], "maxStudyMinutesPerDay": 120,
          "sessionMinutes": 45, "breakMinutes": 10, "personalTime": []}, other["token"])
    settle_runs(other)
    others_before = other_prefs_rows(s["id"], other["id"])
    page = open_page(browser, s, "/planner/settings")
    page.wait_for_selector(".avail")
    for day in ("Mon", "Wed", "Fri"):
        for hour in ("4 pm to 5 pm", "5 pm to 6 pm", "6 pm to 7 pm"):
            page.locator(f'[aria-label^="{DAY_NAMES[day]} {hour}:"]').click()
    page.select_option("#ss-max", "180")
    page.select_option("#ss-session", "60")
    page.fill("#ss-break", "15")
    page.locator('section[aria-label="Personal time"] button:has-text("Add")').click()
    form = page.locator('form[aria-label="Add personal time"]')
    form.locator("label.check", has_text="Sun").click()
    page.fill("#ss-add-start", "18:00")
    page.fill("#ss-add-end", "19:00")
    form.locator('button[type="submit"]').click()
    page.click('button:has-text("Save settings")')
    try:
        page.locator(f"text={SAVED_PREFS}").first.wait_for(timeout=10_000)
        confirmed = True
    except PlaywrightTimeout:
        confirmed = False
    page.reload()
    page.wait_for_selector(".avail")

    def cell(day, hour):
        return page.locator(f'[aria-label^="{DAY_NAMES[day]} {hour}:"]').get_attribute("aria-label").split(": ")[-1]

    study_cells = {(d, h): cell(d, h) for d in ("Mon", "Wed", "Fri") for h in ("4 pm to 5 pm", "5 pm to 6 pm", "6 pm to 7 pm")}
    total = page.locator("p.total").inner_text()
    sun = cell("Sun", "6 pm to 7 pm")
    shown = (page.input_value("#ss-max"), page.input_value("#ss-session"), page.input_value("#ss-break"))
    personal_list = page.locator('section[aria-label="Personal time"] li span').all_inner_texts()
    saved, _ = prefs_row(s["id"])
    others_after = other_prefs_rows(s["id"], other["id"])
    want_study = [(d, "16:00", "19:00") for d in ("Fri", "Mon", "Wed")]
    checks = [
        ("save confirmation shown", confirmed),
        ("after reload Mon, Wed and Fri 4 to 7 pm are study hours", all(v == "Study hours" for v in study_cells.values())),
        ("after reload no other hour is marked as study (9 study hours in total)", total.startswith("9 study hours marked")),
        ("after reload Most study per day is 3 hours, Session length 60 min, Break 15 min", shown == ("180", "60", "15 min")),
        ("after reload Sun 6 to 7 pm is personal time", sun == "Personal time" and personal_list == ["Personal time, Sun, 6 to 7 pm"]),
        ("study_preferences row has study hours Mon, Wed, Fri 16:00-19:00",
         saved is not None and ranges(saved["studyHours"]) == sorted(want_study)),
        ("study_preferences row has max 180, session 60, break 15",
         saved is not None and (saved["maxStudyMinutesPerDay"], saved["sessionMinutes"], saved["breakMinutes"]) == (180, 60, 15)),
        ("study_preferences row has personal time Sun 18:00-19:00",
         saved is not None and ranges(saved["personalTime"]) == [("Sun", "18:00", "19:00")]),
        ("no other student's row changed (a second test student's saved row and every non-test account's row)",
         others_before == others_after and any(r[0] == other["id"] for r in others_before)),
    ]
    actual = (f"\"{SAVED_PREFS}\" appeared. After the reload the page showed \"{total.split('.')[0]}\", "
              f"{', '.join(sorted({d for (d, _), v in study_cells.items() if v == 'Study hours'}, key=DAYS.index))} 4 to 7 pm as study hours, "
              f"max {shown[0]} min, session {shown[1]} min, break {shown[2]}, personal time {personal_list}. "
              f"The row holds study hours {ranges(saved['studyHours']) if saved else None}, max {saved and saved['maxStudyMinutesPerDay']}, "
              f"session {saved and saved['sessionMinutes']}, break {saved and saved['breakMinutes']}, personal time "
              f"{ranges(saved['personalTime']) if saved else None}. Other accounts' rows unchanged: {others_before == others_after}.")
    inputs = ("Study Planner settings page. Painted Mon, Wed and Fri 4 pm to 7 pm with the Study hours tool, Most study per "
              "day 3 hours, Session length 60 min, Break 15 min, added personal time Sun 6 pm to 7 pm with Add, clicked "
              "Save settings, reloaded, then read the study_preferences row.")
    expected = ("A save confirmation appears. After the reload every value is still shown. The study_preferences row holds the "
                "same values and no other student's row has changed.")
    return inputs, expected, checks, actual


def tc_15_02(browser):
    s = student("15-02")
    tok = s["token"]
    ahead = days_ahead("18:00")
    assert ahead, "no 6 pm left this week to put the study block on"
    day = ahead[0]
    call(API, "PUT", "/api/settings/preferences",
         {"studyHours": [{"day": day, "start": "18:00", "end": "19:30"}], "maxStudyMinutesPerDay": 180,
          "sessionMinutes": 90, "breakMinutes": 15, "personalTime": []}, tok)
    st, task, _ = call(API, "POST", "/api/tasks", {"title": "CS 3354 Review", "dueAt": utc_iso("Sun", "23:59"),
                                                   "estimatedMinutes": 90}, tok)
    assert st == 201, (st, task)
    settle_runs(s)
    call(API, "POST", "/api/agent/generate", {}, tok)
    runs_before = settle_runs(s)
    _, sched, _ = call(API, "GET", "/api/schedule", None, tok)
    before = [(b["title"], b["day"], b["start"], b["end"]) for b in sched["blocks"]]
    assert before == [("CS 3354 Review", day, "18:00", "19:30")], f"precondition schedule is {before}"
    with db() as conn:
        rows_before = conn.execute("SELECT title, day, start_minute, end_minute FROM study_blocks WHERE student_id = %s",
                                   (s["id"],)).fetchall()

    page = open_page(browser, s, "/planner/settings")
    page.wait_for_selector(".avail")
    page.locator('section[aria-label="Personal time"] button:has-text("Add")').click()
    form = page.locator('form[aria-label="Add personal time"]')
    form.locator("label.check", has_text=day).click()
    page.fill("#ss-add-start", "18:00")
    page.fill("#ss-add-end", "20:00")
    form.locator('button[type="submit"]').click()
    page.click('button:has-text("Save settings")')
    alert = page.locator(".ss-conflict")
    alert.wait_for(timeout=10_000)
    items = alert.locator("li").all_inner_texts()
    time.sleep(8)
    runs_after = settle_runs(s)

    view = page.context.new_page()
    view.goto(WEB + "/schedule")
    view.wait_for_selector(".cal .block.study", timeout=15_000)
    shown = [view.locator(".cal .block.study").nth(i).get_attribute("aria-label")
             for i in range(view.locator(".cal .block.study").count())]
    with db() as conn:
        rows_after = conn.execute("SELECT title, day, start_minute, end_minute FROM study_blocks WHERE student_id = %s",
                                  (s["id"],)).fetchall()
    want_item = f"CS 3354 Review, {day}, 6 to 7:30 pm"
    checks = [
        (f"conflict message names \"{want_item}\"", items == [want_item]),
        ("GenerateSchedule() not called (no new generation_runs row)", runs_after == runs_before),
        ("View Schedule still shows the one CS 3354 Review block at the same time",
         shown == [f"CS 3354 Review, {day} 6 to 7:30 pm. Open in the editor"]),
        ("study_blocks row unchanged", rows_after == rows_before and len(rows_before) == 1),
    ]
    actual = (f"The page said \"Nothing was saved\" and listed {items}. generation_runs rows {runs_before} before and {runs_after} after. "
              f"View Schedule showed {shown}. study_blocks {rows_before} before, {rows_after} after.")
    inputs = (f"Accepted schedule with one block \"CS 3354 Review\" on {day_label(day)} 6:00 pm to 7:30 pm (made by "
              f"generating a plan for a 90 min task). On Study Planner settings, added personal time {day} 6:00 pm to "
              f"8:00 pm with Add, clicked Save settings, then opened View Schedule.")
    expected = (f"A conflict message names \"CS 3354 Review\" on {day} from 6:00 to 7:30 PM. GenerateSchedule() is not "
                "called. On View Schedule the block is still there and unchanged.")
    return inputs, expected, checks, actual


def tc_15_03(browser):
    s = student("15-03")
    st, _, _ = call(API, "PUT", "/api/settings/preferences",
                    {"studyHours": [], "maxStudyMinutesPerDay": 180, "sessionMinutes": 90, "breakMinutes": 15,
                     "personalTime": []}, s["token"])
    assert st == 200, st
    settle_runs(s)
    row_before = prefs_row(s["id"])
    page = open_page(browser, s, "/planner/settings")
    page.wait_for_selector(".avail")
    puts = []
    page.on("request", lambda r: puts.append(r.url) if r.method == "PUT" and "/api/settings/preferences" in r.url else None)
    page.fill("#ss-break", "120")
    err = page.locator("#ss-break-err")
    err_text = err.inner_text() if err.count() else ""
    next_to_field = err.count() == 1 and page.locator("#ss-break").get_attribute("aria-describedby") == "ss-break-err"
    save = page.locator('button:has-text("Save settings")')
    disabled = save.is_disabled()
    save.click(force=True)
    time.sleep(2)
    row_after = prefs_row(s["id"])
    page.reload()
    page.wait_for_selector(".avail")
    break_after = page.input_value("#ss-break")
    checks = [
        ("message \"Break can't be longer than your 90 min session\" next to the field",
         next_to_field and err_text.startswith("Break can't be longer than your 90 min session")),
        ("Save settings is disabled", disabled),
        ("nothing written to the database (no save request, row unchanged)", not puts and row_after == row_before),
        ("after the reload the break is still 15 min", break_after == "15 min"),
    ]
    actual = (f"The page showed \"{err_text}\" under the break field and Save settings disabled={disabled}. "
              f"Save requests sent: {len(puts)}. Row breakMinutes {row_after[0]['breakMinutes']}, updated_at unchanged: "
              f"{row_after[1] == row_before[1]}. After the reload the break showed \"{break_after}\".")
    inputs = ("Saved preferences session 90 min, break 15 min. On Study Planner settings, typed 120 in Break after each "
              "session, tried to click Save settings, reloaded, then read the study_preferences row.")
    expected = ("The message \"Break can't be longer than your 90 min session\" appears next to the field and Save settings "
                "is disabled. Nothing is written to the database. After the reload the break is still 15 min.")
    return inputs, expected, checks, actual


def tc_15_04(browser):
    s = student("15-04")
    tok = s["token"]
    days = days_ahead("16:00")[:5]
    assert days, "no 4 pm left this week"
    personal_day = days[min(2, len(days) - 1)]
    prefs = {"studyHours": [{"day": d, "start": "16:00", "end": "19:00"} for d in days],
             "maxStudyMinutesPerDay": 120, "sessionMinutes": 60, "breakMinutes": 15,
             "personalTime": [{"day": personal_day, "start": "17:00", "end": "18:00"}]}
    assert call(API, "PUT", "/api/settings/preferences", prefs, tok)[0] == 200
    due_days = [days[0], days[len(days) // 2], days[-1]]
    tasks = {}
    for i, d in enumerate(due_days, 1):
        st, t, _ = call(API, "POST", "/api/tasks", {"title": f"JA assignment {i}", "dueAt": utc_iso(d, "23:59"),
                                                    "estimatedMinutes": 60}, tok)
        assert st == 201, (st, t)
        tasks[t["id"]] = t
    settle_runs(s)
    page = open_page(browser, s, "/schedule/create")
    page.click('button:has-text("Generate my schedule")')
    page.wait_for_selector("text=/Planned by/", timeout=60_000)
    engine = page.locator(".cs-engine").inner_text()
    settle_runs(s)
    _, sched, _ = call(API, "GET", "/api/schedule", None, tok)
    blocks = sched["blocks"]
    page.goto(WEB + "/schedule")
    if blocks:
        page.wait_for_selector(".cal .block.study", timeout=15_000)
    drawn = page.locator(".cal .block.study").count()

    outside, personal_hits, too_long, late, by_day = [], [], [], [], {}
    for b in blocks:
        s_, e_ = mins(b["start"]), mins(b["end"])
        label = f"{b['title']} {b['day']} {b['start']}-{b['end']}"
        if b["day"] not in days or s_ < 16 * 60 or e_ > 19 * 60:
            outside.append(label)
        if b["day"] == personal_day and s_ < 18 * 60 and 17 * 60 < e_:
            personal_hits.append(label)
        if e_ - s_ > 60:
            too_long.append(label)
        task = tasks.get(b["assignmentId"])
        if task is None or parse_utc(b["endsAt"]) > parse_utc(task["dueAt"]):
            late.append(label)
        by_day.setdefault(b["day"], []).append((s_, e_))
    over_max = [d for d, spans in by_day.items() if sum(e - s for s, e in spans) > 120]
    short_gaps = [d for d, spans in by_day.items()
                  for (_, e1), (s2, _) in zip(sorted(spans), sorted(spans)[1:]) if s2 - e1 < 15]
    unscheduled = [t["title"] for tid, t in tasks.items() if not any(b["assignmentId"] == tid for b in blocks)]
    checks = [
        ("plan generated and drawn on View Schedule", len(blocks) > 0 and drawn == len(blocks)),
        (f"every block is on a study day ({', '.join(days)}) between 4:00 and 7:00 pm", not outside),
        (f"no block overlaps {personal_day} 5:00 to 6:00 pm", not personal_hits),
        ("no day has more than 2 hours of study", not over_max),
        ("no block is longer than 60 min", not too_long),
        ("at least 15 min between back-to-back blocks", not short_gaps),
        ("all three assignments are scheduled before their due dates", not unscheduled and not late),
    ]
    listing = "; ".join(f"{b['title']} {b['day']} {b['start']}-{b['end']}" for b in blocks)
    problems = outside + personal_hits + too_long + late + over_max + short_gaps + unscheduled
    actual = (f"{engine}. {len(blocks)} blocks: {listing}. Rule problems: {', '.join(problems) if problems else 'none'}.")
    inputs = (f"Saved preferences: study hours {', '.join(day_label(d) for d in days)} 4:00 to 7:00 pm, most study per day "
              f"2 hours, session 60 min, break 15 min, personal time {personal_day} 5:00 to 6:00 pm. Three 60 min "
              f"assignments due {', '.join(f'{day_label(d)} 11:59 pm' for d in due_days)}. Clicked Generate my schedule on "
              "Create Schedule, then read the plan on View Schedule.")
    expected = (f"Every block falls on {', '.join(days)} between 4:00 PM and 7:00 PM. No block overlaps {personal_day} "
                "5:00 to 6:00 PM. No day has more than 2 hours, no block is longer than 60 min, at least 15 min between "
                "back-to-back blocks. All three assignments are scheduled before their due dates.")
    return inputs, expected, checks, actual


# ---------------------------------------------------------------- UC-16

def tc_16_01(browser):
    s = student("16-01")
    page = open_page(browser, s, "/account/notifications")
    page.wait_for_selector('form[aria-label="Notification settings"]')
    page.locator('[role="radiogroup"][aria-label="Notifications"] [role="radio"]', has_text="On").click()
    toggle(page, "Email").click()
    toggle(page, "Deadline Reminders").click()
    page.select_option("#ac-lead", "1440")
    page.click('button:has-text("Save settings")')
    msg = page.locator(".ac-saved")
    msg.wait_for(timeout=10_000)
    msg_text = msg.inner_text()
    page.reload()
    page.wait_for_selector('form[aria-label="Notification settings"]')
    on = checked(page.locator('[role="radio"]', has_text="On"))
    email, reminders_on = checked(toggle(page, "Email")), checked(toggle(page, "Deadline Reminders"))
    lead = page.input_value("#ac-lead")
    desktop, sms = checked(toggle(page, "Desktop Notification")), checked(toggle(page, "SMS"))
    row = notif_row(s["id"])
    checks = [
        ("confirmation message appears", msg_text == "Notification settings saved."),
        ("after reload Enable Notifications, Email and Deadline Reminders (1 day) are on",
         on and email and reminders_on and lead == "1440"),
        ("database row matches what was saved (on, email only, reminders 1440 min)",
         row is not None and tuple(row[:7]) == (1, 1, 0, 0, "", 1, 1440)),
    ]
    actual = (f"\"{msg_text}\" appeared. After the reload enabled={on}, email={email}, desktop={desktop}, sms={sms}, "
              f"deadline reminders={reminders_on} at {lead} min. Row (enabled, email, desktop, sms, phone, reminders, "
              f"lead) = {tuple(row[:7]) if row else None}.")
    inputs = ("Notifications page, notifications off. Clicked On, turned on Email and Deadline Reminders, chose 1 day "
              "before, clicked Save settings, reloaded, then read the notification_settings row.")
    expected = ("A confirmation message appears. After the reload Enable Notifications, Email and Deadline Reminders (1 Day) "
                "are still on. The database row matches what was saved.")
    return inputs, expected, checks, actual


def tc_16_03(browser):
    s = student("16-03")
    st, _, _ = call(API, "PUT", "/api/settings/notifications",
                    {"enabled": True, "email": True, "desktop": False, "sms": False, "phoneNumber": "",
                     "deadlineReminders": False, "deadlineLeadMinutes": 1440}, s["token"])
    assert st == 200, st
    row_before = notif_row(s["id"])
    page = open_page(browser, s, "/account/notifications")
    page.wait_for_selector('form[aria-label="Notification settings"]')
    toggle(page, "SMS").click()
    page.fill("#ac-phone", "12345")
    page.click('button:has-text("Save settings")')
    page.wait_for_selector('.summary [role="alert"], .ac-saved', timeout=10_000)
    phone_err = page.locator("#ac-phone ~ p.errtext")
    err_text = phone_err.inner_text() if phone_err.count() else ""
    saved_msg = page.locator(".ac-saved").count()
    row_after = notif_row(s["id"])
    page.reload()
    page.wait_for_selector('form[aria-label="Notification settings"]')
    sms, email = checked(toggle(page, "SMS")), checked(toggle(page, "Email"))
    desktop, phone = checked(toggle(page, "Desktop Notification")), page.input_value("#ac-phone")
    checks = [
        ("error \"Enter a valid phone number\" shown", err_text == "Enter a valid phone number" and saved_msg == 0),
        ("not saved (row unchanged)", row_after == row_before),
        ("after reload SMS is off and no phone number is stored", not sms and phone == ""
         and row_after is not None and row_after[3] == 0 and row_after[4] == ""),
        ("after reload Email is still the only source", email and not desktop and not sms
         and row_after is not None and (row_after[1], row_after[2], row_after[3]) == (1, 0, 0)),
    ]
    actual = (f"The page showed \"{err_text}\" under the phone field. Row unchanged: {row_after == row_before}. After the "
              f"reload sms={sms}, phone=\"{phone}\", email={email}, desktop={desktop}. Row (enabled, email, desktop, sms, "
              f"phone) = {tuple(row_after[:5]) if row_after else None}.")
    inputs = ("Notifications on with Email as the only source, no phone number. Turned on SMS, typed 12345 as the phone "
              "number, clicked Save settings, reloaded, then read the notification_settings row.")
    expected = ("An error such as \"Enter a valid phone number\" appears and nothing is saved. After the reload SMS is off, "
                "no phone number is stored and Email is still the only source.")
    return inputs, expected, checks, actual


def due_24h10m():
    due = (dt.datetime.now(dt.timezone.utc) + dt.timedelta(hours=24, minutes=10)).replace(second=0, microsecond=0)
    return due, due.strftime("%Y-%m-%dT%H:%M:%SZ")


def start_16_02(browser):
    s = student("16-02")
    tok = s["token"]
    call(API, "PUT", "/api/settings/notifications",
         {"enabled": True, "email": True, "desktop": True, "sms": False, "phoneNumber": "",
          "deadlineReminders": True, "deadlineLeadMinutes": 1440}, tok)
    due, due_iso = due_24h10m()
    st, task, _ = call(API, "POST", "/api/tasks", {"title": "Test HW 2", "dueAt": due_iso}, tok)
    assert st == 201, (st, task)
    page = open_page(browser, s, "/account/notifications")
    page.wait_for_selector('form[aria-label="Notification settings"]')
    page.locator('[role="radio"]', has_text="Off").click()
    page.click('button:has-text("Save settings")')
    msg = page.locator(".ac-saved")
    msg.wait_for(timeout=10_000)
    state = {"s": s, "due": due, "msg": msg.inner_text(),
             "off": checked(page.locator('[role="radio"]', has_text="Off")),
             "inbox_before": reminders(s["id"], "Test HW 2")}
    page.context.close()
    return state


def finish_16_02(browser, state):
    s, due = state["s"], state["due"]
    would_send = due - dt.timedelta(minutes=1440)
    wait_until(would_send + dt.timedelta(minutes=30))
    page = open_page(browser, s, "/account/notifications")
    shown = [t for t in inbox_items(page) if "Test HW 2" in t]
    rows = reminders(s["id"], "Test HW 2")
    checks = [
        ("confirmation message appears", state["msg"] == "Notification settings saved."),
        ("settings show notifications as off", state["off"]),
        ("test inbox empty before the wait", state["inbox_before"] == []),
        ("no email or desktop notification about Test HW 2 by 30 min after the send time",
         not shown and not rows),
    ]
    actual = (f"\"{state['msg']}\" appeared and Off was selected. At {local(would_send + dt.timedelta(minutes=30))} the "
              f"inbox showed {len(shown)} items about Test HW 2 and the notifications table had {len(rows)} rows for it.")
    inputs = (f"Notifications on with Email and Desktop Notification, Deadline Reminders 1 day. \"Test HW 2\" due "
              f"{local(due)} (24 h 10 min after setup). On the Notifications page clicked Off and Save settings, waited "
              f"until {local(would_send + dt.timedelta(minutes=30))}, then checked the Notifications page inbox and the "
              "notifications table.")
    expected = ("A confirmation message appears and the settings show notifications as off. No email or desktop "
                "notification about \"Test HW 2\" is sent.")
    return inputs, expected, checks, actual


def start_16_04(browser):
    s = student("16-04")
    tok = s["token"]
    call(API, "PUT", "/api/settings/notifications",
         {"enabled": True, "email": True, "desktop": False, "sms": False, "phoneNumber": "",
          "deadlineReminders": True, "deadlineLeadMinutes": 1440}, tok)
    due, due_iso = due_24h10m()
    local_due = due + dt.timedelta(minutes=OFFSET)
    page = open_page(browser, s, "/schedule/create")
    page.fill("#cs-t-title", "Test HW 1")
    page.fill("#cs-t-date", f"{local_due:%Y-%m-%d}")
    page.fill("#cs-t-time", f"{local_due:%H:%M}")
    page.click('button:has-text("Add task")')
    page.wait_for_selector("text=Test HW 1")
    created = time.time()
    tasks = call(API, "GET", "/api/tasks", None, tok)[1]
    page.context.close()
    assert [t["dueAt"] for t in tasks] == [due_iso], f"task saved as {tasks}"
    return {"s": s, "due": due, "created": created, "inbox_before": reminders(s["id"], "Test HW 1")}


def finish_16_04(browser, state):
    s, due = state["s"], state["due"]
    expected_at = due - dt.timedelta(minutes=1440)
    looks = []
    for after in (15, 30):
        wait_until(dt.datetime.fromtimestamp(state["created"], dt.timezone.utc) + dt.timedelta(minutes=after))
        page = open_page(browser, s, "/account/notifications")
        looks.append(([t for t in inbox_items(page) if "Test HW 1" in t], reminders(s["id"], "Test HW 1")))
        page.context.close()
    first_ui, first_rows = looks[0]
    second_ui, second_rows = looks[1]
    emails = [r for r in first_rows if r[0] == "email"]
    due_text = due.strftime("%Y-%m-%d %H:%M UTC")
    sent = dt.datetime.fromtimestamp(emails[0][3] * 60, dt.timezone.utc) if emails else None
    checks = [
        ("test inbox empty when the task was created", state["inbox_before"] == []),
        ("after 15 min exactly one reminder email about Test HW 1", len(emails) == 1 and len(first_ui) >= 1),
        ("sent 24 h before the due time, within 5 min",
         sent is not None and abs((sent - expected_at).total_seconds()) <= 300),
        ("names Test HW 1 and shows its due date and time",
         bool(emails) and "Test HW 1" in emails[0][1] and due_text in emails[0][2]),
        ("no duplicate after 15 more min", [r for r in second_rows if r[0] == "email"] == emails
         and len(second_ui) == len(first_ui)),
    ]
    actual = (f"After 15 min the inbox showed {first_ui} and the table had {len(emails)} email reminder(s)"
              + (f", sent {local(sent)} with body \"{emails[0][2]}\"" if emails else "")
              + f". After 30 min there were {len([r for r in second_rows if r[0] == 'email'])} email reminder(s).")
    inputs = (f"Settings from TC-16-01 (on, Email, Deadline Reminders 1 day). Added \"Test HW 1\" on Create Schedule, due "
              f"{local(due)} (24 h 10 min ahead). Checked the Notifications page inbox and the notifications table 15 and "
              "30 min later.")
    expected = (f"Exactly one reminder email arrives about 24 hours before the due date (around {local(expected_at)}, within "
                "5 min). It names \"Test HW 1\" and shows its due date and time. No duplicate arrives in step 4.")
    return inputs, expected, checks, actual


def local(t):
    return (t + dt.timedelta(minutes=OFFSET)).strftime("%a %d %b %I:%M %p").replace(" 0", " ") + " Central"


def wait_until(t):
    while dt.datetime.now(dt.timezone.utc) < t:
        time.sleep(min(20, max(0.5, (t - dt.datetime.now(dt.timezone.utc)).total_seconds())))


def leftover_students():
    with db() as conn:
        return conn.execute("SELECT count(*) FROM students WHERE email LIKE 'temp-ja-%%'").fetchone()[0]


if __name__ == "__main__":
    API = start_server(API_PORT)
    start_frontend(API)
    report = Report("JA", "Joshua Alston (JA)", "JA live test results: UC-15 and UC-16")
    reqs = {
        "TC-15-01": "UC-15, FR-7, NFR-2",
        "TC-15-02": "UC-15 CheckForConflicts() exception, Show Conflict",
        "TC-15-03": "UC-15 validation, break not longer than a session",
        "TC-15-04": "UC-15 and UC-5, FR-16",
        "TC-16-01": "UC-16, FR-12",
        "TC-16-02": "UC-16, FR-13",
        "TC-16-03": "UC-16 ValidatePreferences() exception, FR-12",
        "TC-16-04": "UC-16 timed alerts",
    }
    titles = {
        "TC-15-01": "Save valid study preferences", "TC-15-02": "Personal time conflict",
        "TC-15-03": "Break longer than session", "TC-15-04": "Schedule follows preferences",
        "TC-16-01": "Opt in to email reminders", "TC-16-02": "Opt out of all notifications",
        "TC-16-03": "Invalid phone number", "TC-16-04": "Reminder sent on time",
    }
    results = {}
    with sync_playwright() as p:
        browser = p.chromium.launch()
        try:
            waiting = {}
            waited = () if os.environ.get("SKIP_REMINDER_WAITS") else (("TC-16-02", start_16_02), ("TC-16-04", start_16_04))
            for tc, start in waited:
                try:
                    waiting[tc] = start(browser)
                except Exception as e:
                    results[tc] = attempt(lambda: (_ for _ in ()).throw(e))
            for tc, fn in (("TC-15-01", tc_15_01), ("TC-15-02", tc_15_02), ("TC-15-03", tc_15_03),
                           ("TC-15-04", tc_15_04), ("TC-16-01", tc_16_01), ("TC-16-03", tc_16_03)):
                results[tc] = attempt(fn, browser)
            for tc, finish in (("TC-16-04", finish_16_04), ("TC-16-02", finish_16_02)):
                if tc in waiting:
                    results[tc] = attempt(finish, browser, waiting[tc])
        finally:
            for s in students:
                print(f"  temporary student {s['email']} deleted -> {delete_student(API, s)}")
            browser.close()
    for tc in [t for t in titles if t in results]:
        report.record(tc, titles[tc], reqs[tc], *results[tc])
    left = leftover_students()
    print(f"temp-ja students left in the database: {left}")
    code = report.write(f"port {API_PORT}; frontend Vite dev server on port {WEB_PORT}",
                        [("Browser", "Chromium through Playwright for Python 1.49.1, time zone America/Chicago"),
                         ("Temporary students left after the run", str(left))])
    sys.exit(code or (1 if left else 0))
