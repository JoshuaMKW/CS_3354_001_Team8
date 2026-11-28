#!/usr/bin/env node
// Visual regression harness: wireframe reference HTML vs. the live app (API mocked).
// Usage: npm run visual -- <settings|edit>
// Env:   VISUAL_BASE_URL (default http://localhost:3000; a Vite dev server is started if nothing answers)

import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromium } from 'playwright';
import pixelmatch from 'pixelmatch';
import { PNG } from 'pngjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const outDir = join(here, 'out');

const TARGETS = {
  settings: { reference: 'design/study-planner-settings.html', route: '/planner/settings' },
  // Class (lecture/lab) blocks have no backend source, so compare against the copy without them.
  edit: { reference: 'design/edit-schedule.nolectures.html', route: '/schedule/edit' },
};

const VIEWPORT = { width: 1280, height: 900 };
const TIMEZONE = 'America/Chicago';
const FROZEN_NOW = new Date('2026-11-09T08:00:00-06:00'); // Monday 08:00 CST
const FAKE_TOKEN = 'visual-harness-fake-token';
const BASE_URL = process.env.VISUAL_BASE_URL ?? 'http://localhost:3000';

const name = process.argv[2];
if (!TARGETS[name]) {
  console.error(`usage: npm run visual -- <${Object.keys(TARGETS).join('|')}>`);
  process.exit(2);
}
const target = TARGETS[name];

async function isUp(url) {
  try {
    const res = await fetch(url, { signal: AbortSignal.timeout(1500) });
    return res.ok;
  } catch {
    return false;
  }
}

async function ensureDevServer() {
  if (await isUp(BASE_URL)) return null;
  const port = new URL(BASE_URL).port || '3000';
  const child = spawn('npx', ['vite', '--port', port, '--strictPort'], {
    cwd: root,
    stdio: ['ignore', 'pipe', 'pipe'],
    detached: process.platform !== 'win32',
  });
  let log = '';
  child.stdout.on('data', (d) => (log += d));
  child.stderr.on('data', (d) => (log += d));
  for (let i = 0; i < 100; i++) {
    if (await isUp(BASE_URL)) return child;
    if (child.exitCode !== null) break;
    await new Promise((r) => setTimeout(r, 200));
  }
  stopServer(child);
  throw new Error(`Vite dev server did not start on ${BASE_URL}\n${log}`);
}

function stopServer(child) {
  if (!child || child.exitCode !== null) return;
  try {
    if (process.platform !== 'win32') process.kill(-child.pid, 'SIGTERM');
    else child.kill();
  } catch {
    child.kill();
  }
}

async function settle(page) {
  await page.waitForLoadState('networkidle');
  await page.evaluate(() => document.fonts.ready);
  await page.waitForTimeout(150);
}

async function shoot(context, url, file, setup) {
  const page = await context.newPage();
  if (setup) await setup(page);
  await page.goto(url, { waitUntil: 'load' });
  await settle(page);
  const buf = await page.screenshot({ fullPage: true, path: file });
  await page.close();
  return PNG.sync.read(buf);
}

/** Fixture keys: "METHOD /api/path" (query ignored) or "METHOD /api/path?query" (exact, preferred).
 *  Value is the JSON body, or {"$status": n, "$body": ...} for a non-200 reply. */
function installMocks(page, fixtures, unmatched) {
  const origin = new URL(BASE_URL).origin;
  return page.route(
    (url) => url.origin === origin && url.pathname.startsWith('/api/'),
    async (route) => {
      const req = route.request();
      const u = new URL(req.url());
      const exact = `${req.method()} ${u.pathname}${u.search}`;
      const loose = `${req.method()} ${u.pathname}`;
      const hit = exact in fixtures ? fixtures[exact] : loose in fixtures ? fixtures[loose] : undefined;
      if (hit === undefined) {
        unmatched.push(exact);
        return route.fulfill({ status: 404, contentType: 'application/json', body: JSON.stringify({ error: 'No such endpoint' }) });
      }
      const wrapped = hit && typeof hit === 'object' && !Array.isArray(hit) && '$status' in hit;
      const status = wrapped ? hit.$status : 200;
      const body = wrapped ? hit.$body : hit;
      if (status === 204 || body === undefined) return route.fulfill({ status });
      return route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
    },
  );
}

function padTo(img, width, height) {
  if (img.width === width && img.height === height) return img;
  const out = new PNG({ width, height });
  out.data.fill(255);
  PNG.bitblt(img, out, 0, 0, img.width, img.height, 0, 0);
  return out;
}

function compare(ref, app) {
  const width = Math.max(ref.width, app.width);
  const height = Math.max(ref.height, app.height);
  const a = padTo(ref, width, height);
  const b = padTo(app, width, height);
  const diff = new PNG({ width, height });
  pixelmatch(a.data, b.data, diff.data, width, height, { threshold: 0.1 });
  // Padding counts as mismatch regardless of colour: recount over the full canvas.
  let mismatched = 0;
  let refInk = 0; // non-white pixels in the reference
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      if (x < ref.width && y < ref.height) {
        const j = (y * ref.width + x) * 4;
        if (ref.data[j] < 250 || ref.data[j + 1] < 250 || ref.data[j + 2] < 250) refInk++;
      }
      const inBoth = x < ref.width && y < ref.height && x < app.width && y < app.height;
      // pixelmatch paints mismatches red (255,0,0) by default.
      const isDiff = diff.data[i] === 255 && diff.data[i + 1] === 0 && diff.data[i + 2] === 0;
      if (!inBoth) {
        diff.data[i] = 255; diff.data[i + 1] = 0; diff.data[i + 2] = 255; diff.data[i + 3] = 255; // magenta = padding
        mismatched++;
      } else if (isDiff) {
        mismatched++;
      }
    }
  }
  return { diff, mismatched, total: width * height, refInk, width, height };
}

async function main() {
  mkdirSync(outDir, { recursive: true });
  const fixtures = JSON.parse(readFileSync(join(here, 'fixtures', `${name}.json`), 'utf8'));
  // Optional "localStorage" entry: values the app keeps on the device (e.g. personal-time labels).
  const seededStorage = fixtures.localStorage ?? {};
  delete fixtures.localStorage;
  // Optional "route" entry: open the page at this path (e.g. a deep link that selects a block).
  const route = fixtures.route ?? target.route;
  delete fixtures.route;
  const server = await ensureDevServer();
  const browser = await chromium.launch();
  const unmatched = [];
  try {
    const ctxOpts = { viewport: VIEWPORT, deviceScaleFactor: 1, timezoneId: TIMEZONE, locale: 'en-US' };

    const refCtx = await browser.newContext(ctxOpts);
    const refFile = join(outDir, `${name}-reference.png`);
    const ref = await shoot(refCtx, pathToFileURL(join(root, target.reference)).href, refFile);
    await refCtx.close();

    const appCtx = await browser.newContext(ctxOpts);
    await appCtx.addInitScript(([key, token, seeded]) => {
      try {
        localStorage.setItem(key, token);
        for (const [k, v] of Object.entries(seeded)) localStorage.setItem(k, typeof v === 'string' ? v : JSON.stringify(v));
      } catch { /* ignore */ }
    }, ['cramberry.token', FAKE_TOKEN, seededStorage]);
    const appFile = join(outDir, `${name}-app.png`);
    const app = await shoot(appCtx, BASE_URL + route, appFile, async (page) => {
      await page.clock.setFixedTime(FROZEN_NOW);
      await installMocks(page, fixtures, unmatched);
      page.on('pageerror', (e) => console.warn(`[page error] ${e.message}`));
    });
    await appCtx.close();

    const { diff, mismatched, total, refInk, width, height } = compare(ref, app);
    writeFileSync(join(outDir, `${name}-diff.png`), PNG.sync.write(diff));

    if (unmatched.length) {
      console.warn(`Unmocked API calls (answered 404):\n  ${[...new Set(unmatched)].join('\n  ')}`);
    }
    console.log(`reference ${ref.width}x${ref.height}, app ${app.width}x${app.height}, compared ${width}x${height}`);
    console.log(`wrote visual/out/${name}-reference.png, ${name}-app.png, ${name}-diff.png`);
    console.log(`(secondary) mismatched / non-white reference pixels: ${((mismatched / Math.max(refInk, 1)) * 100).toFixed(1)}%`);
    console.log(`${name}: ${((mismatched / total) * 100).toFixed(2)}% mismatched pixels (${mismatched} of ${total})`);
  } finally {
    await browser.close();
    stopServer(server);
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
