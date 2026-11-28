"""Shared helpers for the Assignment 5 live tests (<member>_live_tests.py in this folder).

Each script starts its own cramberry_server from backend/out/build, using backend/.env
(Supabase and OpenRouter), on a port of its own. Every test signs up a temporary student
and deletes it again, so the scripts can run at the same time and leave the database with
only the shared test account.

TESTER must name the person running the tests. It is written into the results.
"""

import atexit
import datetime as dt
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[3]
BACKEND = ROOT / "backend"
SERVER = BACKEND / "out" / "build" / "cramberry_server"
PASSWORD = "Cramberry123"
RUN_ID = str(int(time.time()))


def load_env():
    env = {}
    path = BACKEND / ".env"
    if path.exists():
        for line in path.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            env[key.strip()] = value.strip().strip('"').strip("'")
    env.update(os.environ)
    return env


ENV = load_env()


def tester():
    name = os.environ.get("TESTER", "").strip()
    if not name:
        sys.exit('Set TESTER to the name of the person running the tests, e.g. TESTER="Joshua Alston (JA)"')
    return name


def start_server(port, **overrides):
    """Starts cramberry_server on 127.0.0.1:<port> and returns its base URL. Stopped at exit."""
    env = dict(ENV, CRAMBERRY_HOST="127.0.0.1", CRAMBERRY_PORT=str(port))
    env.update({k: str(v) for k, v in overrides.items()})
    log = open(pathlib.Path(tempfile.gettempdir()) / f"cramberry_server_{port}.log", "w")
    proc = subprocess.Popen([str(SERVER)], cwd=BACKEND, env=env, stdout=log, stderr=subprocess.STDOUT)
    atexit.register(proc.terminate)
    base = f"http://127.0.0.1:{port}"
    for _ in range(150):
        if proc.poll() is not None:
            sys.exit(f"cramberry_server on port {port} exited with code {proc.returncode}; see {log.name}")
        try:
            if call(base, "GET", "/api/health")[0] == 200:
                return base
        except OSError:
            pass
        time.sleep(0.2)
    sys.exit(f"cramberry_server on port {port} did not answer /api/health within 30 s; see {log.name}")


def call(base, method, path, body=None, token=None, timeout=60):
    """Returns (status, parsed JSON or text, elapsed ms)."""
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    start = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            status, raw = r.status, r.read()
    except urllib.error.HTTPError as e:
        status, raw = e.code, e.read()
    ms = (time.perf_counter() - start) * 1000
    try:
        parsed = json.loads(raw) if raw else None
    except ValueError:
        parsed = raw.decode(errors="replace")
    return status, parsed, ms


def new_student(base, tag, name=None, utc_offset_minutes=-360):
    email = f"temp-{tag}-{RUN_ID}@cramberry.test".lower()
    status, body, _ = call(base, "POST", "/api/auth/register",
                           {"name": name or f"Temp {tag}", "email": email, "password": PASSWORD,
                            "utcOffsetMinutes": utc_offset_minutes})
    assert status == 201, (status, body)
    return {"email": email, "token": body["token"], "id": body["student"]["id"]}


def delete_student(base, student):
    return call(base, "DELETE", "/api/users/me", {"password": PASSWORD}, student["token"])[0]


def due_in(days, hour=23, minute=59):
    t = dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=days)
    return t.replace(hour=hour, minute=minute, second=0, microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def db():
    import psycopg
    return psycopg.connect(ENV["CRAMBERRY_DATABASE_URL"], prepare_threshold=None)


def database_label():
    url = ENV.get("CRAMBERRY_DATABASE_URL", "")
    if not url:
        return "SQLite (" + ENV.get("CRAMBERRY_SQLITE_PATH", "cramberry.db") + ")"
    host = re.sub(r"^.*@", "", url).split("/")[0]
    host = re.sub(r"^db\.[a-z0-9]+\.", "db.<project>.", host)
    host = re.sub(r"postgres\.[a-z0-9]+", "postgres.<project>", host)
    return f"Supabase hosted PostgreSQL ({host})"


def _cell(text):
    return str(text).replace("|", "\\|").replace("\n", "<br>")


class Report:
    def __init__(self, member, author, title):
        self.member, self.author, self.title = member, author, title
        self.tester = tester()
        self.started = dt.datetime.now().astimezone()
        self.cases = []

    def record(self, tc, title, requirement, inputs, expected, checks, actual):
        ok = all(passed for _, passed in checks)
        self.cases.append({"id": tc, "title": title, "requirement": requirement, "inputs": inputs,
                           "expected": expected, "actual": actual, "status": "Pass" if ok else "Fail",
                           "checks": [{"check": c, "pass": p} for c, p in checks]})
        print(f"[{'PASS' if ok else 'FAIL'}] {tc} {title}")
        for c, p in checks:
            print(f"    {'ok ' if p else 'BAD'} {c}")
        print(f"    actual: {actual}")

    def environment(self, servers):
        return [
            ("Date", self.started.strftime("%a %d %b %Y, %H:%M %Z")),
            ("Tester", self.tester),
            ("Test author", self.author),
            ("Machine", f"{platform.system()} {platform.release()} ({platform.machine()})"),
            ("Server", f"`cramberry_server` from `backend/out/build`, {servers}"),
            ("Database", database_label()),
            ("AI", f"`{ENV.get('OPENROUTER_MODEL', 'anthropic/claude-haiku-5.5')}` through OpenRouter"),
            ("Client", "Python 3 `urllib`, one request at a time"),
        ]

    def write(self, servers, extra_environment=()):
        out_dir = ROOT / "docs" / "testing" / self.member
        out_dir.mkdir(parents=True, exist_ok=True)
        env_rows = self.environment(servers) + list(extra_environment)
        passed = sum(c["status"] == "Pass" for c in self.cases)

        lines = [f"# {self.title}", "", "| Item | Value |", "| :--- | :--- |"]
        lines += [f"| {k} | {_cell(v)} |" for k, v in env_rows]
        lines += ["", f"**{len(self.cases)} run, {passed} Pass, {len(self.cases) - passed} Fail.**", "",
                  "| Test case ID | Requirement / use case | Test author | Tester | Inputs | Expected result | Actual result | Pass/Fail |",
                  "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |"]
        for c in self.cases:
            lines.append(f"| {c['id']} {_cell(c['title'])} | {_cell(c['requirement'])} | {_cell(self.author)} | {_cell(self.tester)} | "
                         f"{_cell(c['inputs'])} | {_cell(c['expected'])} | {_cell(c['actual'])} | **{c['status']}** |")
        lines += ["", "## Checks", ""]
        for c in self.cases:
            lines.append(f"**{c['id']}** ({c['status']})")
            lines += [f"- {'ok' if k['pass'] else 'FAILED'}: {k['check']}" for k in c["checks"]]
            lines.append("")
        (out_dir / "results.md").write_text("\n".join(lines))
        print(f"\n{len(self.cases)} run, {passed} Pass, {len(self.cases) - passed} Fail")
        print(f"Results written to {out_dir / 'results.md'}")
        return 0 if passed == len(self.cases) else 1
