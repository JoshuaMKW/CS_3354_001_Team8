#!/usr/bin/env python3
"""AA (Ahnaf Adib) live tests for Assignment 5: UC-10, UC-11, UC-12, NFR-18, NFR-19.

Runs against a running cramberry_server connected to Supabase, then checks the rows in
the database directly. Every test uses the one test account (student@cramberry.test).
Its data is cleared before each test, and the account itself is kept. TC-NFR19-02 needs a
second student, so it creates one for the test and deletes it again at the end.

Usage (from backend/, with the server running):
    set -a; source .env; set +a
    python3 tests/live/aa_live_tests.py            # needs `pip install "psycopg[binary]"`

Environment:
    BASE          main server, default http://127.0.0.1:8080
    BASE_BAD_AI   optional second server started with OPENROUTER_MODEL=anthropic/no-such-model
                  (used by TC-12-03; skipped when unset)
    CRAMBERRY_DATABASE_URL   the same Supabase connection string the server uses
"""

import datetime as dt
import json
import os
import statistics
import sys
import time
import urllib.error
import urllib.request

import psycopg

BASE = os.environ.get("BASE", "http://127.0.0.1:8080")
BASE_BAD_AI = os.environ.get("BASE_BAD_AI", "")
DB_URL = os.environ["CRAMBERRY_DATABASE_URL"]
TEST_USER = {"name": "Test Student", "email": "student@cramberry.test", "password": "Cramberry123",
             "utcOffsetMinutes": -360}
PASSWORD = TEST_USER["password"]
RUN_ID = str(int(time.time()))

PREFS = {
    "studyHours": [{"day": d, "start": "16:00", "end": "21:00"} for d in ("Mon", "Tue", "Wed", "Thu", "Fri")]
    + [{"day": d, "start": "10:00", "end": "14:00"} for d in ("Sat", "Sun")],
    "sessionMinutes": 60,
    "breakMinutes": 15,
    "maxStudyMinutesPerDay": 180,
    "personalTime": [{"day": "Sat", "start": "12:00", "end": "13:00"}],
}

results = []


def call(method, path, body=None, token=None, base=BASE):
    """Returns (status, parsed body or text, elapsed ms)."""
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    start = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            status, raw = r.status, r.read()
    except urllib.error.HTTPError as e:
        status, raw = e.code, e.read()
    ms = (time.perf_counter() - start) * 1000
    try:
        parsed = json.loads(raw) if raw else None
    except ValueError:
        parsed = raw.decode(errors="replace")
    return status, parsed, ms


def register(tag, base=BASE):
    """Signs in as the test account with all its data cleared, as if it were new. `tag` only labels the test."""
    login = {"email": TEST_USER["email"], "password": PASSWORD}
    status, body, _ = call("POST", "/api/auth/login", login, base=base)
    if status == 401:
        status, body, _ = call("POST", "/api/auth/register", TEST_USER, base=base)
    assert status in (200, 201), (status, body)
    sid = body["student"]["id"]
    with db() as conn:
        for table in ("study_blocks", "tasks", "courses", "study_preferences", "generation_runs",
                      "notifications", "notification_settings"):
            conn.execute(f"DELETE FROM {table} WHERE student_id = %s", (sid,))
        conn.execute("UPDATE students SET name = %s, utc_offset_minutes = %s WHERE id = %s",
                     (TEST_USER["name"], TEST_USER["utcOffsetMinutes"], sid))
    return {"email": TEST_USER["email"], "token": body["token"], "id": sid}


def register_temporary(tag):
    """A second student that exists only for one test; delete it with delete_student()."""
    email = f"temp-{tag}-{RUN_ID}@cramberry.test"
    status, body, _ = call("POST", "/api/auth/register",
                           {"name": f"Temp {tag}", "email": email, "password": PASSWORD, "utcOffsetMinutes": -360})
    assert status == 201, (status, body)
    return {"email": email, "token": body["token"], "id": body["student"]["id"]}


def delete_student(s):
    return call("DELETE", "/api/users/me", {"password": PASSWORD}, s["token"])[0]


def due_in(days, hour=23, minute=59):
    t = dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=days)
    return t.replace(hour=hour, minute=minute, second=0, microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def db():
    return psycopg.connect(DB_URL, prepare_threshold=None)


def record(tc, title, checks, actual, extra=None):
    ok = all(passed for _, passed in checks)
    results.append({"id": tc, "title": title, "status": "Pass" if ok else "Fail",
                    "checks": [{"check": c, "pass": p} for c, p in checks], "actual": actual, **(extra or {})})
    print(f"[{'PASS' if ok else 'FAIL'}] {tc} {title}")
    for c, p in checks:
        print(f"    {'ok ' if p else 'BAD'} {c}")
    print(f"    actual: {actual}")


def mins(clock):
    h, m = clock.split(":")
    return int(h) * 60 + int(m)


def rule_violations(blocks, prefs, tasks, now=None):
    """The same rules the server's validator applies, checked independently."""
    now = now or dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    problems = [f"{b['title']} {b['day']} {b['start']}-{b['end']} is in the past" for b in blocks if b["startsAt"] < now]
    by_day = {}
    for b in blocks:
        s, e = mins(b["start"]), mins(b["end"])
        if e <= s:
            problems.append(f"{b['title']} {b['day']} ends before it starts")
        if e - s > prefs["sessionMinutes"]:
            problems.append(f"{b['title']} {b['day']} {b['start']}-{b['end']} is longer than a session")
        if not any(r["day"] == b["day"] and mins(r["start"]) <= s and e <= mins(r["end"]) for r in prefs["studyHours"]):
            problems.append(f"{b['title']} {b['day']} {b['start']}-{b['end']} is outside study hours")
        if any(r["day"] == b["day"] and s < mins(r["end"]) and mins(r["start"]) < e for r in prefs["personalTime"]):
            problems.append(f"{b['title']} {b['day']} overlaps personal time")
        due = tasks.get(b["assignmentId"])
        if due is None:
            problems.append(f"{b['title']} refers to an unknown task")
        elif b["endsAt"] > due:
            problems.append(f"{b['title']} ends after its deadline")
        by_day.setdefault(b["day"], []).append((s, e))
    for day, spans in by_day.items():
        spans.sort()
        if sum(e - s for s, e in spans) > prefs["maxStudyMinutesPerDay"]:
            problems.append(f"{day} is over the daily maximum")
        for (s1, e1), (s2, _) in zip(spans, spans[1:]):
            if s2 - e1 < prefs["breakMinutes"]:
                problems.append(f"{day}: blocks at {s1 // 60:02}:{s1 % 60:02} and {s2 // 60:02}:{s2 % 60:02} have less than the break between them")
    for task_id in tasks:
        if not any(b["assignmentId"] == task_id for b in blocks):
            problems.append(f"task {task_id} has no block")
    return problems


# ---------------------------------------------------------------- UC-10 / UC-12 (happy path)

def tc_10_02_and_12_02():
    s = register("uc10")
    tok = s["token"]
    call("PUT", "/api/settings/preferences", PREFS, tok)
    t1 = call("POST", "/api/tasks", {"title": "CS 3354 Assignment 5 report", "type": "assignment",
                                     "dueAt": due_in(2), "estimatedMinutes": 180, "priority": 1}, tok)[1]
    t2 = call("POST", "/api/tasks", {"title": "MATH 2418 Exam 3", "type": "exam",
                                     "dueAt": due_in(4), "estimatedMinutes": 120, "priority": 2}, tok)[1]
    status, plan, ms = call("POST", "/api/agent/generate", {}, tok)
    blocks = plan.get("schedule", []) if isinstance(plan, dict) else []
    engine = plan.get("engine") if isinstance(plan, dict) else None
    record("TC-10-02", "Valid scheduling request is processed into a plan (live)", [
        ("HTTP 200", status == 200),
        ("plan made by the AI model (engine claude-haiku-5.5), not the fallback", engine == "claude-haiku-5.5"),
        ("no warnings", isinstance(plan, dict) and plan.get("warnings") == []),
        ("both tasks have at least one block", {b["assignmentId"] for b in blocks} == {t1["id"], t2["id"]}),
        ("AI generation finished within 10 s", ms < 10_000),
    ], f"{status}, engine={engine}, {len(blocks)} blocks, warnings={plan.get('warnings') if isinstance(plan, dict) else plan}, {ms:.0f} ms",
        {"student": s["email"], "elapsed_ms": round(ms)})

    # UC-12: is what was saved valid, and is it what's in Supabase?
    status_s, sched, _ = call("GET", "/api/schedule", None, tok)
    saved = sched["blocks"] if isinstance(sched, dict) else []
    problems = rule_violations(saved, PREFS, {t1["id"]: t1["dueAt"], t2["id"]: t2["dueAt"]})
    with db() as conn:
        rows = conn.execute("SELECT count(*) FROM study_blocks WHERE student_id = %s", (s["id"],)).fetchone()[0]
        run = conn.execute("SELECT engine, ok, block_count FROM generation_runs WHERE student_id = %s "
                           "ORDER BY created_at DESC LIMIT 1", (s["id"],)).fetchone()
    record("TC-12-02", "AI-generated schedule is validated and saved to Supabase (live)", [
        ("GET /api/schedule returns 200", status_s == 200),
        ("saved schedule equals the plan that was returned", len(saved) == len(blocks)),
        ("every block obeys the preferences and none is in the past (independent re-check)", not problems),
        ("study_blocks rows in Supabase = blocks returned", rows == len(saved)),
        ("generation_runs records a successful claude-haiku-5.5 run", run is not None and run[0] == "claude-haiku-5.5" and run[1]),
    ], f"{len(saved)} saved blocks, {rows} rows in study_blocks, latest run={run}, rule problems={problems or 'none'}",
        {"student": s["email"], "blocks": saved})


# ---------------------------------------------------------------- UC-10 (invalid input)

def tc_10_03():
    s = register("uc10-invalid")
    tok = s["token"]
    bad = [
        ("missing title", {"dueAt": due_in(3)}),
        ("due date in words", {"title": "Essay", "dueAt": "next friday"}),
        ("estimate of 0 minutes", {"title": "Essay", "dueAt": due_in(3), "estimatedMinutes": 0}),
        ("priority 5", {"title": "Essay", "dueAt": due_in(3), "priority": 5}),
        ("unknown type", {"title": "Essay", "dueAt": due_in(3), "type": "party"}),
    ]
    answers = [(label, *call("POST", "/api/tasks", body, tok)[:2]) for label, body in bad]
    gen_status, gen, _ = call("POST", "/api/agent/generate", {"day": "Funday"}, tok)
    count = len(call("GET", "/api/tasks", None, tok)[1])
    with db() as conn:
        rows = conn.execute("SELECT count(*) FROM tasks WHERE student_id = %s", (s["id"],)).fetchone()[0]
    checks = [(f"{label} -> 400 with a message", st == 400 and isinstance(b, dict) and b.get("error"))
              for label, st, b in answers]
    checks += [("generate with day \"Funday\" -> 400", gen_status == 400),
               ("no task was saved (API and Supabase both 0)", count == 0 and rows == 0)]
    record("TC-10-03", "Invalid scheduling input is rejected with field errors (live)", checks,
           "; ".join(f"{label}: {st} \"{b.get('error') if isinstance(b, dict) else b}\"" for label, st, b in answers)
           + f"; Funday: {gen_status} \"{gen.get('error') if isinstance(gen, dict) else gen}\"; tasks saved: {rows}",
           {"student": s["email"]})


def tc_10_04():
    s = register("uc10-noprefs")
    tok = s["token"]
    call("POST", "/api/tasks", {"title": "HIST 1301 Essay", "dueAt": due_in(3)}, tok)
    status, body, _ = call("POST", "/api/agent/generate", {}, tok)
    with db() as conn:
        rows = conn.execute("SELECT count(*) FROM study_blocks WHERE student_id = %s", (s["id"],)).fetchone()[0]
    record("TC-10-04", "Request without study preferences is refused (live)", [
        ("HTTP 422", status == 422),
        ("tells the student to set preferences first", isinstance(body, dict) and "preferences" in body.get("error", "")),
        ("no schedule saved in Supabase", rows == 0),
    ], f"{status} \"{body.get('error') if isinstance(body, dict) else body}\", study_blocks rows: {rows}",
        {"student": s["email"]})


# ---------------------------------------------------------------- UC-11

def tc_11_02():
    s = register("uc11")
    tok = s["token"]
    put_status, _, _ = call("PUT", "/api/settings/preferences", PREFS, tok)
    c_status, task, _ = call("POST", "/api/tasks", {"title": "CS 3354 Project demo", "dueAt": due_in(5),
                                                    "estimatedMinutes": 90, "priority": 2}, tok)
    p_status, patched, _ = call("PATCH", f"/api/tasks/{task['id']}",
                                {"estimatedMinutes": 150, "priority": 1, "notes": "practice the demo twice"}, tok)
    st_status, _, _ = call("PUT", f"/api/tasks/{task['id']}/status", {"status": "IN_PROGRESS"}, tok)
    g_status, fetched, _ = call("GET", f"/api/tasks/{task['id']}", None, tok)
    _, prefs, _ = call("GET", "/api/settings/preferences", None, tok)
    with db() as conn:
        row = conn.execute("SELECT title, estimated_minutes, priority, status, notes FROM tasks WHERE id = %s",
                           (task["id"],)).fetchone()
    record("TC-11-02", "Student data is saved, updated and read back from Supabase (live)", [
        ("PUT preferences 200, POST task 201, PATCH 200, status 200, GET 200",
         (put_status, c_status, p_status, st_status, g_status) == (200, 201, 200, 200, 200)),
        ("GET returns the updated values, title unchanged",
         fetched.get("estimatedMinutes") == 150 and fetched.get("priority") == 1 and fetched.get("status") == "IN_PROGRESS"
         and fetched.get("title") == "CS 3354 Project demo"),
        ("row in Supabase has the same values", row == ("CS 3354 Project demo", 150, 1, "IN_PROGRESS", "practice the demo twice")),
        ("preferences read back as saved", prefs.get("saved") is True and prefs["preferences"]["sessionMinutes"] == 60
         and prefs["preferences"]["maxStudyMinutesPerDay"] == 180),
    ], f"GET task: estimate={fetched.get('estimatedMinutes')}, priority={fetched.get('priority')}, status={fetched.get('status')}; "
       f"Supabase row={row}; preferences saved={prefs.get('saved')}", {"student": s["email"]})


# ---------------------------------------------------------------- UC-12 (AI failure)

def tc_12_03():
    if not BASE_BAD_AI:
        results.append({"id": "TC-12-03", "title": "AI failure falls back to the local planner (live)",
                        "status": "Not Run", "actual": "BASE_BAD_AI not set"})
        print("[SKIP] TC-12-03")
        return
    s = register("uc12-badai", base=BASE_BAD_AI)
    tok = s["token"]
    call("PUT", "/api/settings/preferences", PREFS, tok, base=BASE_BAD_AI)
    t = call("POST", "/api/tasks", {"title": "PHYS 2325 Lab report", "dueAt": due_in(3), "estimatedMinutes": 120},
             tok, base=BASE_BAD_AI)[1]
    status, plan, ms = call("POST", "/api/agent/generate", {}, tok, base=BASE_BAD_AI)
    _, runs, _ = call("GET", "/api/agent/runs", None, tok, base=BASE_BAD_AI)
    _, sched, _ = call("GET", "/api/schedule", None, tok, base=BASE_BAD_AI)
    problems = rule_violations(sched["blocks"], PREFS, {t["id"]: t["dueAt"]})
    engines = [(r["engine"], r["ok"]) for r in runs]
    record("TC-12-03", "AI failure is caught and the local planner's valid plan is saved instead (live)", [
        ("HTTP 200 (the student still gets a plan)", status == 200),
        ("engine is \"local\"", plan.get("engine") == "local"),
        ("warning names the AI error", any("no-such-model" in w or "OpenRouter" in w for w in plan.get("warnings", []))),
        # Saving preferences also rebuilds in the background, so there can be more than one
        # generation; each must be a failed AI attempt plus a successful local run. Run times
        # are stored to the minute, so runs in the same minute come back in any order.
        ("runs log: every AI attempt failed, each matched by a successful local run",
         engines.count(("no-such-model", False)) >= 1
         and engines.count(("no-such-model", False)) == engines.count(("local", True)) == len(engines) / 2),
        ("saved fallback schedule obeys the preferences and none is in the past", not problems and len(sched["blocks"]) > 0),
    ], f"{status}, engine={plan.get('engine')}, warnings={plan.get('warnings')}, runs={engines}, "
       f"{len(sched['blocks'])} blocks saved, rule problems={problems or 'none'}, {ms:.0f} ms", {"student": s["email"]})


# ---------------------------------------------------------------- NFR-18

def tc_nfr18_02(n=500):
    s = register("nfr18")
    tok = s["token"]
    call("PUT", "/api/settings/preferences", PREFS, tok)
    ids = [call("POST", "/api/tasks", {"title": f"Load task {i}", "dueAt": due_in(1 + i % 6),
                                       "estimatedMinutes": 30 + i}, tok)[1]["id"] for i in range(20)]
    ops = [
        ("GET /api/tasks", lambda i: call("GET", "/api/tasks", None, tok)),
        ("GET /api/tasks/{id}", lambda i: call("GET", f"/api/tasks/{ids[i % 20]}", None, tok)),
        ("PATCH /api/tasks/{id}", lambda i: call("PATCH", f"/api/tasks/{ids[i % 20]}", {"estimatedMinutes": 30 + i % 90}, tok)),
        ("GET /api/settings/preferences", lambda i: call("GET", "/api/settings/preferences", None, tok)),
        ("GET /api/schedule", lambda i: call("GET", "/api/schedule", None, tok)),
        ("GET /api/dashboard", lambda i: call("GET", "/api/dashboard", None, tok)),
    ]
    times, per_op, failures = [], {}, 0
    for i in range(n):
        name, fn = ops[i % len(ops)]
        status, _, ms = fn(i)
        failures += status >= 400
        times.append(ms)
        per_op.setdefault(name, []).append(ms)
    times.sort()
    p95 = times[int(0.95 * len(times)) - 1]
    within = sum(t < 2000 for t in times) / len(times) * 100
    summary = {k: {"p50": round(statistics.median(v), 1), "p95": round(sorted(v)[int(0.95 * len(v)) - 1], 1),
                   "max": round(max(v), 1)} for k, v in per_op.items()}
    record("TC-NFR18-02", "Standard API requests finish within 2 s against Supabase (live)", [
        (f"p95 of {n} requests < 2000 ms", p95 < 2000),
        ("at least 95% of requests < 2000 ms", within >= 95),
        ("every request succeeded", failures == 0),
    ], f"{n} requests: p50={statistics.median(times):.1f} ms, p95={p95:.1f} ms, max={times[-1]:.1f} ms, "
       f"{within:.1f}% under 2 s, {failures} failures", {"per_endpoint_ms": summary, "student": s["email"]})


# ---------------------------------------------------------------- NFR-19

def tc_nfr19_02():
    a, b = register("nfr19-a"), register_temporary("nfr19-b")
    try:
        _nfr19_checks(a, b)
    finally:
        print(f"  temporary student {b['email']} deleted -> {delete_student(b)}")


def _nfr19_checks(a, b):
    course = call("POST", "/api/courses", {"name": "Software Engineering", "code": "CS 3354"}, a["token"])[1]
    task = call("POST", "/api/tasks", {"title": "A's private essay", "dueAt": due_in(3), "courseId": course["id"]},
                a["token"])[1]
    with db() as conn:
        before = conn.execute("SELECT title, estimated_minutes, status, course_id FROM tasks WHERE id = %s",
                              (task["id"],)).fetchone()
    tb = b["token"]
    attempts = [
        ("B reads A's task", call("GET", f"/api/tasks/{task['id']}", None, tb)[0], {404}),
        ("B edits A's task", call("PATCH", f"/api/tasks/{task['id']}", {"title": "hacked"}, tb)[0], {404}),
        ("B changes A's task status", call("PUT", f"/api/tasks/{task['id']}/status", {"status": "COMPLETED"}, tb)[0], {404}),
        ("B deletes A's task", call("DELETE", f"/api/tasks/{task['id']}", None, tb)[0], {404}),
        ("B edits A's course", call("PATCH", f"/api/courses/{course['id']}", {"name": "hacked"}, tb)[0], {404}),
        ("B files a task under A's course", call("POST", "/api/tasks", {"title": "x", "dueAt": due_in(3),
                                                                       "courseId": course["id"]}, tb)[0], {400}),
        ("no token", call("GET", "/api/tasks")[0], {401}),
        ("made-up token", call("GET", "/api/tasks", None, "not-a-real-token")[0], {401}),
        ("A's token after logout", None, {401}),
    ]
    # Log A out on a second session so the first stays usable for the final check.
    _, login, _ = call("POST", "/api/auth/login", {"email": a["email"], "password": PASSWORD})
    call("POST", "/api/auth/logout", None, login["token"])
    attempts[-1] = ("A's token after logout", call("GET", "/api/tasks", None, login["token"])[0], {401})
    b_list = call("GET", "/api/tasks", None, tb)[1]
    with db() as conn:
        after = conn.execute("SELECT title, estimated_minutes, status, course_id FROM tasks WHERE id = %s",
                             (task["id"],)).fetchone()
        b_rows = conn.execute("SELECT count(*) FROM tasks WHERE student_id = %s", (b["id"],)).fetchone()[0]
    checks = [(f"{label} -> {'/'.join(map(str, want))}", got in want) for label, got, want in attempts]
    checks += [("B's task list is empty (A's task not listed)", b_list == []),
               ("A's row in Supabase is unchanged", before == after),
               ("nothing was created for B", b_rows == 0)]
    record("TC-NFR19-02", "Students can't read or change each other's data (live)", checks,
           "; ".join(f"{label}: {got}" for label, got, _ in attempts) + f"; A's row unchanged: {before == after}",
           {"students": [a["email"], b["email"]]})


if __name__ == "__main__":
    started = dt.datetime.now(dt.timezone.utc)
    _, health, _ = call("GET", "/api/health")
    print(f"AA live tests against {BASE} at {started:%Y-%m-%d %H:%M} UTC, health={health}")
    for test in (tc_10_02_and_12_02, tc_10_03, tc_10_04, tc_11_02, tc_12_03, tc_nfr18_02, tc_nfr19_02):
        try:
            test()
        except Exception as e:  # a crash is a failed test, not a stopped run
            results.append({"id": test.__name__, "status": "Fail", "actual": f"crashed: {e!r}"})
            print(f"[FAIL] {test.__name__} crashed: {e!r}")
    out = os.environ.get("RESULTS", "aa_live_results.json")
    with open(out, "w") as f:
        json.dump({"started": started.isoformat(), "base": BASE, "health": health, "results": results}, f, indent=2)
    passed = sum(r["status"] == "Pass" for r in results)
    print(f"\n{passed}/{len(results)} passed; details in {out}")
    sys.exit(0 if passed == len(results) else 1)
