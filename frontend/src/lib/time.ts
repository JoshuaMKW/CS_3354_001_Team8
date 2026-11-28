// Time helpers shared by the schedule and planner pages.
// Display style follows the wireframes: "10:30 am", "8 am", "6 to 7 pm", "Nov 9 to 15", "Thu, Nov 12".

import type { Day, HM, IsoUtc } from '../api/types';

export const DAYS: readonly Day[] = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const DAY_MS = 86_400_000;

/** 0 for Mon … 6 for Sun. */
export function dayIndex(day: Day): number {
  return DAYS.indexOf(day);
}

/* ---------- HH:MM <-> minutes ---------- */

/** "HH:MM" or "H:MM" → minutes since local midnight ("24:00" → 1440). */
export function toMinutes(hm: HM): number {
  const m = /^(\d{1,2}):(\d{2})$/.exec(hm.trim());
  if (!m) throw new Error(`Bad time "${hm}"`);
  return Number(m[1]) * 60 + Number(m[2]);
}

/** Minutes since midnight → "HH:MM" (1440 → "24:00"). */
export function toHM(minutes: number): HM {
  const h = Math.floor(minutes / 60);
  const m = minutes % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
}

/* ---------- 12-hour display ---------- */

function asMinutes(t: HM | number): number {
  return typeof t === 'number' ? t : toMinutes(t);
}

function split12(minutes: number): { h: number; m: number; ampm: 'am' | 'pm' } {
  const total = ((minutes % 1440) + 1440) % 1440;
  const h24 = Math.floor(total / 60);
  return { h: h24 % 12 === 0 ? 12 : h24 % 12, m: total % 60, ampm: h24 < 12 ? 'am' : 'pm' };
}

/** "10:30 am", "8 am", "12 pm". Accepts "HH:MM" or minutes. */
export function formatTime12(t: HM | number): string {
  const { h, m, ampm } = split12(asMinutes(t));
  return m ? `${h}:${String(m).padStart(2, '0')} ${ampm}` : `${h} ${ampm}`;
}

/** Calendar-row label for a whole hour (0–23): "8 am", "12 pm". */
export function formatHourLabel(hour24: number): string {
  return formatTime12(hour24 * 60);
}

/** Bare hour for compact column headers (0–23): 8 → "8", 13 → "1", 21 → "9". */
export function formatHourShort(hour24: number): string {
  return String(split12(hour24 * 60).h);
}

/** "6 to 7 pm", "10:30 am to 12 pm", "4 to 7 pm". */
export function formatRange12(start: HM | number, end: HM | number): string {
  const s = split12(asMinutes(start));
  const e = split12(asMinutes(end));
  const sText = s.m ? `${s.h}:${String(s.m).padStart(2, '0')}` : `${s.h}`;
  return s.ampm === e.ampm ? `${sText} to ${formatTime12(end)}` : `${formatTime12(start)} to ${formatTime12(end)}`;
}

/** "10:00 to 11:30" (24-hour, as in the wireframe's overlap alert). */
export function formatRange24(start: HM | number, end: HM | number): string {
  const f = (t: HM | number) => {
    const mins = asMinutes(t);
    return `${Math.floor(mins / 60)}:${String(mins % 60).padStart(2, '0')}`;
  };
  return `${f(start)} to ${f(end)}`;
}

/** 90 → "1 h 30 min", 120 → "2 h", 45 → "45 min". */
export function formatDuration(minutes: number): string {
  const h = Math.floor(minutes / 60);
  const m = minutes % 60;
  if (!h) return `${m} min`;
  return m ? `${h} h ${m} min` : `${h} h`;
}

/* ---------- Offsets and weeks ---------- */

/** The browser's current UTC offset in minutes (e.g. -360 for US Central Standard Time). */
export function browserUtcOffsetMinutes(): number {
  return -new Date().getTimezoneOffset();
}

export interface WeekDate {
  day: Day;
  /** 0 = Monday. */
  index: number;
  /** Calendar parts of the student's local date. */
  year: number;
  month: number; // 0-11
  date: number; // 1-31
  /** "Nov" */
  monthName: string;
  /** "Thu, Nov 12" */
  label: string;
}

/**
 * The 7 local dates (Mon..Sun) of a schedule week.
 * weekStart is the student's local Monday 00:00 expressed in UTC (GET /api/schedule).
 */
export function weekDates(weekStart: IsoUtc, utcOffsetMinutes: number): WeekDate[] {
  const localMonday = Date.parse(weekStart) + utcOffsetMinutes * 60_000;
  return DAYS.map((day, index) => {
    const d = new Date(localMonday + index * DAY_MS);
    const month = d.getUTCMonth();
    const date = d.getUTCDate();
    return {
      day,
      index,
      year: d.getUTCFullYear(),
      month,
      date,
      monthName: MONTHS[month],
      label: `${day}, ${MONTHS[month]} ${date}`,
    };
  });
}

/** "Thu, Nov 12" for a day of the given week. */
export function formatDayDate(weekStart: IsoUtc, utcOffsetMinutes: number, day: Day): string {
  return weekDates(weekStart, utcOffsetMinutes)[dayIndex(day)].label;
}

/** "Nov 9 to 15", or "Nov 30 to Dec 6" across a month boundary. */
export function formatWeekRange(weekStart: IsoUtc, utcOffsetMinutes: number): string {
  const days = weekDates(weekStart, utcOffsetMinutes);
  const a = days[0];
  const b = days[6];
  return a.month === b.month
    ? `${a.monthName} ${a.date} to ${b.date}`
    : `${a.monthName} ${a.date} to ${b.monthName} ${b.date}`;
}

/** Shift a weekStart by whole weeks (for < / > week navigation). */
export function addWeeks(weekStart: IsoUtc, weeks: number): IsoUtc {
  return new Date(Date.parse(weekStart) + weeks * 7 * DAY_MS).toISOString().replace('.000Z', 'Z');
}

/** The local Monday 00:00 (as a UTC ISO string) of the week containing `now`, for a fixed offset. */
export function currentWeekStart(utcOffsetMinutes: number, now: Date = new Date()): IsoUtc {
  const local = new Date(now.getTime() + utcOffsetMinutes * 60_000);
  const mondayIndex = (local.getUTCDay() + 6) % 7;
  const localMidnight = Date.UTC(local.getUTCFullYear(), local.getUTCMonth(), local.getUTCDate());
  const mondayLocal = localMidnight - mondayIndex * DAY_MS;
  return new Date(mondayLocal - utcOffsetMinutes * 60_000).toISOString().replace('.000Z', 'Z');
}

/** The student's local day name for a moment in time. */
export function localDay(utcOffsetMinutes: number, now: Date = new Date()): Day {
  const local = new Date(now.getTime() + utcOffsetMinutes * 60_000);
  return DAYS[(local.getUTCDay() + 6) % 7];
}
