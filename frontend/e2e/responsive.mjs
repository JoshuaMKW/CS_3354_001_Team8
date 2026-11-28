// Responsive check (FR 11): every route at 375, 768, 1024, 1440 and 1920 px wide, against the real backend.
// Usage: start the backend (127.0.0.1:8080) and `npm run dev`, then `npm run responsive`.
// Env: RESPONSIVE_BASE_URL (default http://localhost:3000).
//
// For each route and width it asserts that the page never scrolls sideways
// (document.documentElement.scrollWidth <= innerWidth) and that no element sticks out past the
// viewport, except content inside the .cal / .avail scroll containers (which must themselves fit).
// It also drags a block on Edit Schedule and paints the Study Planner grid with real touch input
// while those containers are scrolled sideways.
// Screenshots go to visual/out/responsive/<route>-<width>.png. It uses the one test account
// (e2e/testUser.mjs): its data is cleared and re-seeded through the API; the account is kept.
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { resetTestUser } from './testUser.mjs';

const BASE = process.env.RESPONSIVE_BASE_URL ?? 'http://localhost:3000';
const WIDTHS = [375, 768, 1024, 1440, 1920];
const HEIGHT = { 375: 812, 768: 1024, 1024: 768, 1440: 900, 1920: 1080 };
const TOUCH = new Set([375, 768]); // phone and tablet: touch input, coarse pointer
const outDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'visual', 'out', 'responsive');
mkdirSync(outDir, { recursive: true });

let token = null;

async function api(method, path, body) {
  const r = await fetch(`${BASE}${path}`, {
    method,
    headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: `Bearer ${token}` } : {}) },
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await r.text();
  let json = null;
  try { json = text ? JSON.parse(text) : null; } catch { json = text; }
  if (!r.ok) throw new Error(`${method} ${path} -> ${r.status} ${text.slice(0, 200)}`);
  return json;
}

const ymd = (days) => {
  const d = new Date(Date.now() + days * 86400000);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
};

// ---------- seed ----------
async function seed() {
  token = (await resetTestUser(BASE)).token;
  const days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  await api('PUT', '/api/settings/preferences', {
    studyHours: days.map((day) => ({ day, start: '08:00', end: '22:00' })),
    personalTime: [
      { day: 'Mon', start: '18:00', end: '19:00' },
      { day: 'Wed', start: '18:00', end: '19:00' },
      { day: 'Sat', start: '16:00', end: '19:00' },
    ],
    maxStudyMinutesPerDay: 300,
    sessionMinutes: 90,
    breakMinutes: 15,
  });
  const course = await api('POST', '/api/courses', { code: 'CS 3354', name: 'Software Engineering', term: 'Fall 2026' });
  await api('POST', '/api/tasks', {
    title: 'Sequence diagrams for the project report', dueAt: ymd(3), type: 'assignment', courseId: course.id, estimatedMinutes: 180, priority: 1,
  });
  await api('POST', '/api/tasks', {
    title: 'Review Ch. 9', dueAt: ymd(5), type: 'exam', courseId: course.id, estimatedMinutes: 120, priority: 2,
  });
  await api('POST', '/api/agent/generate');
  const sched = await api('GET', '/api/schedule');
  if (!sched.blocks?.length) throw new Error('generate made no blocks');
  return sched.blocks;
}

// ---------- layout measurement (runs in the page) ----------
// `width` is the device width the context was opened with. Mobile emulation widens the layout
// viewport (innerWidth) to fit content that overflows, so the check compares against `width` too.
function measure(width) {
  const vw = Math.min(window.innerWidth, width);
  const doc = document.documentElement;
  const offenders = [];
  const scrollers = [];
  for (const el of document.querySelectorAll('body *')) {
    if (el.closest('.sr-only')) continue;
    const inScroller = el.parentElement?.closest('.cal, .avail');
    if (inScroller) continue; // clipped by its own scroll container
    const r = el.getBoundingClientRect();
    if (r.width === 0 && r.height === 0) continue;
    const isScroller = el.matches('.cal, .avail');
    if (isScroller) scrollers.push({ sel: el.className, scroll: el.scrollWidth > el.clientWidth });
    if (r.right > vw + 0.5 || r.left < -0.5) {
      const id = el.id ? `#${el.id}` : '';
      const cls = typeof el.className === 'string' && el.className ? `.${el.className.trim().split(/\s+/).join('.')}` : '';
      offenders.push(`${el.tagName.toLowerCase()}${id}${cls} [${Math.round(r.left)}..${Math.round(r.right)}]`);
    }
  }
  // Wide screens: the top bar must span the whole window and the page must sit in the middle,
  // not stop at 1280px against the left edge.
  const bar = document.querySelector('.topbar')?.getBoundingClientRect();
  const frame = document.querySelector('.frame')?.getBoundingClientRect();
  const layout = [];
  if (!bar || bar.left > 0.5 || bar.right < vw - 0.5) layout.push(`topbar spans ${bar ? `${Math.round(bar.left)}..${Math.round(bar.right)}` : 'nothing'} of 0..${vw}`);
  if (!frame || Math.abs(frame.left - (vw - frame.right)) > 2) layout.push(`page not centred: ${frame ? `${Math.round(frame.left)}px left, ${Math.round(vw - frame.right)}px right` : 'no .frame'}`);
  return { vw, iw: window.innerWidth, sw: doc.scrollWidth, offenders: offenders.concat(layout), scrollers };
}

// ---------- touch helpers (real touch input through CDP) ----------
async function touchDrag(cdp, from, to, steps = 10) {
  const pt = (p) => [{ x: p.x, y: p.y, id: 1 }];
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pt(from) });
  for (let i = 1; i <= steps; i++) {
    const p = { x: from.x + ((to.x - from.x) * i) / steps, y: from.y + ((to.y - from.y) * i) / steps };
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: pt(p) });
  }
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
}

const results = [];
const extra = [];
const browser = await chromium.launch();

try {
  const blocks = await seed();
  const b0 = blocks[0];
  const blockParam = encodeURIComponent(`${b0.assignmentId}|${b0.day}|${b0.start}`);
  const ROUTES = [
    { name: 'login', path: '/login', auth: false },
    { name: 'signup', path: '/signup', auth: false },
    { name: 'home', path: '/' },
    { name: 'schedule', path: '/schedule' },
    { name: 'schedule-edit', path: `/schedule/edit?block=${blockParam}` },
    { name: 'schedule-create', path: '/schedule/create' },
    { name: 'schedule-create-upload', path: '/schedule/create/upload' },
    { name: 'planner-settings', path: '/planner/settings' },
    { name: 'assistant', path: '/assistant' },
    { name: 'account', path: '/account' },
    { name: 'account-edit', path: '/account/edit' },
    { name: 'account-notifications', path: '/account/notifications' },
  ];

  for (const width of WIDTHS) {
    const touch = TOUCH.has(width);
    const contexts = {};
    for (const auth of [false, true]) {
      const ctx = await browser.newContext({
        viewport: { width, height: HEIGHT[width] },
        hasTouch: touch,
        isMobile: touch,
        deviceScaleFactor: 1,
        timezoneId: 'America/Chicago',
      });
      if (auth) await ctx.addInitScript((t) => localStorage.setItem('cramberry.token', t), token);
      contexts[auth] = ctx;
    }
    for (const route of ROUTES) {
      const page = await contexts[route.auth !== false].newPage();
      await page.goto(`${BASE}${route.path}`, { waitUntil: 'networkidle' });
      await page.waitForFunction(() => !/Loading/.test(document.querySelector('main')?.innerText ?? 'Loading'), null, { timeout: 15000 }).catch(() => {});
      await page.waitForTimeout(150);
      const m = await page.evaluate(measure, width);
      const url = new URL(page.url());
      const wrongPage = route.auth !== false && url.pathname === '/login';
      const ok = m.sw <= m.vw && m.offenders.length === 0 && !wrongPage;
      const file = `${route.name}-${width}.png`;
      await page.screenshot({ path: join(outDir, file), fullPage: true });
      results.push({ route: route.path.split('?')[0], width, ok, sw: m.sw, vw: m.vw, iw: m.iw, scrollers: m.scrollers, offenders: m.offenders, wrongPage });
      await page.close();
    }
    for (const ctx of Object.values(contexts)) await ctx.close();
  }

  // ---------- touch interactions inside scrolled containers (375 px phone) ----------
  const ctx = await browser.newContext({
    viewport: { width: 375, height: 812 }, hasTouch: true, isMobile: true, deviceScaleFactor: 1, timezoneId: 'America/Chicago',
  });
  await ctx.addInitScript((t) => localStorage.setItem('cramberry.token', t), token);
  const page = await ctx.newPage();
  const cdp = await ctx.newCDPSession(page);

  // 1. Study Planner: swipe the hour header to scroll the grid, then paint with Erase at the far right.
  await page.goto(`${BASE}/planner/settings`, { waitUntil: 'networkidle' });
  await page.waitForSelector('.avail .cell');
  const grid = page.locator('.avail');
  await grid.scrollIntoViewIfNeeded();
  const g = await grid.boundingBox();
  const headY = g.y + 14;
  await touchDrag(cdp, { x: g.x + g.width - 20, y: headY }, { x: g.x + 40, y: headY }, 12);
  await page.waitForTimeout(400);
  const swipeScroll = await grid.evaluate((el) => el.scrollLeft);
  extra.push({ name: 'settings: swipe on the hour row scrolls the grid sideways', ok: swipeScroll > 0, detail: `scrollLeft ${swipeScroll}` });

  await grid.evaluate((el) => { el.scrollLeft = el.scrollWidth; });
  await page.getByRole('button', { name: 'Erase' }).click();
  const tueCells = page.locator('.avail .cell[aria-label^="Tuesday "]');
  const n = await tueCells.count();
  const c1 = await tueCells.nth(n - 3).boundingBox();
  const c2 = await tueCells.nth(n - 1).boundingBox();
  await touchDrag(cdp, { x: c1.x + c1.width / 2, y: c1.y + c1.height / 2 }, { x: c2.x + c2.width / 2, y: c2.y + c2.height / 2 }, 12);
  const painted = await tueCells.evaluateAll((els) => els.slice(-3).map((e) => e.getAttribute('aria-label')));
  const scrolledLeft = await grid.evaluate((el) => el.scrollLeft);
  extra.push({
    name: 'settings: touch-paint the last 3 Tuesday cells while the grid is scrolled',
    ok: scrolledLeft > 0 && painted.every((l) => l.endsWith(': Free')),
    detail: `scrollLeft ${scrolledLeft}; ${painted.map((l) => l.split(': ').pop()).join(', ')}`,
  });

  // 2. Edit Schedule: scroll the calendar so the selected block sits right of the hour column,
  //    then touch-drag it one day later.
  await page.goto(`${BASE}/schedule/edit?block=${blockParam}`, { waitUntil: 'networkidle' });
  await page.waitForSelector('.block.selected');
  const cal = page.locator('.cal');
  await cal.scrollIntoViewIfNeeded();
  const inView = await cal.evaluate((el) => {
    const c = el.getBoundingClientRect();
    const b = el.querySelector('.block.selected').getBoundingClientRect();
    return { ok: b.left >= c.left && b.right <= c.right, scrollLeft: Math.round(el.scrollLeft) };
  });
  extra.push({ name: 'edit: the ?block= deep link scrolls the calendar to the selected block', ok: inView.ok, detail: `scrollLeft ${inView.scrollLeft}` });
  const DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  // Pick a target day with nothing else at that time, preferring the day after.
  const sameTime = (x) => x.start < b0.end && b0.start < x.end;
  const busy = new Set(blocks.filter((x) => x !== b0 && sameTime(x)).map((x) => x.day));
  const from = DAYS.indexOf(b0.day);
  const order = [1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6].map((k) => from + k).filter((i) => i >= 0 && i < 7);
  const personalHit = (d) => (d === 'Mon' || d === 'Wed') ? b0.start < '19:00' && '18:00' < b0.end : d === 'Sat' ? b0.start < '19:00' && '16:00' < b0.end : false;
  // Also skip days the move would push past the 300 min daily maximum set in seed().
  const mins = (t) => Number(t.slice(0, 2)) * 60 + Number(t.slice(3, 5));
  const len = mins(b0.end) - mins(b0.start);
  const dayTotal = (d) => blocks.filter((x) => x !== b0 && x.day === d).reduce((n, x) => n + mins(x.end) - mins(x.start), 0);
  const toIdx = order.find((i) => !busy.has(DAYS[i]) && !personalHit(DAYS[i]) && dayTotal(DAYS[i]) + len <= 300);
  // Centre the source and target columns in the visible part, away from the edges where a drag
  // auto-scrolls the calendar.
  const geo = await cal.evaluate((el, [fromIdx, toI]) => {
    const colW = (el.scrollWidth - 54) / 7;
    const mid = 54 + ((fromIdx + toI) / 2 + 0.5) * colW;
    el.scrollLeft = Math.max(0, mid - el.clientWidth / 2);
    return { colW, scrollLeft: el.scrollLeft, canScroll: el.scrollWidth > el.clientWidth };
  }, [from, toIdx]);
  await page.waitForTimeout(100);
  const sel = await page.locator('.block.selected').boundingBox();
  const start = { x: sel.x + sel.width / 2, y: sel.y + sel.height / 2 };
  const end = { x: start.x + (toIdx - from) * geo.colW, y: start.y };
  await touchDrag(cdp, start, end, 14);
  const moved = await (async () => {
    for (let i = 0; i < 40; i++) {
      const s = await api('GET', '/api/schedule');
      const hit = s.blocks.find((x) => x.assignmentId === b0.assignmentId && x.day === DAYS[toIdx] && x.start === b0.start);
      if (hit) return true;
      await page.waitForTimeout(250);
    }
    return false;
  })();
  const alertText = moved ? '' : ((await page.locator('.alert').first().textContent().catch(() => '')) ?? '');
  extra.push({
    name: `edit: touch-drag the selected block ${b0.day} -> ${DAYS[toIdx]} while the calendar is scrolled`,
    ok: geo.canScroll && geo.scrollLeft > 0 && moved,
    detail: `scrollLeft ${Math.round(geo.scrollLeft)}, column ${Math.round(geo.colW)}px${alertText ? `; alert: ${alertText.slice(0, 120)}` : ''}`,
  });
  await page.screenshot({ path: join(outDir, 'schedule-edit-375-after-drag.png'), fullPage: true });
  await ctx.close();
} catch (e) {
  extra.push({ name: 'no unexpected error', ok: false, detail: e.stack?.split('\n').slice(0, 3).join(' | ') ?? String(e) });
} finally {
  await browser.close();
}

// ---------- report ----------
const routes = [...new Set(results.map((r) => r.route))];
const pad = (s, n) => String(s).padEnd(n);
console.log(`\n${pad('route', 26)}${WIDTHS.map((w) => pad(w, 8)).join('')}`);
for (const route of routes) {
  const cells = WIDTHS.map((w) => {
    const r = results.find((x) => x.route === route && x.width === w);
    return pad(r ? (r.ok ? 'PASS' : 'FAIL') : '-', 8);
  });
  console.log(`${pad(route, 26)}${cells.join('')}`);
}
for (const r of results.filter((x) => !x.ok)) {
  console.log(`\nFAIL ${r.route} @ ${r.width}: scrollWidth ${r.sw}, innerWidth ${r.iw}, device width ${r.vw}${r.wrongPage ? ', redirected to /login' : ''}`);
  for (const o of r.offenders.slice(0, 12)) console.log(`  sticks out: ${o}`);
  if (r.offenders.length > 12) console.log(`  ... and ${r.offenders.length - 12} more`);
}
const inner = results.filter((r) => r.scrollers.some((s) => s.scroll)).map((r) => `${r.route}@${r.width}`);
console.log(`\nscrolling inside .cal/.avail: ${inner.join(', ') || 'none'}`);
console.log('');
for (const x of extra) console.log(`${x.ok ? 'PASS' : 'FAIL'}  ${x.name}${x.detail ? `  (${x.detail})` : ''}`);

const failed = results.filter((r) => !r.ok).length + extra.filter((x) => !x.ok).length;
const total = results.length + extra.length;
console.log(`\n${total - failed}/${total} passed (${results.length} route x width checks, ${extra.length} touch checks)`);
process.exit(failed ? 1 : 0);
