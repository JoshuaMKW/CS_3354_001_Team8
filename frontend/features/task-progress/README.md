# Task progress — RM

UC-13 (status), UC-14 (weekly report), FR-24 (course/date filters), and FR-26 (overdue tasks). A reusable frontend panel backed by the existing C++ REST API. It does not replace login, routing, or the team's task creation features.

## Integration

Import `createTaskApi` from `api.mjs`, `mountTaskProgress` from `task-progress.mjs` and the stylesheet. Create an API instance using the existing authenticated session and configured backend URL, then mount the panel in a dashboard container. Call the returned cleanup function on unmount.

```js
const api = createTaskApi({ baseUrl: backendUrl, getToken: () => session.token });
const cleanup = mountTaskProgress(containerElement, api);
```

The panel renders server-returned tasks and reports, changes status using PUT, and refreshes both views after a successful update. Dates used for filtering/report boundaries are UTC. The existing application must provide the account session; no credentials are embedded.

## Tests

`node --test frontend/features/task-progress/api.test.mjs`

These tests exercise the frontend API adapter with a mock transport. They do not constitute a live backend or browser integration test; perform those after mounting in the shared application.

## Local demo

Start the compiled backend with an isolated local SQLite file, no external AI configuration, and CORS set to `http://localhost:3002`. In a second terminal run `python3 -m http.server 3002 --bind 127.0.0.1 --directory frontend/features/task-progress` and open `http://localhost:3002/demo.html`. Select Load sample tasks to create disposable sample data. This harness is only for local development and is not the production login interface.
