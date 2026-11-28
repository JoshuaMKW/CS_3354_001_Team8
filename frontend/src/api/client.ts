// Typed client for every endpoint in backend/API.md.
// All paths are same-origin "/api/..."; Vite proxies them to the backend in dev.

import type {
  AgentRun,
  AppNotification,
  AuthResponse,
  Block,
  BlocksResponse,
  ChatResponse,
  Course,
  CourseInput,
  CreateTaskRequest,
  Dashboard,
  Day,
  ErrorBody,
  GenerateResponse,
  Health,
  LoginRequest,
  MoveBlockRequest,
  NotificationSettings,
  Preferences,
  PreferencesResponse,
  RegisterRequest,
  RemoveBlockRequest,
  SaveNotificationSettingsResponse,
  SavePreferencesResponse,
  Schedule,
  Student,
  Task,
  TaskFilters,
  TaskStatus,
  UpdateMeRequest,
  UpdateTaskRequest,
  WeeklyReport,
} from './types';

export const TOKEN_KEY = 'cramberry.token';

/* ---------- Token storage ---------- */

export function getToken(): string | null {
  try {
    return localStorage.getItem(TOKEN_KEY);
  } catch {
    return null;
  }
}

export function setToken(token: string | null): void {
  try {
    if (token) localStorage.setItem(TOKEN_KEY, token);
    else localStorage.removeItem(TOKEN_KEY);
  } catch {
    /* storage unavailable: session lasts for this page only */
  }
}

/* ---------- Errors ---------- */

export class ApiError extends Error {
  readonly status: number;
  readonly details?: string[];
  /** Parsed JSON error body (or null if the response had none). */
  readonly body: ErrorBody | null;

  constructor(status: number, body: ErrorBody | null) {
    super(body?.error ?? `Request failed (${status})`);
    this.name = 'ApiError';
    this.status = status;
    this.details = body?.details;
    this.body = body;
  }
}

/* ---------- 401 handling ---------- */

type UnauthorizedHandler = () => void;

let onUnauthorized: UnauthorizedHandler = () => {
  if (window.location.pathname !== '/login') window.location.assign('/login');
};

/** AuthContext installs a router-aware handler; the default does a full page load to /login. */
export function setUnauthorizedHandler(handler: UnauthorizedHandler): void {
  onUnauthorized = handler;
}

/* ---------- Core request ---------- */

type Method = 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';

interface RequestOptions {
  body?: unknown;
  query?: Record<string, string | number | boolean | undefined>;
  /**
   * When false, a 401 is just thrown as an ApiError (no sign-out/redirect).
   * Used for login/register and for endpoints where 401 means "wrong password".
   */
  redirectOn401?: boolean;
}

export async function request<T>(method: Method, path: string, opts: RequestOptions = {}): Promise<T> {
  let url = path;
  if (opts.query) {
    const qs = new URLSearchParams();
    for (const [k, v] of Object.entries(opts.query)) if (v !== undefined) qs.set(k, String(v));
    const s = qs.toString();
    if (s) url += `?${s}`;
  }

  const headers: Record<string, string> = { Accept: 'application/json' };
  const token = getToken();
  if (token) headers.Authorization = `Bearer ${token}`;
  if (opts.body !== undefined) headers['Content-Type'] = 'application/json';

  const res = await fetch(url, {
    method,
    headers,
    body: opts.body !== undefined ? JSON.stringify(opts.body) : undefined,
  });

  if (res.status === 204) return undefined as T;

  const text = await res.text();
  let data: unknown = null;
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = null;
    }
  }

  if (!res.ok) {
    const body = (data && typeof data === 'object' ? data : null) as ErrorBody | null;
    if (res.status === 401 && opts.redirectOn401 !== false) {
      setToken(null);
      onUnauthorized();
    }
    throw new ApiError(res.status, body);
  }
  return data as T;
}

/* ---------- Health ---------- */

export const getHealth = () => request<Health>('GET', '/api/health');

/* ---------- Accounts ---------- */

/** Creates an account, stores the token, returns the auth response. */
export async function register(body: RegisterRequest): Promise<AuthResponse> {
  const r = await request<AuthResponse>('POST', '/api/auth/register', { body, redirectOn401: false });
  setToken(r.token);
  return r;
}

/** Signs in, stores the token, returns the auth response. */
export async function login(body: LoginRequest): Promise<AuthResponse> {
  const r = await request<AuthResponse>('POST', '/api/auth/login', { body, redirectOn401: false });
  setToken(r.token);
  return r;
}

/** Ends this session server-side (best effort) and always clears the stored token. */
export async function logout(): Promise<void> {
  try {
    if (getToken()) await request<void>('POST', '/api/auth/logout', { redirectOn401: false });
  } finally {
    setToken(null);
  }
}

export const getMe = () => request<Student>('GET', '/api/users/me');
export const updateMe = (body: UpdateMeRequest) => request<Student>('PATCH', '/api/users/me', { body });

/** Permanently deletes the account. A 401 here means "Password is incorrect" (no redirect). */
export async function deleteMe(password: string): Promise<void> {
  await request<void>('DELETE', '/api/users/me', { body: { password }, redirectOn401: false });
  setToken(null);
}

/* ---------- Courses ---------- */

export const listCourses = () => request<Course[]>('GET', '/api/courses');
export const createCourse = (body: CourseInput) => request<Course>('POST', '/api/courses', { body });
export const updateCourse = (id: string, body: CourseInput) =>
  request<Course>('PATCH', `/api/courses/${encodeURIComponent(id)}`, { body });
export const deleteCourse = (id: string) => request<void>('DELETE', `/api/courses/${encodeURIComponent(id)}`);

/* ---------- Tasks ---------- */

export const listTasks = (filters: TaskFilters = {}) =>
  request<Task[]>('GET', '/api/tasks', {
    query: {
      courseId: filters.courseId,
      from: filters.from,
      to: filters.to,
      status: filters.status,
      overdue: filters.overdue ? true : undefined,
    },
  });
export const createTask = (body: CreateTaskRequest) => request<Task>('POST', '/api/tasks', { body });
export const getTask = (id: string) => request<Task>('GET', `/api/tasks/${encodeURIComponent(id)}`);
export const updateTask = (id: string, body: UpdateTaskRequest) =>
  request<Task>('PATCH', `/api/tasks/${encodeURIComponent(id)}`, { body });
export const setTaskStatus = (id: string, status: TaskStatus) =>
  request<Task>('PUT', `/api/tasks/${encodeURIComponent(id)}/status`, { body: { status } });
export const deleteTask = (id: string) => request<void>('DELETE', `/api/tasks/${encodeURIComponent(id)}`);

/* ---------- Weekly report ---------- */

/** start: YYYY-MM-DD (7 days from 00:00 UTC). Omit for the current week. */
export const getWeeklyReport = (start?: string) =>
  request<WeeklyReport>('GET', '/api/reports/weekly', { query: { start } });

/* ---------- Preferences ---------- */

export const getPreferences = () => request<PreferencesResponse>('GET', '/api/settings/preferences');
/** 409 → ApiError whose body is a PreferenceConflictBody. */
export const savePreferences = (body: Preferences) =>
  request<SavePreferencesResponse>('PUT', '/api/settings/preferences', { body });

/* ---------- AI ---------- */

/** Replaces this week's saved schedule. 422 → ApiError with message/details. */
export const generateSchedule = (day?: Day) =>
  request<GenerateResponse>('POST', '/api/agent/generate', day ? { body: { day } } : {});
export const chat = (message: string) => request<ChatResponse>('POST', '/api/agent/chat', { body: { message } });
export const listAgentRuns = () => request<AgentRun[]>('GET', '/api/agent/runs');

/* ---------- Schedule ---------- */

export const getSchedule = () => request<Schedule>('GET', '/api/schedule');
/** Returns the full updated block list. 422 → ApiError with details. */
export const moveBlock = (body: MoveBlockRequest) =>
  request<BlocksResponse>('PATCH', '/api/schedule/blocks', { body });
/** Removes one block (DELETE /api/schedule/blocks). Returns the full updated block list. */
export const removeBlock = (block: RemoveBlockRequest | Block) =>
  request<BlocksResponse>('DELETE', '/api/schedule/blocks', {
    body: { assignmentId: block.assignmentId, day: block.day, start: block.start, end: block.end },
  });
export const clearSchedule = () => request<void>('DELETE', '/api/schedule');

/**
 * URL of the .ics export. It needs the Authorization header, so a plain <a href> will 401;
 * use downloadScheduleIcs() for a user-facing download.
 */
export const scheduleExportUrl = () => '/api/schedule/export.ics';

/** Fetches the .ics with the auth header and triggers a browser download. */
export async function downloadScheduleIcs(filename = 'cramberry-schedule.ics'): Promise<void> {
  const token = getToken();
  const res = await fetch(scheduleExportUrl(), { headers: token ? { Authorization: `Bearer ${token}` } : {} });
  if (!res.ok) {
    let body: ErrorBody | null = null;
    try {
      body = (await res.json()) as ErrorBody;
    } catch {
      /* not JSON */
    }
    if (res.status === 401) {
      setToken(null);
      onUnauthorized();
    }
    throw new ApiError(res.status, body);
  }
  const url = URL.createObjectURL(await res.blob());
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/* ---------- Dashboard ---------- */

export const getDashboard = () => request<Dashboard>('GET', '/api/dashboard');

/* ---------- Notifications ---------- */

export const getNotificationSettings = () => request<NotificationSettings>('GET', '/api/settings/notifications');
export const saveNotificationSettings = (body: NotificationSettings) =>
  request<SaveNotificationSettingsResponse>('PUT', '/api/settings/notifications', { body });
export const listNotifications = (unreadOnly = false) =>
  request<AppNotification[]>('GET', '/api/notifications', { query: { unread: unreadOnly ? true : undefined } });
export const markNotificationRead = (id: string) =>
  request<void>('POST', `/api/notifications/${encodeURIComponent(id)}/read`);
export const markAllNotificationsRead = () => request<void>('POST', '/api/notifications/read-all');
