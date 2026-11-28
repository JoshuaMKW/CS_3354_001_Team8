// "Study block" side panel (FR 17) of the Edit Schedule wireframe.

import type { Course, Day, TaskStatus } from '../../../api/types';
import { formatDuration, formatTime12, type WeekDate } from '../../../lib/time';

export interface Draft {
  title: string;
  /** "" = no course. */
  courseId: string;
  day: Day;
  start: number;
  duration: number;
  status: TaskStatus;
  notes: string;
}

const STATUSES: Array<[TaskStatus, string]> = [
  ['NOT_STARTED', 'Not started'],
  ['IN_PROGRESS', 'In progress'],
  ['COMPLETED', 'Completed'],
];

export interface BlockPanelProps {
  draft: Draft;
  /** False when the block's task is missing: task fields are read-only then. */
  hasTask: boolean;
  courses: Course[];
  week: WeekDate[];
  sessionMinutes: number;
  why: string;
  rebalance: boolean;
  /** Task fields differ from the saved task (saving them re-plans the week). */
  taskDirty: boolean;
  busy: boolean;
  saving: boolean;
  errors: string[];
  notice: string | null;
  onChange(patch: Partial<Draft>): void;
  onRebalance(on: boolean): void;
  onRemove(): void;
  onDiscard(): void;
  onSave(): void;
}

function withCurrent(values: number[], current: number): number[] {
  return values.includes(current) ? values : [...values, current].sort((a, b) => a - b);
}

export function BlockPanel(p: BlockPanelProps) {
  const { draft } = p;
  const starts = withCurrent(Array.from({ length: 96 }, (_, i) => i * 15), draft.start).filter(
    (m) => m + 5 <= 1440,
  );
  const maxLen = Math.max(p.sessionMinutes, draft.duration, 15);
  const durations = withCurrent(
    Array.from({ length: Math.floor(maxLen / 15) }, (_, i) => (i + 1) * 15),
    draft.duration,
  );
  const courseKnown = !draft.courseId || p.courses.some((c) => c.id === draft.courseId);

  return (
    <>
      <div className="rowhead"><h2>Study block</h2></div>
      <div className="field">
        <label htmlFor="es-title">Title</label>
        <input id="es-title" className="input" value={draft.title} maxLength={200}
          onChange={(e) => p.onChange({ title: e.target.value })} />
      </div>
      <div className="pair">
        <div className="field">
          <label htmlFor="es-course">Course</label>
          <select id="es-course" className="input" value={draft.courseId} disabled={!p.hasTask}
            onChange={(e) => p.onChange({ courseId: e.target.value })}>
            {p.courses.map((c) => (
              <option key={c.id} value={c.id}>{c.code || c.name}</option>
            ))}
            {!courseKnown && <option value={draft.courseId}>Unknown course</option>}
            <option value="">No course</option>
          </select>
        </div>
        <div className="field">
          <label htmlFor="es-day">Day</label>
          <select id="es-day" className="input" value={draft.day}
            onChange={(e) => p.onChange({ day: e.target.value as Day })}>
            {p.week.map((w) => (
              <option key={w.day} value={w.day}>{w.label}</option>
            ))}
          </select>
        </div>
      </div>
      <div className="pair">
        <div className="field">
          <label htmlFor="es-start">Start</label>
          <select id="es-start" className="input" value={draft.start}
            onChange={(e) => {
              const start = Number(e.target.value);
              p.onChange({ start, duration: Math.min(draft.duration, 1440 - start) });
            }}>
            {starts.map((m) => (
              <option key={m} value={m}>{formatTime12(m)}</option>
            ))}
          </select>
        </div>
        <div className="field">
          <label htmlFor="es-length">Duration</label>
          <select id="es-length" className="input" value={draft.duration}
            onChange={(e) => p.onChange({ duration: Number(e.target.value) })}>
            {durations.filter((d) => draft.start + d <= 1440).map((d) => (
              <option key={d} value={d}>{formatDuration(d)}</option>
            ))}
          </select>
        </div>
      </div>
      <div className="field">
        <div className="rowhead"><label id="es-status-label">Status</label></div>
        <div className="seg" role="radiogroup" aria-labelledby="es-status-label">
          {STATUSES.map(([value, label]) => (
            <span
              key={value}
              role="radio"
              aria-checked={draft.status === value}
              aria-disabled={!p.hasTask}
              tabIndex={draft.status === value ? 0 : -1}
              className={`es-segbtn${draft.status === value ? ' on' : ''}`}
              onClick={() => p.hasTask && p.onChange({ status: value })}
              onKeyDown={(e) => {
                if (!p.hasTask) return;
                const i = STATUSES.findIndex(([v]) => v === draft.status);
                if (e.key === 'ArrowRight' || e.key === 'ArrowDown') {
                  e.preventDefault();
                  p.onChange({ status: STATUSES[(i + 1) % 3][0] });
                } else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') {
                  e.preventDefault();
                  p.onChange({ status: STATUSES[(i + 2) % 3][0] });
                } else if (e.key === ' ' || e.key === 'Enter') {
                  e.preventDefault();
                  p.onChange({ status: value });
                }
              }}
            >
              {label}
            </span>
          ))}
        </div>
      </div>
      <div className="why">
        <div className="rowhead"><span>Why this block?</span></div>
        {p.why}
      </div>
      <div className="field">
        <div className="rowhead"><label htmlFor="es-notes">Notes</label></div>
        <textarea id="es-notes" className="input" rows={2} value={draft.notes} maxLength={4000} disabled={!p.hasTask}
          onChange={(e) => p.onChange({ notes: e.target.value })} />
      </div>
      <div
        className="check es-check"
        role="checkbox"
        aria-checked={p.rebalance}
        tabIndex={0}
        onClick={() => p.onRebalance(!p.rebalance)}
        onKeyDown={(e) => {
          if (e.key === ' ' || e.key === 'Enter') {
            e.preventDefault();
            p.onRebalance(!p.rebalance);
          }
        }}
      >
        <span className={`box${p.rebalance ? ' on' : ''}`} aria-hidden="true">{p.rebalance ? 'x' : ''}</span>
        <span>Rebalance my other study blocks after saving</span>
      </div>
      {p.taskDirty && (
        <p className="hint">Marking a task completed, or undoing that, makes the planner re-plan your whole week.</p>
      )}
      {p.errors.length > 0 && (
        <div className="errtext" role="alert">
          {p.errors.map((e, i) => <div key={i}>{e}</div>)}
        </div>
      )}
      {p.notice && <p className="hint" role="status">{p.notice}</p>}
      <div className="footbuttons">
        <button type="button" className="linkish" onClick={p.onRemove} disabled={p.busy}>Remove block</button>
        <div className="actions">
          <button type="button" className="btn" onClick={p.onDiscard} disabled={p.busy}>Discard</button>
          <button type="button" className="btn primary" onClick={p.onSave} disabled={p.busy} aria-busy={p.saving}>
            {p.saving ? 'Saving…' : 'Save changes'}
          </button>
        </div>
      </div>
    </>
  );
}
