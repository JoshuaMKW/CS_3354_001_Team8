// Types for the Cramberry REST API. Mirrors backend/API.md; keep the two in sync.

/** 3-letter English day name. Weekly ranges are in the student's local time. */
export type Day = 'Mon' | 'Tue' | 'Wed' | 'Thu' | 'Fri' | 'Sat' | 'Sun';

/** 24-hour "HH:MM" local time ("24:00" means midnight at the end of the day). */
export type HM = string;

/** ISO 8601 UTC timestamp, e.g. "2026-12-01T23:59:00Z". */
export type IsoUtc = string;

export interface TimeRange {
  day: Day;
  start: HM;
  end: HM;
}

/* ---------- Health ---------- */

export interface Health {
  status: 'ok' | 'degraded';
  database: boolean;
  ai: boolean;
}

/* ---------- Accounts ---------- */

export interface Student {
  id: string;
  name: string;
  email: string;
  utcOffsetMinutes: number;
  createdAt: IsoUtc;
}

export interface RegisterRequest {
  name: string;
  email: string;
  password: string;
  utcOffsetMinutes?: number;
}

export interface LoginRequest {
  email: string;
  password: string;
}

export interface AuthResponse {
  token: string;
  expiresAt: IsoUtc;
  student: Student;
}

export interface UpdateMeRequest {
  name?: string;
  utcOffsetMinutes?: number;
}

/* ---------- Courses ---------- */

export interface Course {
  id: string;
  name: string;
  code: string;
  term: string;
}

export interface CourseInput {
  name?: string;
  code?: string;
  term?: string;
}

/* ---------- Tasks ---------- */

export type TaskType = 'assignment' | 'exam' | 'study' | 'other';
export type TaskStatus = 'NOT_STARTED' | 'IN_PROGRESS' | 'COMPLETED';
/** 1 high, 2 medium, 3 low. */
export type Priority = 1 | 2 | 3;

export interface Task {
  id: string;
  courseId: string | null;
  title: string;
  notes: string;
  type: TaskType;
  dueAt: IsoUtc;
  estimatedMinutes: number;
  priority: Priority;
  status: TaskStatus;
  completedAt: IsoUtc | null;
  createdAt: IsoUtc;
  /** Computed: not completed and dueAt is in the past. */
  overdue: boolean;
}

export interface CreateTaskRequest {
  title: string;
  /** "YYYY-MM-DD", "YYYY-MM-DDTHH:MM[:SS]" with optional Z. */
  dueAt: string;
  type?: TaskType;
  courseId?: string | null;
  estimatedMinutes?: number;
  priority?: Priority;
  status?: TaskStatus;
  notes?: string;
}

/** Any POST field; send courseId: null to unlink. */
export type UpdateTaskRequest = Partial<CreateTaskRequest>;

export interface TaskFilters {
  courseId?: string;
  /** YYYY-MM-DD: due at or after 00:00 UTC that day. */
  from?: string;
  /** YYYY-MM-DD: due before 00:00 UTC that day. */
  to?: string;
  status?: TaskStatus;
  overdue?: boolean;
}

/* ---------- Weekly report ---------- */

export interface WeeklyReport {
  weekStart: IsoUtc;
  weekEnd: IsoUtc;
  completed: Task[];
  overdue: Task[];
  remaining: Task[];
  averageCompletionMinutes: number | null;
}

/* ---------- Preferences ---------- */

export interface Preferences {
  studyHours: TimeRange[];
  maxStudyMinutesPerDay: number;
  sessionMinutes: number;
  breakMinutes: number;
  personalTime: TimeRange[];
}

export interface PreferencesResponse {
  saved: boolean;
  preferences: Preferences;
}

export interface SavePreferencesResponse {
  message: string;
  preferences: Preferences;
}

/** Body of a 409 from PUT /api/settings/preferences (available as ApiError.body). */
export interface PreferenceConflictBody {
  error: string;
  conflicts: Array<TimeRange & { title: string; assignmentId: string }>;
}

/* ---------- AI ---------- */

export interface PlannedBlock extends TimeRange {
  title: string;
  assignmentId: string;
}

export interface GenerateResponse {
  engine: string;
  schedule: PlannedBlock[];
  warnings: string[];
}

export interface ChatResponse {
  reply: string;
}

export interface AgentRun {
  id: string;
  createdAt: IsoUtc;
  engine: string;
  ok: boolean;
  blockCount: number;
  details: string[];
}

/* ---------- Schedule ---------- */

export interface Block extends TimeRange {
  title: string;
  assignmentId: string;
  startsAt: IsoUtc;
  endsAt: IsoUtc;
}

export interface Schedule {
  /** The student's local Monday 00:00, in UTC. */
  weekStart: IsoUtc;
  utcOffsetMinutes: number;
  blocks: Block[];
}

export interface MoveBlockRequest {
  assignmentId: string;
  from: TimeRange;
  to: TimeRange;
  title?: string;
}

export interface RemoveBlockRequest extends TimeRange {
  assignmentId: string;
}

export interface BlocksResponse {
  blocks: Block[];
}

/* ---------- Dashboard ---------- */

export interface Dashboard {
  student: Student;
  today: Day;
  hasPreferences: boolean;
  todaysBlocks: Block[];
  schedule: Block[];
  upcoming: Task[];
  overdue: Task[];
  counts: { notStarted: number; inProgress: number; completedThisWeek: number };
}

/* ---------- Notifications ---------- */

export interface NotificationSettings {
  enabled: boolean;
  email: boolean;
  desktop: boolean;
  sms: boolean;
  phoneNumber: string;
  deadlineReminders: boolean;
  deadlineLeadMinutes: number;
}

export interface SaveNotificationSettingsResponse {
  message: string;
  settings: NotificationSettings;
}

export type NotificationChannel = 'email' | 'desktop' | 'sms';

export interface AppNotification {
  id: string;
  channel: NotificationChannel;
  subject: string;
  body: string;
  sentAt: IsoUtc;
  read: boolean;
}

/* ---------- Errors ---------- */

export interface ErrorBody {
  error: string;
  details?: string[];
  [key: string]: unknown;
}
