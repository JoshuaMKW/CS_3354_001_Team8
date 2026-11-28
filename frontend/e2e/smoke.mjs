// End-to-end smoke test: drives every page of the app against the real backend.
// Usage: start the backend (127.0.0.1:8080) and `npm run dev` (port 3000), then `npm run smoke`.
// Env: SMOKE_BASE_URL (default http://localhost:3000). Uses the one test account (e2e/testUser.mjs):
// its data is cleared first, then it signs in through the Login page. The account is kept.
import { chromium } from 'playwright';
import { TEST_USER, resetTestUser } from './testUser.mjs';

const BASE = process.env.SMOKE_BASE_URL ?? 'http://localhost:3000';
const results = [];
const check = (name, ok, detail = '') => {
  results.push({ name, ok });
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
};

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, timezoneId: 'America/Chicago' });
const api = async (method, path, body) =>
  page.evaluate(async ([m, p, b]) => {
    const r = await fetch(p, {
      method: m,
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${localStorage.getItem('cramberry.token')}` },
      body: b ? JSON.stringify(b) : undefined,
    });
    const text = await r.text();
    return { status: r.status, body: text ? JSON.parse(text) : null };
  }, [method, path, body]);

try {
  // Sign in through the Login page
  await resetTestUser(BASE);
  await page.goto(`${BASE}/login`);
  await page.fill('input[type="email"]', TEST_USER.email);
  await page.fill('input[type="password"]', TEST_USER.password);
  await page.click('button[type="submit"]');
  await page.waitForURL(`${BASE}/`);
  check('sign in lands on Home with the name in the topbar', (await page.textContent('.topbar .who'))?.includes(TEST_USER.name));

  // Study Planner settings: paint 8 am-10 pm on every day, save, reload
  await page.goto(`${BASE}/planner/settings`);
  await page.waitForSelector('.avail');
  for (const day of ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']) {
    const cells = page.locator(`[aria-label^="${day} "]`);
    const first = await cells.first().boundingBox();
    const last = await cells.last().boundingBox();
    await page.mouse.move(first.x + 5, first.y + 5);
    await page.mouse.down();
    await page.mouse.move(last.x + 5, last.y + 5, { steps: 8 });
    await page.mouse.up();
  }
  await page.click('button:has-text("Save settings")');
  await page.waitForSelector('text=Saved. Cramberry is rebuilding your schedule.');
  const prefs = await api('GET', '/api/settings/preferences');
  check('settings saved through the painted grid', prefs.body.saved && prefs.body.preferences.studyHours.length === 7,
    `${prefs.body.preferences.studyHours.length} study ranges, expected 7 (one per day)`);

  // Create Schedule: add a task through the form, then generate
  await page.goto(`${BASE}/schedule/create`);
  const due = new Date(Date.now() + 2 * 86400000);
  const ymd = `${due.getFullYear()}-${String(due.getMonth() + 1).padStart(2, '0')}-${String(due.getDate()).padStart(2, '0')}`;
  await page.fill('#cs-t-title', 'Smoke essay');
  await page.fill('#cs-t-date', ymd);
  await page.fill('#cs-t-est', '120');
  await page.click('button:has-text("Add task")');
  await page.waitForSelector('text=Smoke essay');
  check('task added through the form', (await api('GET', '/api/tasks')).body.length === 1);
  await page.click('button:has-text("Generate my schedule")');
  await page.waitForSelector('text=/Planned by/', { timeout: 30000 });
  const sched = await api('GET', '/api/schedule');
  check('generate builds a schedule', sched.body.blocks.length > 0, `${sched.body.blocks.length} blocks`);

  // View Schedule
  await page.goto(`${BASE}/schedule`);
  await page.waitForSelector('.cal .block.study');
  check('View Schedule draws the blocks', (await page.locator('.cal .block.study').count()) === sched.body.blocks.length);

  // Edit Schedule: select a block, rename it, save
  const b = sched.body.blocks[0];
  await page.goto(`${BASE}/schedule/edit?block=${encodeURIComponent(`${b.assignmentId}|${b.day}|${b.start}`)}`);
  await page.waitForSelector('.block.selected');
  const rebalance = page.locator('.check input[type="checkbox"]').last();
  if (await rebalance.isChecked().catch(() => false)) await rebalance.uncheck();
  await page.fill('#t', 'Smoke essay outline').catch(async () => {
    await page.locator('aside input.input').first().fill('Smoke essay outline');
  });
  await page.click('button:has-text("Save changes")');
  await page.waitForFunction(() => !document.body.innerText.includes('Saving'), null, { timeout: 30000 });
  const after = await api('GET', '/api/schedule');
  check('Edit Schedule saves a rename', after.body.blocks.some((x) => x.title === 'Smoke essay outline'));

  // AI Assistant
  await page.goto(`${BASE}/assistant`);
  await page.fill('textarea[placeholder="Ask Cramberry anything..."]', 'What should I work on first?');
  await page.keyboard.press('Enter');
  await page.waitForFunction(() => document.querySelectorAll('.ai-main').length && !document.body.innerText.includes('Thinking'), null, { timeout: 30000 });
  check('AI Assistant answers', (await page.textContent('.ai-main'))?.includes('Cramberry'));

  // Account pages render
  for (const [path, text] of [['/account', 'Account Info'], ['/account/edit', 'Time zone'], ['/account/notifications', 'Inbox']]) {
    await page.goto(`${BASE}${path}`);
    check(`${path} renders`, await page.waitForSelector(`text=${text}`, { timeout: 10000 }).then(() => true, () => false));
  }
} catch (e) {
  check('no unexpected error', false, e.message.split('\n')[0]);
} finally {
  await browser.close();
}

const failed = results.filter((r) => !r.ok).length;
console.log(`\n${results.length - failed}/${results.length} passed`);
process.exit(failed ? 1 : 0);
