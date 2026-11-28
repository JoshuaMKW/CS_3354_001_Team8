#!/usr/bin/env python3
"""NK (Nishanth Kandula) live tests for Assignment 5: UC-5 Generate AI Study Schedule and
UC-7 Interact with AI Chatbot (TC-05-01 to TC-05-04, TC-07-01 to TC-07-04).

Starts its own cramberry_server on port 8102 (backend/.env: Supabase and OpenRouter) with
CRAMBERRY_AUTO_REPLAN=0, so a schedule only changes when the step under test changes it.
Each case signs up a temporary student (temp-nk-...), drives the API the pages call, checks
the rows in Supabase, and deletes the student again.

The cases' weekdays are moved onto the next two days of the current week whose 4 PM is
still ahead in the student's local time (UTC-6), because the server only plans the rest of
the current week. A fixed class is entered as personal time, the app's only kind of fixed
commitment the planner must avoid.

Run from the repo root:
    TESTER="Ahnaf Adib (AA)" <venv>/bin/python backend/tests/live/nk_live_tests.py
"""

import datetime as dt
import re
import sys
import time

from live_common import call, db, delete_student, new_student, start_server, Report

PORT = 8102
OFFSET = -360
TAG = "nk"
UC5 = "UC-5 Generate AI Study Schedule, FR-16 (AI builds daily and weekly study schedules from tasks, deadlines and availability)"
UC7_ASK = "UC-7 Interact with AI Chatbot, FR-20 (chatbot answers questions about assignments and schedules)"
UC7_DO = "UC-7 Interact with AI Chatbot, FR-20 (chatbot as a shortcut to create tasks or edit the schedule)"
CHAT_PATH = "POST /api/agent/chat, the message box on the AI Assistant page"
CONFIRM = re.compile(r"\b(confirm|shall i|should i|do you want|would you like)\b", re.I)
DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]


class Day:
    def __init__(self, date):
        self.date = date
        self.name = DAY_NAMES[date.weekday()]
        self.full = date.strftime("%A")
        self.label = f"{self.full} {date.strftime('%b')} {date.day}"

    def utc(self, hour, minute=0):
        local = dt.datetime.combine(self.date, dt.time(hour, minute))
        return (local - dt.timedelta(minutes=OFFSET)).strftime("%Y-%m-%dT%H:%M:00Z")


def days_ahead(margin_minutes=45):
    now_local = dt.datetime.now(dt.timezone.utc).replace(tzinfo=None) + dt.timedelta(minutes=OFFSET)
    today = now_local.date()
    sunday = today + dt.timedelta(days=6 - today.weekday())
    out = []
    d = today
    while d <= sunday:
        if dt.datetime.combine(d, dt.time(16, 0)) > now_local + dt.timedelta(minutes=margin_minutes):
            out.append(Day(d))
        d += dt.timedelta(days=1)
    return out


def hm(clock):
    h, m = clock.split(":")
    return int(h) * 60 + int(m)


def span(r):
    return f"{r['day']} {r['start']}-{r['end']}"


def prefs(study, personal=()):
    return {"studyHours": [{"day": d.name, "start": s, "end": e} for d, s, e in study],
            "personalTime": [{"day": d.name, "start": s, "end": e} for d, s, e in personal],
            "sessionMinutes": 60, "breakMinutes": 15, "maxStudyMinutesPerDay": 180}


def add_task(base, tok, title, due, minutes):
    status, task, _ = call(base, "POST", "/api/tasks", {"title": title, "dueAt": due, "estimatedMinutes": minutes}, tok)
    assert status == 201, (status, task)
    return task


def save_prefs(base, s, p):
    """Saves preferences and waits for the rebuild that every save starts, even with auto re-plan off.

    The save sends no ticket header, but tickets count up per student and only saves issue
    them here, so the n-th save is ticket n."""
    status, body, _ = call(base, "PUT", "/api/settings/preferences", p, s["token"])
    assert status == 200, (status, body)
    s["saves"] = s.get("saves", 0) + 1
    deadline = time.time() + 90
    while time.time() < deadline:
        state = call(base, "GET", f"/api/agent/replans/{s['saves']}", None, s["token"])[1].get("state")
        if state in ("done", "skipped", "failed"):
            return
        assert state == "pending", state
        time.sleep(0.3)
    raise AssertionError("background rebuild after saving preferences did not finish")


def schedule(base, tok):
    status, body, _ = call(base, "GET", "/api/schedule", None, tok)
    assert status == 200, (status, body)
    return body["blocks"]


def generate(base, tok):
    return call(base, "POST", "/api/agent/generate", {}, tok, timeout=90)


def rows(sid):
    with db() as conn:
        blocks = conn.execute("SELECT assignment_id, title, day, start_minute, end_minute FROM study_blocks "
                              "WHERE student_id = %s ORDER BY day, start_minute", (sid,)).fetchall()
        tasks = conn.execute("SELECT id, title, due_at, estimated_minutes, status FROM tasks WHERE student_id = %s "
                             "ORDER BY title", (sid,)).fetchall()
        saved = conn.execute("SELECT preferences FROM study_preferences WHERE student_id = %s", (sid,)).fetchone()
    return {"blocks": blocks, "tasks": tasks, "prefs": saved[0] if saved else None}


def db_matches_api(db_blocks, api_blocks):
    api = sorted((b["assignmentId"], b["title"], DAY_NAMES.index(b["day"]), hm(b["start"]), hm(b["end"]))
                 for b in api_blocks)
    return sorted(db_blocks) == api


def db_text(db_blocks, api_blocks):
    if db_matches_api(db_blocks, api_blocks):
        return "study_blocks rows match"
    shown = ", ".join(f"{t} {DAY_NAMES[d]} {clock12(f'{a // 60}:{a % 60:02}')}-{clock12(f'{b // 60}:{b % 60:02}')}"
                      for _, t, d, a, b in db_blocks)
    return f"study_blocks rows differ ({shown or 'none'})"


def due_minutes(iso):
    t = dt.datetime.strptime(iso, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)
    return int(t.timestamp() // 60)


def problems(blocks, p, due_by_id):
    """Rules from the expected results: inside study hours, clear of personal time, before the task's deadline, no overlaps."""
    out = []
    for b in blocks:
        s, e = hm(b["start"]), hm(b["end"])
        if not any(r["day"] == b["day"] and hm(r["start"]) <= s and e <= hm(r["end"]) for r in p["studyHours"]):
            out.append(f"{b['title']} {span(b)} is outside the free hours")
        if any(r["day"] == b["day"] and s < hm(r["end"]) and hm(r["start"]) < e for r in p["personalTime"]):
            out.append(f"{b['title']} {span(b)} overlaps the class")
        due = due_by_id.get(b["assignmentId"])
        if due is None or b["endsAt"] > due:
            out.append(f"{b['title']} {span(b)} ends after its deadline")
    ordered = sorted(blocks, key=lambda b: (DAY_NAMES.index(b["day"]), hm(b["start"])))
    for a, b in zip(ordered, ordered[1:]):
        if a["day"] == b["day"] and hm(b["start"]) < hm(a["end"]):
            out.append(f"{a['title']} {span(a)} overlaps {b['title']} {span(b)}")
    return out


def minutes_by_task(blocks):
    out = {}
    for b in blocks:
        out[b["title"]] = out.get(b["title"], 0) + hm(b["end"]) - hm(b["start"])
    return out


def place(base, tok, wanted, spare):
    """Moves blocks with PATCH /api/schedule/blocks (Edit Schedule) until each task sits in its wanted slot."""
    for _ in range(8):
        blocks = schedule(base, tok)
        slot = lambda b: (b["day"], b["start"], b["end"])
        wrong = [b for b in blocks if slot(b) != wanted[b["assignmentId"]]]
        if not wrong:
            return blocks
        taken = {slot(b) for b in blocks}
        b = next((w for w in wrong if wanted[w["assignmentId"]] not in taken), wrong[0])
        to = wanted[b["assignmentId"]] if wanted[b["assignmentId"]] not in taken else spare
        status, body, _ = call(base, "PATCH", "/api/schedule/blocks", {
            "assignmentId": b["assignmentId"],
            "from": {"day": b["day"], "start": b["start"], "end": b["end"]},
            "to": {"day": to[0], "start": to[1], "end": to[2]}}, tok)
        assert status == 200, (status, body)
    raise AssertionError(f"could not place blocks: {schedule(base, tok)}")


def ask(base, tok, message):
    """Sends the message, and a yes when the assistant asks to confirm. Returns (status, [replies])."""
    status, body, _ = call(base, "POST", "/api/agent/chat", {"message": message}, tok, timeout=90)
    replies = [body.get("reply", "") if isinstance(body, dict) else str(body)]
    if status == 200 and CONFIRM.search(replies[0]):
        status, body, _ = call(base, "POST", "/api/agent/chat", {"message": "Yes, please do that."}, tok, timeout=90)
        replies.append(body.get("reply", "") if isinstance(body, dict) else str(body))
    return status, replies


def quote(text, n=220):
    text = re.sub(r"\s+", " ", text).replace("—", ",").replace("–", "-").replace("**", "")
    text = text.replace("’", "'").replace("“", '"').replace("”", '"')
    return text if len(text) <= n else text[:n].rstrip() + "..."


def clock12(clock):
    h, m = divmod(hm(clock), 60)
    return f"{(h - 1) % 12 + 1}{'' if m == 0 else f':{m:02}'} {'AM' if h < 12 else 'PM'}"


def plain_blocks(blocks):
    return ", ".join(f"{b['title']} {b['day']} {clock12(b['start'])}-{clock12(b['end'])}" for b in blocks) or "none"


# ---------------------------------------------------------------- UC-5

def tc_05_01(base, s, d1, d2):
    tok = s["token"]
    p = prefs([(d1, "16:00", "19:00"), (d2, "16:00", "19:00")], [(d1, "17:00", "18:00")])
    save_prefs(base, s, p)
    quiz = add_task(base, tok, "Math Quiz", d2.utc(10), 60)
    proj = add_task(base, tok, "CS 3354 Project", d2.utc(23, 59), 120)
    status, plan, ms = generate(base, tok)
    reloaded = schedule(base, tok)
    r = rows(s["id"])
    bad = problems(reloaded, p, {quiz["id"]: quiz["dueAt"], proj["id"]: proj["dueAt"]})
    engine = plan.get("engine") if isinstance(plan, dict) else None
    returned = plan.get("schedule", []) if isinstance(plan, dict) else []
    return dict(
        inputs=f"Study preferences (PUT /api/settings/preferences, Study Settings page): free {d1.label} and {d2.label} 4-7 PM. "
               f"Class {d1.label} 5-6 PM entered as personal time. "
               f"60 min sessions, 15 min breaks, 180 min a day. Tasks: Math Quiz 1 hr due {d2.label} 10 AM, CS 3354 Project 2 hrs due {d2.label} 11:59 PM. "
               "Generate New Plan is POST /api/agent/generate, then the calendar is reloaded with GET /api/schedule.",
        expected="Study blocks fit available hours and occur before deadlines. None overlap the class. Accepted plan appears on calendar.",
        checks=[
            ("generate answers 200", status == 200),
            ("both saved tasks get study blocks", {b["assignmentId"] for b in reloaded} == {quiz["id"], proj["id"]}),
            ("every block is inside the free hours, before its deadline, off the class hour, with no overlaps", not bad),
            ("calendar after reload shows the plan that was returned", len(returned) == len(reloaded) > 0),
            ("study_blocks rows in Supabase match the calendar", db_matches_api(r["blocks"], reloaded)),
        ],
        actual=f"{status}, engine {engine}, warnings {quote('; '.join(plan.get('warnings', [])) if isinstance(plan, dict) else '') or 'none'}, {ms / 1000:.1f} s. Saved blocks {plain_blocks(reloaded)}; minutes {minutes_by_task(reloaded)}; "
               f"rule problems {'; '.join(bad) or 'none'}; {db_text(r['blocks'], reloaded)}.")


def tc_05_02(base, s, d1, d2):
    tok = s["token"]
    save_prefs(base, s, prefs([(d2, "16:00", "19:00")]))
    add_task(base, tok, "Existing task", d2.utc(23, 59), 60)
    st0, _, _ = generate(base, tok)
    assert st0 == 200, st0
    p = prefs([(d1, "16:00", "17:00"), (d2, "16:00", "19:00")])
    save_prefs(base, s, p)
    add_task(base, tok, "Physics Exam Prep", d2.utc(9), 240)
    accepted = schedule(base, tok)
    before = rows(s["id"])["blocks"]
    status, body, _ = generate(base, tok)
    after = schedule(base, tok)
    after_rows = rows(s["id"])["blocks"]
    tasks = {t["id"]: t["dueAt"] for t in call(base, "GET", "/api/tasks", None, tok)[1]}
    bad = problems(after, p, tasks)
    msgs = []
    if isinstance(body, dict):
        msgs = [body.get("error", "")] + body.get("details", []) + body.get("warnings", [])
    said = " ".join(m for m in msgs if m)
    return dict(
        inputs=f"Accepted plan first: free {d2.label} 4-7 PM, task Existing task 1 hr due {d2.label} 11:59 PM, generated and saved. "
               f"Test availability then saved: free {d1.label} 4-5 PM and {d2.label} 4-7 PM. Task Physics Exam Prep 4 hrs due {d2.label} 9 AM, "
               f"so the only free time before its deadline is {d1.label} 4-5 PM. The saved plan is the calendar right before Generate, after the rebuild that saving preferences starts. Generate is POST /api/agent/generate.",
        expected="System reports that 4 hrs cannot fit in the 1-hr free window and requests changed constraints or workload. "
                 "No overlapping block is accepted; saved plan stays unchanged.",
        checks=[
            ("the answer says Physics Exam Prep does not fit before its deadline",
             "Physics Exam Prep" in said and re.search(r"fit|not scheduled|only \d+ of", said, re.I) is not None),
            ("the answer asks for more study time or less work",
             re.search(r"\b(add|change|increase|extend|reduce|lower|move)\b[^.]*\b(study hours|time|availability|estimate|workload|deadline)",
                       said, re.I) is not None),
            ("no overlapping or out-of-hours block is saved", not bad),
            ("saved plan is unchanged (API)", [span(b) + b["title"] for b in after] == [span(b) + b["title"] for b in accepted]),
            ("saved plan is unchanged (study_blocks rows)", after_rows == before),
        ],
        actual=f"{status}, engine {body.get('engine') if isinstance(body, dict) else None}, messages \"{quote(said) or 'none'}\". "
               f"Plan before {plain_blocks(accepted)}; after {plain_blocks(after)}.")


def tc_05_03(base, s, d1, d2):
    tok = s["token"]
    good = prefs([(d2, "16:00", "17:00")])
    save_prefs(base, s, good)
    bio = add_task(base, tok, "BIO Lab", d2.utc(23, 59), 60)
    st0, _, _ = generate(base, tok)
    assert st0 == 200, st0
    accepted = schedule(base, tok)
    before = rows(s["id"])
    bad_prefs = dict(good, studyHours=good["studyHours"] + [{"day": d1.name, "start": "19:00", "end": "16:00"}])
    put_status, put_body, _ = call(base, "PUT", "/api/settings/preferences", bad_prefs, tok)
    gen_status, _, _ = generate(base, tok)
    after = rows(s["id"])
    _, read_back, _ = call(base, "GET", "/api/settings/preferences", None, tok)
    reloaded = schedule(base, tok)
    err = " ".join([put_body.get("error", "")] + put_body.get("details", [])) if isinstance(put_body, dict) else str(put_body)
    return dict(
        inputs=f"Accepted plan first: free {d2.label} 4-5 PM, task BIO Lab 1 hr due {d2.label} 11:59 PM, generated and saved. "
               f"Then study availability {d1.label} from 7 PM to 4 PM added and saved with PUT /api/settings/preferences "
               "(the Study Settings grid cannot paint a reversed range, so the request is sent as the page would send it), "
               "then POST /api/agent/generate, then preferences and calendar reloaded.",
        expected="Validation explains that end time must be after start. Invalid hours are not saved or used for generation; the accepted plan remains intact.",
        checks=[
            ("save is rejected with 400", put_status == 400),
            ("message says the end time must be after the start", re.search(r"end\w*\s+after\s+(it|they)?\s*start", err, re.I) is not None),
            ("reloaded preferences have no hours on the invalid day",
             not any(r["day"] == d1.name for r in read_back["preferences"]["studyHours"])),
            ("study_preferences row in Supabase is unchanged", after["prefs"] == before["prefs"]),
            ("no block uses the invalid hours", not any(b["day"] == d1.name for b in reloaded)),
            ("accepted plan remains intact after the generate attempt",
             [span(b) for b in reloaded] == [span(b) for b in accepted] and after["blocks"] == before["blocks"]
             and {b["assignmentId"] for b in reloaded} == {bio["id"]}),
        ],
        actual=f"Save {put_status} \"{quote(err)}\"; generate {gen_status}. Saved hours {[span(r) for r in read_back['preferences']['studyHours']]}; "
               f"plan before {plain_blocks(accepted)}, after {plain_blocks(reloaded)}.")


def tc_05_04(base, s, d1, d2):
    tok = s["token"]
    eng = add_task(base, tok, "ENG 1301 Essay", d2.utc(23, 59), 90)
    lab = add_task(base, tok, "CS 3354 Lab", d1.utc(23, 59), 60)
    p = prefs([(d1, "16:00", "19:00"), (d2, "16:00", "19:00")], [(d1, "16:00", "17:00")])
    save_prefs(base, s, p)
    status, plan, _ = generate(base, tok)
    reloaded = schedule(base, tok)
    _, tasks, _ = call(base, "GET", "/api/tasks", None, tok)
    r = rows(s["id"])
    due = {t["id"]: t["dueAt"] for t in tasks}
    bad = problems(reloaded, p, due)
    got = minutes_by_task(reloaded)
    db_due = {t[0]: t[2] for t in r["tasks"]}
    return dict(
        inputs=f"Tasks saved with POST /api/tasks (Tasks page): ENG 1301 Essay 90 min due {d2.label} 11:59 PM, CS 3354 Lab 60 min due {d1.label} 11:59 PM. "
               f"Fixed class {d1.label} 4-5 PM entered as personal time. "
               f"Free {d1.label} and {d2.label} 4-7 PM. Generate is POST /api/agent/generate, then GET /api/tasks and GET /api/schedule as the reload.",
        expected=f"Generated study times account for both tasks and their deadlines, do not overlap the {d1.label} 4-5 PM class, "
                 "and remain visible after reload. Task deadlines are unchanged.",
        checks=[
            ("generate answers 200", status == 200),
            ("ENG 1301 Essay gets at least its 90 min and CS 3354 Lab at least its 60 min",
             got.get("ENG 1301 Essay", 0) >= 90 and got.get("CS 3354 Lab", 0) >= 60),
            ("every block is before its task's deadline, inside free hours, off the class hour, with no overlaps", not bad),
            ("blocks are visible after reload and match study_blocks rows", len(reloaded) > 0 and db_matches_api(r["blocks"], reloaded)),
            ("task deadlines unchanged (API)", due.get(eng["id"]) == d2.utc(23, 59) and due.get(lab["id"]) == d1.utc(23, 59)),
            ("task deadlines unchanged (tasks rows)",
             db_due.get(eng["id"]) == due_minutes(d2.utc(23, 59)) and db_due.get(lab["id"]) == due_minutes(d1.utc(23, 59))),
        ],
        actual=f"{status}, engine {plan.get('engine') if isinstance(plan, dict) else None}. Blocks after reload {plain_blocks(reloaded)}; "
               f"minutes {got}; rule problems {'; '.join(bad) or 'none'}; {db_text(r['blocks'], reloaded)}.")


# ---------------------------------------------------------------- UC-7

def tc_07_01(base, s, d1, d2):
    tok = s["token"]
    save_prefs(base, s, prefs([(d1, "16:00", "19:00")]))
    add_task(base, tok, "CS 3354 Project", d2.utc(23, 59), 60)
    assert generate(base, tok)[0] == 200
    before = rows(s["id"])
    status, replies = ask(base, tok, "When is my CS 3354 Project due?")
    after = rows(s["id"])
    reply = " ".join(replies)
    day_ok = re.search(rf"\b({d2.full}|{d2.name})\b|\b{d2.date.strftime('%B')} {d2.date.day}\b|\b{d2.date.strftime('%b')} {d2.date.day}\b",
                       reply, re.I) is not None
    time_ok = re.search(r"11:59\s*(PM|p\.m\.)|23:59", reply, re.I) is not None
    return dict(
        inputs=f"Saved task CS 3354 Project due {d2.label} 11:59 PM, with a generated plan on the calendar. "
               f"Prompt \"When is my CS 3354 Project due?\" sent with {CHAT_PATH}.",
        expected=f"Reply identifies CS 3354 Project and its {d2.label} 11:59 PM deadline accurately. No tasks or schedule blocks change.",
        checks=[
            ("chat answers 200", status == 200),
            ("reply names CS 3354 Project", "CS 3354 Project" in reply),
            (f"reply gives the due day ({d2.label})", day_ok),
            ("reply gives the due time 11:59 PM", time_ok),
            ("tasks rows unchanged", after["tasks"] == before["tasks"]),
            ("study_blocks rows unchanged", after["blocks"] == before["blocks"]),
        ],
        actual=f"{status}, reply \"{quote(reply)}\". Tasks and blocks unchanged: {after['tasks'] == before['tasks'] and after['blocks'] == before['blocks']}.")


def tc_07_02(base, s, d1, d2):
    tok = s["token"]
    save_prefs(base, s, prefs([(d1, "16:00", "17:00"), (d1, "18:00", "19:00")]))
    review = add_task(base, tok, "CS 3354 Review", d2.utc(23, 59), 60)
    other = add_task(base, tok, "Other task", d2.utc(23, 59), 60)
    assert generate(base, tok)[0] == 200
    save_prefs(base, s, prefs([(d1, "16:00", "17:00"), (d1, "18:00", "19:00"), (d2, "16:00", "17:00")]))
    place(base, tok, {review["id"]: (d1.name, "16:00", "17:00"), other["id"]: (d1.name, "18:00", "19:00")},
          (d2.name, "16:00", "17:00"))
    before = schedule(base, tok)
    prompt = f"Move CS 3354 Review from {d1.full} 4-5 PM to {d2.full} 4-5 PM."
    status, replies = ask(base, tok, prompt)
    reloaded = schedule(base, tok)
    r = rows(s["id"])
    mine = [b for b in reloaded if b["assignmentId"] == review["id"]]
    others_before = [span(b) + b["title"] for b in before if b["assignmentId"] != review["id"]]
    others_after = [span(b) + b["title"] for b in reloaded if b["assignmentId"] != review["id"]]
    return dict(
        inputs=f"CS 3354 Review scheduled {d1.label} 4-5 PM and Other task {d1.label} 6-7 PM (placed with PATCH /api/schedule/blocks, Edit Schedule); "
               f"{d2.label} 4-5 PM is free study time. Prompt \"{prompt}\" sent with {CHAT_PATH}. "
               "A yes is sent if the reply asks to confirm. Then GET /api/schedule as the reload.",
        expected=f"One CS 3354 Review block appears {d2.label} 4-5 PM, not {d1.label} 4-5 PM. Other events remain unchanged; the new time persists after reload.",
        checks=[
            ("chat answers 200", status == 200),
            (f"exactly one CS 3354 Review block, at {d2.name} 16:00-17:00",
             [span(b) for b in mine] == [f"{d2.name} 16:00-17:00"]),
            (f"no CS 3354 Review block at {d1.name} 16:00-17:00", f"{d1.name} 16:00-17:00" not in [span(b) for b in mine]),
            ("other events unchanged", others_after == others_before),
            ("new time persists in study_blocks rows",
             (review["id"], "CS 3354 Review", DAY_NAMES.index(d2.name), 960, 1020) in r["blocks"]),
        ],
        actual=f"{status}, reply \"{quote(' / '.join(replies))}\". CS 3354 Review after reload {[span(b) for b in mine]}.")


def tc_07_03(base, s, d1, d2):
    tok = s["token"]
    save_prefs(base, s, prefs([(d1, "16:00", "19:00"), (d2, "16:00", "19:00")]))
    add_task(base, tok, "CS 3354 Project", d2.utc(23, 59), 60)
    assert generate(base, tok)[0] == 200
    before = rows(s["id"])
    status, replies = ask(base, tok, "Move HIST 3300 Essay to tomorrow at 5 PM.")
    after = rows(s["id"])
    _, tasks, _ = call(base, "GET", "/api/tasks", None, tok)
    reply = " ".join(replies)
    not_found = re.search(
        r"(\bno\b|n't (see|find|locate|have)|not (see|find|locate|have))[^.]{0,40}HIST 3300 Essay|"
        r"HIST 3300 Essay[^.]{0,40}(isn't|is not|not found|not in|not on|doesn't exist|does not exist)|"
        r"which (task|assignment)|valid task", reply, re.I) is not None
    return dict(
        inputs=f"Task list has only CS 3354 Project, with a saved plan. Prompt \"Move HIST 3300 Essay to tomorrow at 5 PM.\" sent with {CHAT_PATH}. "
               "Then GET /api/tasks and GET /api/schedule as the reload.",
        expected="Chatbot reports the task cannot be found or asks for a valid task. No task is fabricated, and the saved schedule is unchanged.",
        checks=[
            ("chat answers 200", status == 200),
            ("reply says the task can't be found or asks for a valid task", not_found),
            ("no HIST 3300 Essay task exists", not any("HIST" in t["title"] for t in tasks)),
            ("tasks rows unchanged", after["tasks"] == before["tasks"]),
            ("study_blocks rows unchanged", after["blocks"] == before["blocks"]),
        ],
        actual=f"{status}, reply \"{quote(reply)}\". Tasks after {[t['title'] for t in tasks]}; blocks unchanged: {after['blocks'] == before['blocks']}.")


def tc_07_04(base, s, d1, d2):
    tok = s["token"]
    save_prefs(base, s, prefs([(d1, "16:00", "17:00"), (d2, "16:00", "19:00")]))
    prompt = f"Add CHEM 1311 Chapter 5 Review due {d2.name} 11:59 PM and schedule 60 minutes {d1.name} 4-5 PM."
    status, replies = ask(base, tok, prompt)
    _, tasks, _ = call(base, "GET", "/api/tasks", None, tok)
    reloaded = schedule(base, tok)
    r = rows(s["id"])
    chem = [t for t in tasks if t["title"] == "CHEM 1311 Chapter 5 Review"]
    chem_ids = {t["id"] for t in chem}
    blocks = [b for b in reloaded if b["assignmentId"] in chem_ids]
    return dict(
        inputs=f"Study preferences free {d1.label} 4-5 PM and {d2.label} 4-7 PM, no tasks, empty calendar. Prompt \"{prompt}\" sent with {CHAT_PATH}. "
               "A yes is sent if the reply asks to confirm. Then GET /api/tasks and GET /api/schedule as the reload.",
        expected=f"Exactly one task has the correct title and due time; one {d1.label} 4-5 PM study block exists without conflict. Both persist after reload.",
        checks=[
            ("chat answers 200", status == 200),
            (f"exactly one CHEM 1311 Chapter 5 Review task, due {d2.label} 11:59 PM",
             len(chem) == 1 and chem[0]["dueAt"] == d2.utc(23, 59)),
            (f"exactly one block for it, at {d1.name} 16:00-17:00", [span(b) for b in blocks] == [f"{d1.name} 16:00-17:00"]),
            ("task and block persist in Supabase rows",
             any(t[1] == "CHEM 1311 Chapter 5 Review" for t in r["tasks"])
             and any(b[1] == "CHEM 1311 Chapter 5 Review" and b[3] == 960 for b in r["blocks"])),
        ],
        actual=f"{status}, reply \"{quote(' / '.join(replies))}\". Tasks after reload {[t['title'] for t in tasks] or 'none'}; "
               f"blocks {plain_blocks(reloaded)}.")


CASES = [
    ("TC-05-01", "Generate a conflict-free weekly plan", UC5, tc_05_01),
    ("TC-05-02", "Insufficient time before deadline", UC5, tc_05_02),
    ("TC-05-03", "Reject reversed availability hours", UC5, tc_05_03),
    ("TC-05-04", "Sync tasks, calendar, and AI schedule", UC5, tc_05_04),
    ("TC-07-01", "Answer a saved task question", UC7_ASK, tc_07_01),
    ("TC-07-02", "Move a study block through chat", UC7_DO, tc_07_02),
    ("TC-07-03", "Reject a nonexistent task update", UC7_DO, tc_07_03),
    ("TC-07-04", "Create and schedule a task via chat", UC7_DO, tc_07_04),
]


def main():
    ahead = days_ahead()
    if len(ahead) < 2:
        sys.exit("Fewer than two days with 4 PM still ahead this week (UTC-6). Run again on Monday to Friday.")
    d1, d2 = ahead[0], ahead[1]
    report = Report("NK", "Nishanth Kandula (NK)", "NK live test results: UC-5 and UC-7")
    base = start_server(PORT, CRAMBERRY_AUTO_REPLAN=0)
    print(f"NK live tests on {base}, test days {d1.label} and {d2.label}")
    for tc, title, req, fn in CASES:
        s = new_student(base, f"{TAG}-{tc[3:].lower()}", utc_offset_minutes=OFFSET)
        try:
            out = fn(base, s, d1, d2)
            report.record(tc, title, req, out["inputs"], out["expected"], out["checks"], out["actual"])
        except Exception as e:  # a crash is a failed case, not a stopped run
            report.record(tc, title, req, "Setup did not finish.", "", [("case ran to the end", False)], f"crashed with {e!r}")
        finally:
            delete_student(base, s)
    with db() as conn:
        left = conn.execute("SELECT count(*) FROM students WHERE email LIKE %s", (f"temp-{TAG}-%",)).fetchone()[0]
    print(f"temporary temp-{TAG}-* students left: {left}")
    sys.exit(report.write(f"one server on port {PORT} with `CRAMBERRY_AUTO_REPLAN=0`, test students on UTC-6"))


if __name__ == "__main__":
    main()
