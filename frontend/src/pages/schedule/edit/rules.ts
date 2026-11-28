// Client-side preflight for moving/resizing a study block.
// Mirrors the server's checks (backend agent/Agent.cpp findPreferenceViolations) so a clash can be
// explained before anything is sent: inside one study-hours range, clear of personal time and other
// blocks (with the break between them), no longer than a session, within the daily maximum, and
// finished before the task is due. The server stays the authority; this only explains early.

import type { Block, Day, Preferences, Task, TimeRange } from '../../../api/types';
import { DAYS, dayIndex, formatRange24, toHM, toMinutes } from '../../../lib/time';

export const WEEK_MINUTES = 7 * 1440;

/** A block's place in the week, in minutes since local midnight. */
export interface Slot {
  day: Day;
  start: number;
  end: number;
}

export const slotOf = (r: TimeRange): Slot => ({ day: r.day, start: toMinutes(r.start), end: toMinutes(r.end) });
export const rangeOf = (s: Slot): TimeRange => ({ day: s.day, start: toHM(s.start), end: toHM(s.end) });
export const sameSlot = (a: Slot, b: Slot) => a.day === b.day && a.start === b.start && a.end === b.end;

/** Identity of a block for selection and deep links: "<assignmentId>|<Day>|<HH:MM>". */
export function blockKey(b: { assignmentId: string; day: Day; start: string | number }): string {
  const start = typeof b.start === 'number' ? toHM(b.start) : toHM(toMinutes(b.start));
  return `${b.assignmentId}|${b.day}|${start}`;
}

export function sameBlock(a: Block, b: Block): boolean {
  return a.assignmentId === b.assignmentId && a.day === b.day && a.start === b.start && a.end === b.end;
}

export interface RuleContext {
  prefs: Preferences;
  /** Every block currently on the schedule (the moving one included; it is skipped). */
  blocks: Block[];
  tasks: Map<string, Task>;
  weekStart: string;
  personalLabel: (r: TimeRange) => string;
}

const weekMinute = (day: Day, minute: number) => dayIndex(day) * 1440 + minute;

/** Minutes from the week's local Monday 00:00 to the task's due time; null when outside this week. */
function dueWeekMinute(task: Task, weekStart: string): number | null {
  const m = Math.floor((Date.parse(task.dueAt) - Date.parse(weekStart)) / 60_000);
  return m >= 0 && m <= WEEK_MINUTES ? m : null;
}

/** Human-readable reasons the slot breaks a preference ([] when it fits). */
export function checkPlacement(ctx: RuleContext, moving: Block, cand: Slot): string[] {
  const { prefs } = ctx;
  const problems: string[] = [];
  const len = cand.end - cand.start;
  const where = `${cand.day} ${formatRange24(cand.start, cand.end)}`;
  if (len <= 0) return ['That block has no length'];

  const others = ctx.blocks.filter((b) => !sameBlock(b, moving) && b.day === cand.day).map((b) => ({ b, s: slotOf(b) }));

  for (const { b, s } of others) {
    if (s.start < cand.end && cand.start < s.end) problems.push(`Overlaps ${b.title}, ${s.day} ${formatRange24(s.start, s.end)}`);
  }
  for (const p of prefs.personalTime) {
    const s = slotOf(p);
    if (s.day === cand.day && s.start < cand.end && cand.start < s.end) {
      problems.push(`Overlaps ${ctx.personalLabel(p)}, ${s.day} ${formatRange24(s.start, s.end)}`);
    }
  }
  const inside = prefs.studyHours.some((r) => {
    const s = slotOf(r);
    return s.day === cand.day && s.start <= cand.start && cand.end <= s.end;
  });
  if (!inside) problems.push(`Outside your study hours, ${where}`);

  if (prefs.breakMinutes > 0) {
    for (const { b, s } of others) {
      if (s.start < cand.end && cand.start < s.end) continue; // already reported as an overlap
      const gap = cand.start >= s.end ? cand.start - s.end : s.start - cand.end;
      if (gap < prefs.breakMinutes) {
        problems.push(`Less than a ${prefs.breakMinutes} min break from ${b.title}, ${s.day} ${formatRange24(s.start, s.end)}`);
      }
    }
  }
  if (len > prefs.sessionMinutes) problems.push(`Longer than your ${prefs.sessionMinutes} min study session`);

  const dayTotal = others.reduce((sum, { s }) => sum + (s.end - s.start), 0) + len;
  if (dayTotal > prefs.maxStudyMinutesPerDay) {
    problems.push(`Puts ${cand.day} at ${dayTotal} min of study (your max is ${prefs.maxStudyMinutesPerDay})`);
  }

  const task = ctx.tasks.get(moving.assignmentId);
  const due = task ? dueWeekMinute(task, ctx.weekStart) : null;
  if (task && due !== null && weekMinute(cand.day, cand.end) > due) problems.push(`Ends after ${task.title} is due`);

  return problems;
}

/** Minutes since this week's local Monday 00:00 for `now` (negative before the week starts). */
export function nowWeekMinute(weekStart: string, now: number = Date.now()): number {
  return Math.floor((now - Date.parse(weekStart)) / 60_000);
}

/**
 * The first slot at or after `cand` (never in the past, in 5-minute steps, wrapping to the start of
 * the week if nothing later fits) that passes every rule and isn't the block's current place. Keeps the block's length, capped at one session.
 */
export function findNextFree(ctx: RuleContext, moving: Block, cand: Slot, now: number = Date.now()): Slot | null {
  const len = Math.min(cand.end - cand.start, ctx.prefs.sessionMinutes);
  if (len <= 0) return null;
  const current = slotOf(moving);
  const earliest = Math.max(0, Math.ceil(nowWeekMinute(ctx.weekStart, now) / 5) * 5);
  const from = Math.max(earliest, Math.ceil(weekMinute(cand.day, cand.start) / 5) * 5);
  const tryAt = (t: number): Slot | null => {
    const day = DAYS[Math.floor(t / 1440)];
    const start = t % 1440;
    if (!day || start + len > 1440) return null;
    const s: Slot = { day, start, end: start + len };
    if (sameSlot(s, current)) return null; // where it already is doesn't count as a move
    return checkPlacement(ctx, moving, s).length === 0 ? s : null;
  };
  for (let t = from; t + len <= WEEK_MINUTES; t += 5) {
    const s = tryAt(t);
    if (s) return s;
  }
  for (let t = earliest; t < from; t += 5) {
    const s = tryAt(t);
    if (s) return s;
  }
  return null;
}
