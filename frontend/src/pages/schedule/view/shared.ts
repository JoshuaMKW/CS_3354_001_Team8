// Helpers shared by Home, View Schedule, Create Schedule and Upload Syllabus.
// Due dates are shown and entered in the student's local time (their fixed utcOffsetMinutes),
// the same clock the schedule uses.

import type { Block, Course, Day, Priority, TaskStatus, TaskType, TimeRange } from '../../../api/types';
import { ApiError } from '../../../api/client';
import { DAYS, formatTime12 } from '../../../lib/time';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const DAY_MS = 86_400_000;

export interface LocalParts {
  year: number;
  month: number; // 0-11
  date: number;
  day: Day;
  minutes: number; // since local midnight
}

export function localParts(iso: string, offset: number): LocalParts {
  const d = new Date(Date.parse(iso) + offset * 60_000);
  return {
    year: d.getUTCFullYear(),
    month: d.getUTCMonth(),
    date: d.getUTCDate(),
    day: DAYS[(d.getUTCDay() + 6) % 7],
    minutes: d.getUTCHours() * 60 + d.getUTCMinutes(),
  };
}

/** "Thu, Dec 3, 11:59 pm" (adds the year when it isn't this year). */
export function formatDue(iso: string, offset: number, now: Date = new Date()): string {
  const p = localParts(iso, offset);
  const thisYear = localParts(now.toISOString(), offset).year;
  const year = p.year === thisYear ? '' : ` ${p.year}`;
  return `${p.day}, ${MONTHS[p.month]} ${p.date}${year}, ${formatTime12(p.minutes)}`;
}

/** "Dec 3" */
export function formatShortDate(iso: string, offset: number): string {
  const p = localParts(iso, offset);
  return `${MONTHS[p.month]} ${p.date}`;
}

/** "today", "tomorrow", "in 5 days", "yesterday", "3 days ago" by local calendar day. */
export function relativeDay(iso: string, offset: number, now: Date = new Date()): string {
  const a = localParts(iso, offset);
  const b = localParts(now.toISOString(), offset);
  const diff = Math.round((Date.UTC(a.year, a.month, a.date) - Date.UTC(b.year, b.month, b.date)) / DAY_MS);
  if (diff === 0) return 'today';
  if (diff === 1) return 'tomorrow';
  if (diff === -1) return 'yesterday';
  return diff > 0 ? `in ${diff} days` : `${-diff} days ago`;
}

/** UTC ISO → the "YYYY-MM-DD" and "HH:MM" a date/time input shows in local time. */
export function toLocalInputs(iso: string, offset: number): { date: string; time: string } {
  const d = new Date(Date.parse(iso) + offset * 60_000).toISOString();
  return { date: d.slice(0, 10), time: d.slice(11, 16) };
}

/** Local "YYYY-MM-DD" + "HH:MM" → "YYYY-MM-DDTHH:MM:00Z" (UTC). Null if either is malformed. */
export function fromLocalInputs(date: string, time: string, offset: number): string | null {
  const dm = /^(\d{4})-(\d{2})-(\d{2})$/.exec(date);
  const tm = /^(\d{1,2}):(\d{2})$/.exec(time);
  if (!dm || !tm) return null;
  const ms = Date.UTC(+dm[1], +dm[2] - 1, +dm[3], +tm[1], +tm[2]) - offset * 60_000;
  if (Number.isNaN(ms)) return null;
  return new Date(ms).toISOString().slice(0, 16) + ':00Z';
}

/** Deep link into the editor for one block: /schedule/edit?block=<assignmentId>|<Day>|<HH:MM>. */
export function blockLink(b: Pick<Block, 'assignmentId' | 'day' | 'start'>): string {
  return `/schedule/edit?block=${encodeURIComponent(`${b.assignmentId}|${b.day}|${b.start}`)}`;
}

/* ---------- Personal-time labels (written by Study Planner settings) ---------- */

export const personalKey = (r: TimeRange) => `${r.day}|${r.start}|${r.end}`;

export function loadPersonalLabels(studentId: string | undefined): Record<string, string> {
  if (!studentId) return {};
  try {
    const raw = localStorage.getItem(`cramberry.personalLabels.${studentId}`);
    const parsed: unknown = raw ? JSON.parse(raw) : null;
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return {};
    const out: Record<string, string> = {};
    for (const [k, v] of Object.entries(parsed)) if (typeof v === 'string' && v.trim()) out[k] = v.trim();
    return out;
  } catch {
    return {};
  }
}

/* ---------- Labels ---------- */

export const STATUS_OPTIONS: { value: TaskStatus; label: string }[] = [
  { value: 'NOT_STARTED', label: 'Not started' },
  { value: 'IN_PROGRESS', label: 'In progress' },
  { value: 'COMPLETED', label: 'Completed' },
];

export const TYPE_OPTIONS: { value: TaskType; label: string }[] = [
  { value: 'assignment', label: 'Assignment' },
  { value: 'exam', label: 'Exam' },
  { value: 'study', label: 'Study' },
  { value: 'other', label: 'Other' },
];

export const PRIORITY_OPTIONS: { value: Priority; label: string }[] = [
  { value: 1, label: 'High' },
  { value: 2, label: 'Medium' },
  { value: 3, label: 'Low' },
];

export const typeLabel = (t: TaskType) => TYPE_OPTIONS.find((o) => o.value === t)?.label ?? t;
export const priorityLabel = (p: Priority) => PRIORITY_OPTIONS.find((o) => o.value === p)?.label ?? String(p);

/** "CS 3354" (or the name when there's no code). */
export function courseLabel(c: Course | undefined): string {
  if (!c) return '';
  return c.code || c.name;
}

export function courseMap(courses: Course[]): Map<string, Course> {
  return new Map(courses.map((c) => [c.id, c]));
}

/** A readable message for any thrown error. */
export function errorMessage(e: unknown): string {
  if (e instanceof ApiError) return e.message;
  if (e instanceof Error) return e.message;
  return 'Something went wrong. Please try again.';
}
