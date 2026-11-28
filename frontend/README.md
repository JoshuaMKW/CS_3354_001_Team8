# Cramberry Frontend

The web app for Cramberry, a study planner: students enter their courses, tasks and deadlines, and Cramberry builds a weekly study schedule with AI. It is built with React 18, TypeScript and Vite. The pages follow the site map in the team's SE Document, and the look comes from our Study Planner Settings and Edit Schedule wireframes (copies are in `design/`).

The stylesheet, `src/styles/base.css`, uses the wireframes' colors, font and class names, and it resizes with the window (requirement FR 11). At 1280 px wide the pages look like the wireframes, which `npm run visual` checks. On narrower screens, two-column pages become one column below 1100 px, and phones get 16 px side margins and a menu that wraps onto more lines. The week calendar and the availability grid scroll sideways inside their own border. On screens wider than 1280 px, the page is centered and the top bar spans the whole window.

## Run

You need Node 20+ and the backend running on `127.0.0.1:8080` (see [../backend/README.md](../backend/README.md)).

```bash
cd frontend
npm install
npm run dev        # http://localhost:3000; /api is proxied to the backend, so no CORS setup is needed
```

Open http://localhost:3000, sign up, then:
1. Study Planner: paint your study hours and save.
2. Schedule > Create Schedule: add tasks, then press Generate my schedule.
3. Schedule > Edit Schedule: drag blocks, change status and notes.

To make a production build, run `npm run build`. It type-checks the code and writes static files to `dist/`.

## Pages

| Route | Page | Site map |
| :--- | :--- | :--- |
| `/login`, `/signup` | Sign in, create account | Login, Signup |
| `/` | Today's blocks, upcoming and overdue tasks, weekly report, study timer (by Risha Mothukuru, FR-25) | Home |
| `/schedule` | Week view, .ics export | View Schedule |
| `/schedule/edit` | Drag, resize, edit, remove blocks; Ask AI to adjust (our wireframe) | Edit Schedule |
| `/schedule/create` | Courses and tasks, generate the week's plan | Create Schedule: Manual Creation and Auto Create |
| `/schedule/create/upload` | Find deadlines in syllabus text | Upload Syllabus |
| `/planner/settings` | Study hours, personal time, sessions and breaks (our wireframe) | Set Study Settings |
| `/assistant` | Chat about your tasks and schedule | AI Assistant |
| `/account`, `/account/edit`, `/account/notifications` | Profile, name and time zone, delete account; notification settings and inbox | Profile, Edit Account, Notifications |

## Checks

| Command | What it checks |
| :--- | :--- |
| `npm run build` | TypeScript, then a production build |
| `npm run visual -- settings` / `-- edit` | Takes a screenshot of the page filled with fake data copied from the wireframe, and compares it with a screenshot of the wireframe HTML. Prints how many of the wireframe's drawn (non-white) pixels differ. The current scores are 0.2% for Settings and 1.0% for Edit Schedule. Edit Schedule is compared with `design/edit-schedule.nolectures.html`, a copy of the wireframe without class blocks, because the backend has no class meeting times. |
| `npm run responsive` | Needs `npm run dev` and the backend running. Opens every page at 375, 768, 1024, 1440 and 1920 px against the real backend, signed in as the test account (its data is cleared and filled again through the API). Fails if a page scrolls sideways, if anything sticks out past the window other than inside the calendar or the availability grid, if the top bar doesn't span the window, or if the page isn't centered. It also uses touch to drag a block on the calendar and to paint the availability grid, with each one scrolled sideways. Screenshots go to `visual/out/responsive/`. Set `RESPONSIVE_BASE_URL` to test another address (default http://localhost:3000). 64 checks. |
| `npm run smoke` | Signs in as the test account (its data is cleared first) and uses every page against the real backend. Needs `npm run dev` and the backend running. 10 checks. |
| `npm test` | Study timer tests and the syllabus date-finding test cases |

## Test account

There is one account for testing and for trying the app:

| Email | Password |
| :--- | :--- |
| `student@cramberry.test` | `Cramberry123` |

`npm run smoke`, `npm run responsive` and the backend's live tests (`backend/tests/live/aa_live_tests.py`) all use this account. Before they run, they clear its tasks, courses, schedule and settings. They keep the account itself, and if it doesn't exist yet, the first check to run creates it. The backend's live tests also create a second, temporary student for one privacy test and delete it when that test ends, so afterwards the test account is the only user in the database. The email and password are set in `frontend/e2e/testUser.mjs`.

## Saved on this device only

The backend has no fields for these, so they are kept in the browser's `localStorage`:
- personal-time labels such as "Gym" (`cramberry.personalLabels.<studentId>`)
- the Google Calendar checkbox and the Deadlines panel on the Study Planner page (`cramberry.plannerExtras.<studentId>`); neither one changes how the schedule is planned
- AI Assistant chat history (`cramberry.chats.<studentId>`)

There are no class (lecture or lab) blocks on the calendar, because courses have no meeting times in the backend. Only the current week is shown, because the backend stores one week's schedule.
