import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createTaskApi } from './api.mjs';

function setup(data = [], ok = true) {
  const calls = [];
  const api = createTaskApi({ baseUrl: 'http://localhost:8080/', getToken: () => 'test-session', fetchImpl: async (url, options) => { calls.push({ url, options }); return { ok, status: ok ? 200 : 400, json: async () => data }; } });
  return { api, calls };
}
test('UC-13 sends the selected status and returns the saved task', async () => {
  const { api, calls } = setup({ status: 'COMPLETED' });
  assert.equal((await api.updateStatus('task-1', 'COMPLETED')).status, 'COMPLETED');
  assert.equal(calls[0].url, 'http://localhost:8080/api/tasks/task-1/status');
  assert.equal(calls[0].options.method, 'PUT');
  assert.deepEqual(JSON.parse(calls[0].options.body), { status: 'COMPLETED' });
});
test('UC-13 rejects unsupported statuses before sending', () => {
  const { api, calls } = setup(); assert.throws(() => api.updateStatus('task-1', 'unknown')); assert.equal(calls.length, 0);
});
test('FR-24 and FR-26 send combined filters', async () => {
  const { api, calls } = setup(); await api.listTasks({ courseId: 'course 1', from: '2026-11-23', to: '2026-11-30', overdue: true });
  assert.deepEqual(Object.fromEntries(new URL(calls[0].url).searchParams), { courseId: 'course 1', from: '2026-11-23', to: '2026-11-30', overdue: 'true' });
});
test('UC-14 requests selected week', async () => {
  const { api, calls } = setup(); await api.weeklyReport('2026-11-23'); assert.equal(calls[0].url, 'http://localhost:8080/api/reports/weekly?start=2026-11-23');
});
test('UC-13 reports a failed update', async () => {
  const { api } = setup({ error: 'Task not found' }, false); await assert.rejects(api.updateStatus('task-1', 'IN_PROGRESS'), /Task not found/);
});
