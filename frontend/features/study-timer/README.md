# Study timer (FR-25) — RM

A standalone, TypeScript-compatible frontend module for Cramberry. It supports custom focus sessions (1–180 minutes), a five-minute break preset, start, pause/resume, reset and a completion message. It does not require AI, a database or a backend connection. Timer state is local to the open page and is not persisted on reload.

## Run the standalone demo

From the repository root:

```bash
python3 -m http.server 3001 --bind 127.0.0.1 --directory frontend/features/study-timer
```

Open http://localhost:3001/demo.html. Set a one-minute duration for a quick completion demonstration, or use the focus and break presets.

## Run automated tests

```bash
node --test frontend/features/study-timer/timer.test.mjs
```

Requires Node.js 18+; no npm dependencies. The model uses elapsed wall-clock time rather than subtracting one second per tick to avoid drift when browser tabs are inactive.

Development verification: all six Node.js model tests passed on Node.js 24.22.3. The browser smoke test still needs to be executed on the team's development machine; the available test environment could not launch Chromium.

## Integrate into the shared frontend

Import `mountStudyTimer` from `study-timer.mjs`, import `timer.css` using your framework's stylesheet convention, and call `mountStudyTimer(containerElement)` after the target element mounts. Invoke the returned cleanup function on unmount. In React, do this inside a client-side effect using a container ref. The included `.d.mts` declaration exposes a typed integration interface without requiring a particular UI framework.

The standalone demo is not yet integrated into the team's unpublished frontend. Browser compatibility, 200% zoom, and accessibility verification should be repeated in the final application.
