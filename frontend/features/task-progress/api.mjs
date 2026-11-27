export const statuses = ['NOT_STARTED', 'IN_PROGRESS', 'COMPLETED'];

export function createTaskApi({ baseUrl = '', getToken, fetchImpl = globalThis.fetch }) {
  async function request(path, method = 'GET', body) {
    const token = await getToken();
    if (!token) throw new Error('Please sign in first.');
    const response = await fetchImpl(`${baseUrl.replace(/\/$/, '')}${path}`, {
      method,
      headers: { Authorization: `Bearer ${token}`, ...(body ? { 'Content-Type': 'application/json' } : {}) },
      ...(body ? { body: JSON.stringify(body) } : {})
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `Request failed (${response.status})`);
    return data;
  }
  return {
    listCourses: () => request('/api/courses'),
    listTasks(filters = {}) {
      const query = new URLSearchParams();
      for (const key of ['courseId', 'from', 'to', 'status', 'overdue']) {
        if (filters[key]) query.set(key, String(filters[key]));
      }
      return request(`/api/tasks${query.size ? `?${query}` : ''}`);
    },
    updateStatus(id, status) {
      if (!statuses.includes(status)) throw new Error('Invalid task status.');
      return request(`/api/tasks/${encodeURIComponent(id)}/status`, 'PUT', { status });
    },
    weeklyReport(start = '') {
      return request(`/api/reports/weekly${start ? `?${new URLSearchParams({ start })}` : ''}`);
    }
  };
}
