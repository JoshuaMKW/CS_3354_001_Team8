// Small display helpers for the Edit Schedule page.

import type { Task, TimeRange } from '../../../api/types';
import { DAYS, formatTime12, toHM, toMinutes } from '../../../lib/time';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

/** "Fri, Nov 13 at 11:59 pm" in the student's local time. */
export function formatDue(dueAt: string, utcOffsetMinutes: number): string {
  const d = new Date(Date.parse(dueAt) + utcOffsetMinutes * 60_000);
  const day = DAYS[(d.getUTCDay() + 6) % 7];
  return `${day}, ${MONTHS[d.getUTCMonth()]} ${d.getUTCDate()} at ${formatTime12(d.getUTCHours() * 60 + d.getUTCMinutes())}`;
}

/** FR 15: why this block exists, using only what the task really says. */
export function whyText(task: Task | undefined, utcOffsetMinutes: number): string {
  if (!task) return "This block's task is no longer on your task list.";
  const due = formatDue(task.dueAt, utcOffsetMinutes);
  const lead = task.status === 'COMPLETED'
    ? `Marked completed. It was due ${due}.`
    : task.overdue
      ? `Overdue: it was due ${due}.`
      : `${task.type === 'exam' ? 'Exam due' : 'Due'} ${due}.`;
  return `${lead} You estimated ${task.estimatedMinutes} minutes for this task.`;
}

/** Labels the Settings page keeps for personal-time ranges ("Gym"); there is no backend field. */
export function readPersonalLabels(studentId: string | undefined): Record<string, string> {
  if (!studentId) return {};
  try {
    const raw = localStorage.getItem(`cramberry.personalLabels.${studentId}`);
    const v: unknown = raw ? JSON.parse(raw) : {};
    return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, string>) : {};
  } catch {
    return {};
  }
}

export function personalLabelFor(labels: Record<string, string>, r: TimeRange): string {
  const norm = (t: string) => {
    try {
      return toHM(toMinutes(t));
    } catch {
      return t;
    }
  };
  const label = labels[`${r.day}|${norm(r.start)}|${norm(r.end)}`];
  return typeof label === 'string' && label.trim() ? label.trim() : 'Personal time';
}
