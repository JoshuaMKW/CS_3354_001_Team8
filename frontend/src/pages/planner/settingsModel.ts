// Data model for the Study Planner Settings page: weekly availability as minute spans per day,
// hour-cell painting that only touches the painted hour, and the device-only localStorage stores.

import type { Day, Preferences, TimeRange } from '../../api/types';
import { DAYS, toHM, toMinutes } from '../../lib/time';

/** Minutes since local midnight, [start, end). */
export interface Span {
  start: number;
  end: number;
  /** Personal time only: device-only label ("Gym"). */
  label?: string;
}

export interface DayPlan {
  study: Span[];
  personal: Span[];
}

export type Plan = Record<Day, DayPlan>;

export type Tool = 'study' | 'personal' | 'erase';

export type CellKind = { kind: 'study' } | { kind: 'personal'; label?: string } | null;

/* ---------- Span arithmetic ---------- */

const byStart = (a: Span, b: Span) => a.start - b.start || a.end - b.end;

/** Removes [a, b) from every span, keeping the parts outside it (and their labels). */
export function subtract(spans: Span[], a: number, b: number): Span[] {
  const out: Span[] = [];
  for (const s of spans) {
    if (s.end <= a || s.start >= b) {
      out.push(s);
      continue;
    }
    if (s.start < a) out.push({ ...s, end: a });
    if (s.end > b) out.push({ ...s, start: b });
  }
  return out;
}

/** Sorts and merges overlapping or touching spans that carry the same label. */
export function merge(spans: Span[]): Span[] {
  const out: Span[] = [];
  for (const s of [...spans].sort(byStart)) {
    const last = out[out.length - 1];
    if (last && s.start <= last.end && (last.label ?? '') === (s.label ?? '')) {
      last.end = Math.max(last.end, s.end);
    } else {
      out.push({ ...s });
    }
  }
  return out;
}

const overlaps = (s: Span, a: number, b: number) => s.start < b && s.end > a;

/** What a one-hour cell shows: personal time wins over study hours. */
export function cellKind(day: DayPlan, hour: number): CellKind {
  const a = hour * 60;
  const b = a + 60;
  const p = day.personal.find((s) => overlaps(s, a, b));
  if (p) return { kind: 'personal', label: p.label };
  if (day.study.some((s) => overlaps(s, a, b))) return { kind: 'study' };
  return null;
}

/** Paints one hour of one day. Everything outside [hour, hour+1) is left exactly as it was. */
export function paintHour(day: DayPlan, hour: number, tool: Tool): DayPlan {
  const a = hour * 60;
  const b = a + 60;
  if (tool === 'erase') return { study: subtract(day.study, a, b), personal: subtract(day.personal, a, b) };
  if (tool === 'study') {
    return { study: merge([...subtract(day.study, a, b), { start: a, end: b }]), personal: subtract(day.personal, a, b) };
  }
  // Personal: keep the label of personal time already in this hour, else extend a neighbour's.
  const label =
    day.personal.find((s) => overlaps(s, a, b))?.label ??
    day.personal.find((s) => s.end === a)?.label ??
    day.personal.find((s) => s.start === b)?.label;
  return {
    study: subtract(day.study, a, b),
    personal: merge([...subtract(day.personal, a, b), { start: a, end: b, label }]),
  };
}

/** Adds personal time over [a, b) on one day, clearing study hours underneath. */
export function addPersonal(day: DayPlan, a: number, b: number, label?: string): DayPlan {
  return {
    study: subtract(day.study, a, b),
    personal: merge([...subtract(day.personal, a, b), { start: a, end: b, label }]),
  };
}

export function emptyPlan(): Plan {
  return Object.fromEntries(DAYS.map((d) => [d, { study: [], personal: [] }])) as unknown as Plan;
}

/* ---------- API <-> plan ---------- */

export function labelKey(day: Day, start: number, end: number): string {
  return `${day}|${toHM(start)}|${toHM(end)}`;
}

/** Builds the plan from saved ranges verbatim (no merging, so unpainted days round-trip exactly). */
export function planFromPrefs(p: Preferences, labels: Record<string, string>): Plan {
  const plan = emptyPlan();
  const push = (r: TimeRange, into: 'study' | 'personal') => {
    if (!plan[r.day]) return;
    const start = toMinutes(r.start);
    const end = toMinutes(r.end);
    const span: Span = { start, end };
    if (into === 'personal') {
      const label = labels[labelKey(r.day, start, end)];
      if (label) span.label = label;
    }
    plan[r.day][into].push(span);
  };
  p.studyHours.forEach((r) => push(r, 'study'));
  p.personalTime.forEach((r) => push(r, 'personal'));
  for (const d of DAYS) {
    plan[d].study.sort(byStart);
    plan[d].personal.sort(byStart);
  }
  return plan;
}

export function rangesFromPlan(plan: Plan, kind: 'study' | 'personal'): TimeRange[] {
  return DAYS.flatMap((day) => plan[day][kind].map((s) => ({ day, start: toHM(s.start), end: toHM(s.end) })));
}

/** Every labeled personal range, in the `cramberry.personalLabels.<id>` format. */
export function labelsFromPlan(plan: Plan): Record<string, string> {
  const out: Record<string, string> = {};
  for (const day of DAYS) for (const s of plan[day].personal) if (s.label) out[labelKey(day, s.start, s.end)] = s.label;
  return out;
}

export function studyMinutes(plan: Plan): number {
  return DAYS.reduce((sum, d) => sum + plan[d].study.reduce((n, s) => n + (s.end - s.start), 0), 0);
}

/** Visible hour columns: 8 am to 10 pm, widened to cover any saved range. */
export function gridHours(plan: Plan): { first: number; last: number } {
  let first = 8;
  let last = 22;
  for (const d of DAYS) {
    for (const s of [...plan[d].study, ...plan[d].personal]) {
      first = Math.min(first, Math.floor(s.start / 60));
      last = Math.max(last, Math.ceil(s.end / 60));
    }
  }
  return { first: Math.max(0, first), last: Math.min(24, last) };
}

/* ---------- Personal-time list ---------- */

export interface PersonalGroup {
  key: string;
  label?: string;
  start: number;
  end: number;
  days: Day[];
}

/** Groups identical personal ranges across days: "Gym, Mon and Wed, 6 to 7 pm". */
export function groupPersonal(plan: Plan): PersonalGroup[] {
  const groups = new Map<string, PersonalGroup>();
  for (const day of DAYS) {
    for (const s of plan[day].personal) {
      const key = `${s.label ?? ''}|${s.start}|${s.end}`;
      const g = groups.get(key);
      if (g) g.days.push(day);
      else groups.set(key, { key, label: s.label, start: s.start, end: s.end, days: [day] });
    }
  }
  return [...groups.values()];
}

export function removeGroup(plan: Plan, g: PersonalGroup): Plan {
  const next = { ...plan };
  for (const day of g.days) {
    next[day] = {
      ...plan[day],
      personal: plan[day].personal.filter(
        (s) => !(s.start === g.start && s.end === g.end && (s.label ?? '') === (g.label ?? '')),
      ),
    };
  }
  return next;
}

/** "Mon", "Mon and Wed", "Mon, Wed and Fri". */
export function joinDays(days: string[]): string {
  if (days.length <= 1) return days.join('');
  return `${days.slice(0, -1).join(', ')} and ${days[days.length - 1]}`;
}

/* ---------- Device-only storage ---------- */

export const labelsStorageKey = (studentId: string) => `cramberry.personalLabels.${studentId}`;
export const extrasStorageKey = (studentId: string) => `cramberry.plannerExtras.${studentId}`;

/** Settings the backend has no field for. Kept on this device only; they do not change planning. */
export interface PlannerExtras {
  /** "Finish work this early": days before the due date. */
  finishEarlyDays: number;
  harderCoursesEarlier: boolean;
  googleCalendarBusy: boolean;
}

/** Defaults match the wireframe. */
export const DEFAULT_EXTRAS: PlannerExtras = { finishEarlyDays: 2, harderCoursesEarlier: true, googleCalendarBusy: true };

function readJson(key: string): unknown {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

function writeJson(key: string, value: unknown): void {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    /* storage unavailable: kept for this page only */
  }
}

export function readLabels(studentId: string): Record<string, string> {
  const v = readJson(labelsStorageKey(studentId));
  if (!v || typeof v !== 'object' || Array.isArray(v)) return {};
  const out: Record<string, string> = {};
  for (const [k, label] of Object.entries(v)) if (typeof label === 'string' && label.trim()) out[k] = label;
  return out;
}

export function writeLabels(studentId: string, labels: Record<string, string>): void {
  writeJson(labelsStorageKey(studentId), labels);
}

export function readExtras(studentId: string): PlannerExtras {
  const v = readJson(extrasStorageKey(studentId)) as Partial<PlannerExtras> | null;
  return {
    finishEarlyDays: typeof v?.finishEarlyDays === 'number' ? v.finishEarlyDays : DEFAULT_EXTRAS.finishEarlyDays,
    harderCoursesEarlier:
      typeof v?.harderCoursesEarlier === 'boolean' ? v.harderCoursesEarlier : DEFAULT_EXTRAS.harderCoursesEarlier,
    googleCalendarBusy:
      typeof v?.googleCalendarBusy === 'boolean' ? v.googleCalendarBusy : DEFAULT_EXTRAS.googleCalendarBusy,
  };
}

export function writeExtras(studentId: string, extras: PlannerExtras): void {
  writeJson(extrasStorageKey(studentId), extras);
}

/* ---------- Validation (mirrors PUT /api/settings/preferences) ---------- */

/** "15", "15 min", "15 minutes" → 15; "-5" → -5; anything else → NaN. */
export function parseMinutes(text: string): number {
  const m = /^\s*(-?\d+)\s*(m|min|mins|minutes?)?\s*$/i.exec(text);
  return m ? Number(m[1]) : NaN;
}

export interface Validation {
  breakError: string | null;
  maxError: string | null;
  summary: string | null;
}

export function validate(maxPerDay: number, sessionMinutes: number, breakMinutes: number): Validation {
  let breakError: string | null = null;
  if (Number.isNaN(breakMinutes)) breakError = 'Enter the break in minutes, like "15 min".';
  else if (breakMinutes < 0) breakError = "Break can't be negative.";
  else if (breakMinutes > sessionMinutes) breakError = `Break can't be longer than your ${sessionMinutes} min session.`;
  const maxError = maxPerDay < 1 || maxPerDay > 1440 ? 'Most study per day must be between 1 minute and 24 hours.' : null;
  const summary = breakError ? 'Fix the break length to save.' : maxError ? 'Fix most study per day to save.' : null;
  return { breakError, maxError, summary };
}
