import { useCallback, useEffect, useRef, useState, type CSSProperties, type FormEvent, type KeyboardEvent, type PointerEvent } from 'react';
import { Link } from 'react-router-dom';
import { ApiError, getPreferences, savePreferences } from '../../api/client';
import type { Day, PreferenceConflictBody, Preferences } from '../../api/types';
import { useAuth } from '../../auth/AuthContext';
import { Layout, PageHead } from '../../components/Layout';
import { DAYS, formatDuration, formatHourLabel, formatHourShort, formatRange12, toMinutes } from '../../lib/time';
import {
  addPersonal,
  cellKind,
  gridHours,
  groupPersonal,
  joinDays,
  labelsFromPlan,
  paintHour,
  parseMinutes,
  planFromPrefs,
  rangesFromPlan,
  readExtras,
  readLabels,
  removeGroup,
  studyMinutes,
  validate,
  writeExtras,
  writeLabels,
  type Plan,
  type PlannerExtras,
  type Tool,
} from './settingsModel';
import './StudySettings.css';

const DEVICE_ONLY = 'Saved on this device only. Cramberry does not use this when planning yet.';

const MAX_PER_DAY_OPTIONS = [1, 2, 3, 4, 5, 6, 7, 8].map((h) => h * 60);
const SESSION_OPTIONS = [30, 45, 60, 90, 120];
const FINISH_EARLY_OPTIONS: Array<[number, string]> = [
  [0, 'On the day it’s due'],
  [1, '1 day before it\'s due'],
  [2, '2 days before it\'s due'],
  [3, '3 days before it\'s due'],
  [7, '1 week before it\'s due'],
];

const TOOLS: Array<{ tool: Tool; label: string; className: string }> = [
  { tool: 'study', label: 'Study hours', className: 'tool swatch-study' },
  { tool: 'personal', label: 'Personal time', className: 'tool swatch-personal' },
  { tool: 'erase', label: 'Erase', className: 'tool' },
];

const DAY_NAMES: Record<Day, string> = {
  Mon: 'Monday', Tue: 'Tuesday', Wed: 'Wednesday', Thu: 'Thursday', Fri: 'Friday', Sat: 'Saturday', Sun: 'Sunday',
};

const hoursLabel = (minutes: number) => (minutes % 60 === 0 ? `${minutes / 60} ${minutes === 60 ? 'hour' : 'hours'}` : formatDuration(minutes));
const minLabel = (minutes: number) => `${minutes} min`;
/** Options list that always contains the saved value. */
const withValue = (options: number[], value: number) => (options.includes(value) ? options : [...options, value].sort((a, b) => a - b));

interface Baseline {
  plan: Plan;
  maxPerDay: number;
  sessionMinutes: number;
  breakText: string;
  extras: PlannerExtras;
}

type Status =
  | { kind: 'saved' }
  | { kind: 'error'; message: string }
  | { kind: 'conflict'; message: string; conflicts: PreferenceConflictBody['conflicts'] }
  | null;

function baselineFrom(prefs: Preferences, studentId: string): Baseline {
  return {
    plan: planFromPrefs(prefs, readLabels(studentId)),
    maxPerDay: prefs.maxStudyMinutesPerDay,
    sessionMinutes: prefs.sessionMinutes,
    breakText: minLabel(prefs.breakMinutes),
    extras: readExtras(studentId),
  };
}

export default function StudySettings() {
  const { student } = useAuth();
  const studentId = student?.id ?? '';

  const [loadError, setLoadError] = useState<string | null>(null);
  const [baseline, setBaseline] = useState<Baseline | null>(null);
  const [plan, setPlan] = useState<Plan | null>(null);
  const [maxPerDay, setMaxPerDay] = useState(180);
  const [sessionMinutes, setSessionMinutes] = useState(60);
  const [breakText, setBreakText] = useState('15 min');
  const [extras, setExtras] = useState<PlannerExtras>(() => readExtras(studentId));
  const [tool, setTool] = useState<Tool>('study');
  const [saving, setSaving] = useState(false);
  const [status, setStatus] = useState<Status>(null);
  const [adding, setAdding] = useState(false);

  const reset = useCallback((b: Baseline) => {
    setBaseline(b);
    setPlan(b.plan);
    setMaxPerDay(b.maxPerDay);
    setSessionMinutes(b.sessionMinutes);
    setBreakText(b.breakText);
    setExtras(b.extras);
  }, []);

  useEffect(() => {
    if (!studentId) return;
    let live = true;
    getPreferences()
      .then((r) => live && reset(baselineFrom(r.preferences, studentId)))
      .catch((e: unknown) => live && setLoadError(e instanceof Error ? e.message : 'Could not load your settings.'));
    return () => {
      live = false;
    };
  }, [studentId, reset]);

  const breakMinutes = parseMinutes(breakText);
  const v = validate(maxPerDay, sessionMinutes, breakMinutes);

  /** Any edit clears the last save/conflict message. */
  const edited = () => setStatus(null);

  const updatePlan = (fn: (p: Plan) => Plan) => {
    edited();
    setPlan((p) => (p ? fn(p) : p));
  };

  async function save() {
    if (!plan || v.summary || saving) return;
    setSaving(true);
    setStatus(null);
    const body: Preferences = {
      studyHours: rangesFromPlan(plan, 'study'),
      maxStudyMinutesPerDay: maxPerDay,
      sessionMinutes,
      breakMinutes,
      personalTime: rangesFromPlan(plan, 'personal'),
    };
    try {
      const r = await savePreferences(body);
      writeLabels(studentId, labelsFromPlan(plan));
      writeExtras(studentId, extras);
      reset(baselineFrom(r.preferences, studentId));
      setStatus({ kind: 'saved' });
    } catch (e) {
      if (e instanceof ApiError && e.status === 409) {
        const b = e.body as PreferenceConflictBody | null;
        setStatus({ kind: 'conflict', message: e.message, conflicts: b?.conflicts ?? [] });
      } else if (e instanceof ApiError && e.status === 400) {
        setStatus({ kind: 'error', message: [e.message, ...(e.details ?? [])].join(' ') });
      } else {
        setStatus({ kind: 'error', message: `Couldn't save: ${e instanceof Error ? e.message : 'unknown error'}` });
      }
    } finally {
      setSaving(false);
    }
  }

  function cancel() {
    if (baseline) reset({ ...baseline, extras: readExtras(studentId) });
    setStatus(null);
    setAdding(false);
  }

  const head = (
    <PageHead
      crumb={['Study Planner', 'Set Study Settings']}
      title="Study planner settings"
      subtitle="Cramberry follows these whenever it builds or adjusts your schedule."
    />
  );

  if (!plan) {
    return (
      <Layout>
        {head}
        <main>{loadError ? <p className="errtext" role="alert">{loadError}</p> : <p className="muted">Loading your settings…</p>}</main>
      </Layout>
    );
  }

  const total = studyMinutes(plan) / 60;
  const totalText = `${Number.isInteger(total) ? total : total.toFixed(1)} study ${total === 1 ? 'hour' : 'hours'} marked this week.`;
  const groups = groupPersonal(plan);

  return (
    <Layout>
      {head}
      <main className="settings">
        <section className="panel" aria-label="When you can study">
          <div className="rowhead"><h2>When can you study?</h2><span className="req">Use case 15</span></div>
          <div className="paint" role="toolbar" aria-label="Paint tool">
            <span>Paint the week with:</span>
            {TOOLS.map((t) => (
              <button
                key={t.tool}
                type="button"
                className={`${t.className}${tool === t.tool ? ' on' : ''}`}
                aria-pressed={tool === t.tool}
                onClick={() => setTool(t.tool)}
              >
                {t.label}
              </button>
            ))}
          </div>
          <AvailabilityGrid plan={plan} tool={tool} onPaint={(day, hour, t) => updatePlan((p) => ({ ...p, [day]: paintHour(p[day], hour, t) }))} />
          <p className="total">{totalText} Cramberry never schedules over personal time.</p>
          <Check
            checked={extras.googleCalendarBusy}
            onChange={(on) => { edited(); setExtras({ ...extras, googleCalendarBusy: on }); }}
            title={DEVICE_ONLY}
          >
            Treat my Google Calendar events as busy (optional)
          </Check>
        </section>

        <div className="stack">
          <section className="panel" aria-label="Sessions and breaks">
            <div className="rowhead"><h2>Sessions and breaks</h2><span className="req">FR 7</span></div>
            <div className="pair">
              <div className="field">
                <label htmlFor="ss-max">Most study per day</label>
                <select
                  id="ss-max"
                  className={`input${v.maxError ? ' bad' : ''}`}
                  value={maxPerDay}
                  aria-invalid={v.maxError ? true : undefined}
                  aria-describedby={v.maxError ? 'ss-max-err' : undefined}
                  onChange={(e) => { edited(); setMaxPerDay(Number(e.target.value)); }}
                >
                  {withValue(MAX_PER_DAY_OPTIONS, maxPerDay).map((m) => <option key={m} value={m}>{hoursLabel(m)}</option>)}
                </select>
              </div>
              <div className="field">
                <label htmlFor="ss-session">Session length</label>
                <select
                  id="ss-session"
                  className="input"
                  value={sessionMinutes}
                  onChange={(e) => { edited(); setSessionMinutes(Number(e.target.value)); }}
                >
                  {withValue(SESSION_OPTIONS, sessionMinutes).map((m) => <option key={m} value={m}>{minLabel(m)}</option>)}
                </select>
              </div>
            </div>
            {v.maxError && <p id="ss-max-err" className="errtext">{v.maxError}</p>}
            <div className="field">
              <label htmlFor="ss-break">Break after each session</label>
              <input
                id="ss-break"
                className={`input${v.breakError ? ' bad' : ''}`}
                inputMode="numeric"
                value={breakText}
                aria-invalid={v.breakError ? true : undefined}
                aria-describedby={v.breakError ? 'ss-break-err' : undefined}
                onChange={(e) => { edited(); setBreakText(e.target.value); }}
                onBlur={() => { if (!Number.isNaN(breakMinutes)) setBreakText(minLabel(breakMinutes)); }}
              />
              {v.breakError && <p id="ss-break-err" className="errtext">{v.breakError}</p>}
            </div>
          </section>

          <section className="panel" aria-label="Deadlines">
            <div className="rowhead"><h2>Deadlines</h2><span className="req">FR 7</span></div>
            <div className="field" title={DEVICE_ONLY}>
              <label htmlFor="ss-early">Finish work this early</label>
              <select
                id="ss-early"
                className="input"
                value={extras.finishEarlyDays}
                onChange={(e) => { edited(); setExtras({ ...extras, finishEarlyDays: Number(e.target.value) }); }}
              >
                {(FINISH_EARLY_OPTIONS.some(([d]) => d === extras.finishEarlyDays)
                  ? FINISH_EARLY_OPTIONS
                  : [...FINISH_EARLY_OPTIONS, [extras.finishEarlyDays, `${extras.finishEarlyDays} days before it's due`] as [number, string]]
                ).map(([d, text]) => <option key={d} value={d}>{text}</option>)}
              </select>
            </div>
            <Check
              checked={extras.harderCoursesEarlier}
              onChange={(on) => { edited(); setExtras({ ...extras, harderCoursesEarlier: on }); }}
              title={DEVICE_ONLY}
            >
              Put harder courses earlier in the day
            </Check>
          </section>

          <section className="panel" aria-label="Personal time">
            <div className="rowhead">
              <h2>Personal time</h2>
              <button type="button" className="btn small" aria-expanded={adding} onClick={() => setAdding((a) => !a)}>Add</button>
            </div>
            {groups.length > 0 ? (
              <ul className="list">
                {groups.map((g) => (
                  <li key={g.key}>
                    <span>{g.label ?? 'Personal time'}, {joinDays(g.days)}, {formatRange12(g.start, g.end)}</span>
                    <button
                      type="button"
                      className="linkish"
                      aria-label={`Remove ${g.label ?? 'personal time'}, ${joinDays(g.days)}, ${formatRange12(g.start, g.end)}`}
                      onClick={() => updatePlan((p) => removeGroup(p, g))}
                    >
                      Remove
                    </button>
                  </li>
                ))}
              </ul>
            ) : (
              !adding && <p className="hint">No personal time yet. Paint it on the grid or use Add.</p>
            )}
            {adding && (
              <AddPersonalForm
                onCancel={() => setAdding(false)}
                onAdd={(label, days, start, end) => {
                  updatePlan((p) => {
                    const next = { ...p };
                    for (const d of days) next[d] = addPersonal(p[d], start, end, label || undefined);
                    return next;
                  });
                  setAdding(false);
                }}
              />
            )}
          </section>
        </div>
      </main>

      {status?.kind === 'conflict' && (
        <div className="alert ss-conflict" role="alert">
          <strong>Nothing was saved.</strong> This personal time overlaps study blocks already on your schedule:
          <ul className="ss-conflicts">
            {status.conflicts.map((c, i) => (
              <li key={`${c.assignmentId}-${c.day}-${c.start}-${i}`}>
                {c.title}, {c.day}, {formatRange12(c.start, c.end)}
              </li>
            ))}
          </ul>
          {status.conflicts.length === 0 && <p>{status.message}</p>}
          <p>
            Move or remove those blocks on <Link className="linkish-a" to="/schedule/edit">Edit Schedule</Link>, or pick a
            different time.
          </p>
        </div>
      )}

      <div className="summary">
        {v.summary ? (
          <p className="errtext" role="alert">{v.summary}</p>
        ) : status?.kind === 'error' ? (
          <p className="errtext" role="alert">{status.message}</p>
        ) : status?.kind === 'saved' ? (
          <p className="ss-note" role="status">Saved. Cramberry is rebuilding your schedule.</p>
        ) : null}
        <button type="button" className="btn" onClick={cancel}>Cancel</button>
        <button type="button" className="btn primary" disabled={!!v.summary || saving} onClick={() => void save()}>
          {saving ? 'Saving…' : 'Save settings'}
        </button>
      </div>
    </Layout>
  );
}

/* ---------- Availability grid ---------- */

function AvailabilityGrid({ plan, tool, onPaint }: { plan: Plan; tool: Tool; onPaint: (day: Day, hour: number, tool: Tool) => void }) {
  const { first, last } = gridHours(plan);
  const hours = Array.from({ length: last - first }, (_, i) => first + i);
  const dragging = useRef(false);
  // Last cell painted in this drag. Pointer events skip cells when the mouse moves fast,
  // so each new cell also paints the cells on the straight line back to this one.
  const lastCell = useRef<{ row: number; hour: number } | null>(null);
  const [focus, setFocus] = useState<{ row: number; col: number }>({ row: 0, col: 0 });
  const refs = useRef(new Map<string, HTMLButtonElement>());

  useEffect(() => {
    const stop = () => { dragging.current = false; };
    window.addEventListener('pointerup', stop);
    window.addEventListener('pointercancel', stop);
    return () => {
      window.removeEventListener('pointerup', stop);
      window.removeEventListener('pointercancel', stop);
    };
  }, []);

  const col = Math.min(focus.col, hours.length - 1);

  function onPointerDown(e: PointerEvent<HTMLButtonElement>, day: Day, hour: number) {
    if (e.button !== 0) return;
    // Touch pointers are captured by the first cell; release so the drag reaches the others.
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId);
    dragging.current = true;
    lastCell.current = { row: DAYS.indexOf(day), hour };
    onPaint(day, hour, tool);
  }

  function onDragEnter(day: Day, hour: number) {
    if (!dragging.current) return;
    const to = { row: DAYS.indexOf(day), hour };
    const from = lastCell.current ?? to;
    const steps = Math.max(Math.abs(to.row - from.row), Math.abs(to.hour - from.hour), 1);
    for (let i = 1; i <= steps; i++) {
      const row = Math.round(from.row + ((to.row - from.row) * i) / steps);
      const h = Math.round(from.hour + ((to.hour - from.hour) * i) / steps);
      onPaint(DAYS[row], h, tool);
    }
    lastCell.current = to;
  }

  function onKeyDown(e: KeyboardEvent<HTMLButtonElement>, row: number, c: number) {
    const moves: Record<string, [number, number]> = {
      ArrowUp: [-1, 0], ArrowDown: [1, 0], ArrowLeft: [0, -1], ArrowRight: [0, 1],
    };
    let next: { row: number; col: number } | null = null;
    if (e.key in moves) {
      const [dr, dc] = moves[e.key];
      next = { row: Math.max(0, Math.min(6, row + dr)), col: Math.max(0, Math.min(hours.length - 1, c + dc)) };
    } else if (e.key === 'Home') next = { row, col: 0 };
    else if (e.key === 'End') next = { row, col: hours.length - 1 };
    if (!next) return;
    e.preventDefault();
    setFocus(next);
    refs.current.get(`${next.row}:${next.col}`)?.focus();
  }

  // base.css sizes the hour columns from --avail-hours (14 when unset).
  const style = hours.length !== 14 ? ({ '--avail-hours': hours.length } as CSSProperties) : undefined;

  return (
    <div
      className="avail ss-grid"
      style={style}
      role="group"
      aria-label={`Weekly grid of study hours and personal time. Arrow keys move, Enter or Space paints with the ${TOOLS.find((t) => t.tool === tool)!.label.toLowerCase()} tool.`}
    >
      <div className="h" style={{ borderLeft: 0 }} />
      {hours.map((h) => <div key={h} className="h" aria-hidden="true">{formatHourShort(h)}</div>)}
      {DAYS.map((day, row) => (
        <DayRow
          key={day}
          day={day}
          row={row}
          plan={plan}
          hours={hours}
          focusCol={focus.row === row ? col : -1}
          refs={refs.current}
          onPointerDown={onPointerDown}
          onPointerEnter={onDragEnter}
          onKeyDown={onKeyDown}
          onFocusCell={(c) => setFocus({ row, col: c })}
          onToggle={(d, h, current) => onPaint(d, h, current === tool ? 'erase' : tool)}
        />
      ))}
    </div>
  );
}

interface DayRowProps {
  day: Day;
  row: number;
  plan: Plan;
  hours: number[];
  focusCol: number;
  refs: Map<string, HTMLButtonElement>;
  onPointerDown: (e: PointerEvent<HTMLButtonElement>, day: Day, hour: number) => void;
  onPointerEnter: (day: Day, hour: number) => void;
  onKeyDown: (e: KeyboardEvent<HTMLButtonElement>, row: number, col: number) => void;
  onFocusCell: (col: number) => void;
  onToggle: (day: Day, hour: number, current: Tool | null) => void;
}

function DayRow({ day, row, plan, hours, focusCol, refs, onPointerDown, onPointerEnter, onKeyDown, onFocusCell, onToggle }: DayRowProps) {
  return (
    <>
      <div className="d">{day}</div>
      {hours.map((hour, c) => {
        const k = cellKind(plan[day], hour);
        const what = k?.kind === 'personal' ? (k.label ?? 'Personal time') : k ? 'Study hours' : 'Free';
        const short = k?.kind === 'personal' && k.label ? k.label.split(/\s+/)[0] : '';
        return (
          <button
            key={hour}
            type="button"
            ref={(el) => { if (el) refs.set(`${row}:${c}`, el); else refs.delete(`${row}:${c}`); }}
            className={`cell${k ? ` ${k.kind}` : ''}`}
            tabIndex={(focusCol === -1 ? row === 0 && c === 0 : focusCol === c) ? 0 : -1}
            aria-label={`${DAY_NAMES[day]} ${formatHourLabel(hour)} to ${formatHourLabel((hour + 1) % 24)}: ${what}`}
            title={k?.kind === 'personal' ? (k.label ?? 'Personal time') : undefined}
            onPointerDown={(e) => onPointerDown(e, day, hour)}
            onPointerEnter={() => onPointerEnter(day, hour)}
            onKeyDown={(e) => onKeyDown(e, row, c)}
            onFocus={() => onFocusCell(c)}
            onClick={(e) => {
              // Mouse and touch paint on pointerdown; a click with detail 0 comes from Enter/Space.
              if (e.detail === 0) onToggle(day, hour, k?.kind ?? null);
            }}
          >
            {short}
          </button>
        );
      })}
    </>
  );
}

/* ---------- Checkbox in the wireframe's .check/.box style ---------- */

function Check({ checked, onChange, title, children }: { checked: boolean; onChange: (on: boolean) => void; title?: string; children: string }) {
  return (
    <label className="check ss-check" title={title}>
      <input type="checkbox" className="sr-only" checked={checked} onChange={(e) => onChange(e.target.checked)} />
      <span className={`box${checked ? ' on' : ''}`} aria-hidden="true">{checked ? 'x' : ''}</span>
      <span>{children}</span>
    </label>
  );
}

/* ---------- Add personal time ---------- */

function AddPersonalForm({ onAdd, onCancel }: { onAdd: (label: string, days: Day[], start: number, end: number) => void; onCancel: () => void }) {
  const [label, setLabel] = useState('');
  const [days, setDays] = useState<Day[]>([]);
  const [start, setStart] = useState('18:00');
  const [end, setEnd] = useState('19:00');
  const [error, setError] = useState<string | null>(null);

  function submit(e: FormEvent) {
    e.preventDefault();
    if (days.length === 0) return setError('Pick at least one day.');
    if (!start || !end) return setError('Enter a start and end time.');
    const s = toMinutes(start);
    const t = end === '00:00' ? 1440 : toMinutes(end);
    if (t <= s) return setError('End time must be after the start.');
    onAdd(label.trim(), DAYS.filter((d) => days.includes(d)), s, t);
  }

  return (
    <form className="ss-add" onSubmit={submit} aria-label="Add personal time" noValidate>
      <div className="field">
        <label htmlFor="ss-add-label">Label</label>
        <input id="ss-add-label" className="input" value={label} placeholder="Gym" maxLength={40} onChange={(e) => setLabel(e.target.value)} autoFocus />
      </div>
      <fieldset className="ss-days">
        <legend>Days</legend>
        {DAYS.map((d) => (
          <Check
            key={d}
            checked={days.includes(d)}
            onChange={(on) => { setError(null); setDays(on ? [...days, d] : days.filter((x) => x !== d)); }}
          >
            {d}
          </Check>
        ))}
      </fieldset>
      <div className="pair">
        <div className="field">
          <label htmlFor="ss-add-start">Start</label>
          <input id="ss-add-start" type="time" step={900} className="input" value={start} onChange={(e) => { setError(null); setStart(e.target.value); }} />
        </div>
        <div className="field">
          <label htmlFor="ss-add-end">End</label>
          <input id="ss-add-end" type="time" step={900} className="input" value={end} onChange={(e) => { setError(null); setEnd(e.target.value); }} />
        </div>
      </div>
      {error && <p className="errtext" role="alert">{error}</p>}
      <div className="ss-addbuttons">
        <button type="button" className="linkish" onClick={onCancel}>Cancel</button>
        <button type="submit" className="btn small primary">Add personal time</button>
      </div>
    </form>
  );
}
