// The one test account every check uses. The checks reuse it and never create other accounts.
// resetTestUser() signs in (creating the account only if it doesn't exist yet) and clears its data,
// so every run starts from the same empty state.
export const TEST_USER = {
  name: 'Test Student',
  email: 'student@cramberry.test',
  password: 'Cramberry123',
  utcOffsetMinutes: -360,
};

export async function resetTestUser(base) {
  let token = null;
  const call = async (method, path, body) => {
    const r = await fetch(`${base}${path}`, {
      method,
      headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: `Bearer ${token}` } : {}) },
      body: body ? JSON.stringify(body) : undefined,
    });
    const text = await r.text();
    return { status: r.status, body: text ? JSON.parse(text) : null };
  };
  const ok = (res, what) => {
    if (res.status >= 300) throw new Error(`${what} -> ${res.status} ${JSON.stringify(res.body)}`);
    return res.body;
  };

  const { email, password, name, utcOffsetMinutes } = TEST_USER;
  let auth = await call('POST', '/api/auth/login', { email, password });
  if (auth.status === 401) auth = await call('POST', '/api/auth/register', { name, email, password, utcOffsetMinutes });
  token = ok(auth, 'sign in').token;

  for (const t of ok(await call('GET', '/api/tasks'), 'list tasks')) ok(await call('DELETE', `/api/tasks/${t.id}`), 'delete task');
  for (const c of ok(await call('GET', '/api/courses'), 'list courses')) ok(await call('DELETE', `/api/courses/${c.id}`), 'delete course');
  ok(await call('PUT', '/api/settings/preferences', { studyHours: [], personalTime: [] }), 'reset preferences');
  ok(await call('DELETE', '/api/schedule'), 'clear schedule');
  ok(await call('PUT', '/api/settings/notifications', {}), 'reset notifications');
  ok(await call('POST', '/api/notifications/read-all'), 'mark notifications read');
  const student = ok(await call('PATCH', '/api/users/me', { name, utcOffsetMinutes }), 'reset profile');
  return { token, student };
}
