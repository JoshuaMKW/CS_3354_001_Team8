import { forwardRef, useState, type FormEvent } from 'react';
import * as api from '../../../api/client';
import type { Course, CreateTaskRequest, Priority, Task, TaskType } from '../../../api/types';
import { formatDuration } from '../../../lib/time';
import { Seg } from '../view/controls';
import {
  courseLabel,
  courseMap,
  errorMessage,
  formatDue,
  fromLocalInputs,
  PRIORITY_OPTIONS,
  priorityLabel,
  STATUS_OPTIONS,
  toLocalInputs,
  TYPE_OPTIONS,
  typeLabel,
} from '../view/shared';

type Field = 'title' | 'course' | 'due' | 'est' | 'priority' | 'notes' | 'general';

interface FormState {
  title: string;
  courseId: string;
  type: TaskType;
  date: string;
  time: string;
  est: string;
  priority: Priority;
  notes: string;
}

const blank: FormState = { title: '', courseId: '', type: 'assignment', date: '', time: '23:59', est: '60', priority: 2, notes: '' };

/** Which field a server 400 message is about. */
function fieldFor(msg: string): Field {
  if (/title/i.test(msg)) return 'title';
  if (/due|date/i.test(msg)) return 'due';
  if (/estimat|minutes/i.test(msg)) return 'est';
  if (/course/i.test(msg)) return 'course';
  if (/priority/i.test(msg)) return 'priority';
  if (/notes/i.test(msg)) return 'notes';
  return 'general';
}

interface Props {
  tasks: Task[];
  courses: Course[];
  offset: number;
  onChanged(): Promise<void> | void;
}

/** Add / edit / delete tasks. Due date and time are entered in local time and sent as UTC. */
export const TasksPanel = forwardRef<HTMLInputElement, Props>(function TasksPanel({ tasks, courses, offset, onChanged }, titleRef) {
  const [form, setForm] = useState<FormState>(blank);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [errors, setErrors] = useState<Partial<Record<Field, string>>>({});
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState<string | null>(null);
  const [confirmDelete, setConfirmDelete] = useState<string | null>(null);
  const [rowError, setRowError] = useState<{ id: string; msg: string } | null>(null);
  const cmap = courseMap(courses);

  const set = <K extends keyof FormState>(k: K, v: FormState[K]) => setForm((f) => ({ ...f, [k]: v }));

  function startEdit(t: Task) {
    const { date, time } = toLocalInputs(t.dueAt, offset);
    setEditingId(t.id);
    setForm({
      title: t.title,
      courseId: t.courseId ?? '',
      type: t.type,
      date,
      time,
      est: String(t.estimatedMinutes),
      priority: t.priority,
      notes: t.notes,
    });
    setErrors({});
    setSaved(null);
    if (titleRef && typeof titleRef !== 'function') titleRef.current?.focus();
  }

  function reset() {
    setEditingId(null);
    setForm(blank);
    setErrors({});
  }

  async function submit(e: FormEvent) {
    e.preventDefault();
    setSaved(null);
    const errs: Partial<Record<Field, string>> = {};
    if (!form.title.trim()) errs.title = 'Give the task a title.';
    const dueAt = fromLocalInputs(form.date, form.time || '23:59', offset);
    if (!dueAt) errs.due = 'Pick a due date and time.';
    if (!/^-?\d+(\.\d+)?$/.test(form.est.trim())) errs.est = 'Enter the estimate in minutes, like 90.';
    setErrors(errs);
    if (Object.keys(errs).length || !dueAt) return;

    const body: CreateTaskRequest = {
      title: form.title.trim(),
      dueAt,
      type: form.type,
      courseId: form.courseId || null,
      estimatedMinutes: Number(form.est),
      priority: form.priority,
      notes: form.notes,
    };
    setSaving(true);
    try {
      if (editingId) await api.updateTask(editingId, body);
      else await api.createTask(body);
      setSaved(editingId ? `Saved "${body.title}".` : `Added "${body.title}". Cramberry will re-plan your week.`);
      reset();
      await onChanged();
    } catch (err) {
      const msg = errorMessage(err);
      setErrors({ [fieldFor(msg)]: msg });
    } finally {
      setSaving(false);
    }
  }

  async function remove(t: Task) {
    setRowError(null);
    try {
      await api.deleteTask(t.id);
      setConfirmDelete(null);
      if (editingId === t.id) reset();
      await onChanged();
    } catch (err) {
      setRowError({ id: t.id, msg: errorMessage(err) });
    }
  }

  const bad = (f: Field) => (errors[f] ? 'input bad' : 'input');
  const err = (f: Field, id: string) =>
    errors[f] ? <p id={id} className="errtext" role="alert">{errors[f]}</p> : null;

  return (
    <section className="panel" aria-label="Tasks">
      <div className="rowhead">
        <h2>{editingId ? 'Edit task' : 'Add a task'}</h2>
        <span className="req">FR 8</span>
      </div>
      <form className="cs-task-form" onSubmit={(e) => void submit(e)} noValidate>
        <div className="field">
          <label htmlFor="cs-t-title">Title</label>
          <input
            id="cs-t-title"
            ref={titleRef}
            className={bad('title')}
            value={form.title}
            placeholder="Homework 5"
            aria-invalid={!!errors.title}
            aria-describedby={errors.title ? 'cs-e-title' : undefined}
            onChange={(e) => set('title', e.target.value)}
          />
          {err('title', 'cs-e-title')}
        </div>
        <div className="pair">
          <div className="field">
            <label htmlFor="cs-t-course">Course</label>
            <select id="cs-t-course" className={bad('course')} value={form.courseId} onChange={(e) => set('courseId', e.target.value)}>
              <option value="">No course</option>
              {courses.map((c) => (
                <option key={c.id} value={c.id}>{courseLabel(c)}{c.code && c.name ? `, ${c.name}` : ''}</option>
              ))}
            </select>
            {err('course', 'cs-e-course')}
          </div>
          <div className="field">
            <label>Type</label>
            <Seg label="Type" options={TYPE_OPTIONS} value={form.type} onChange={(v) => set('type', v)} />
          </div>
        </div>
        <div className="pair">
          <div className="field">
            <label htmlFor="cs-t-date">Due date</label>
            <div className="cs-due">
              <input
                id="cs-t-date"
                type="date"
                className={bad('due')}
                value={form.date}
                aria-invalid={!!errors.due}
                onChange={(e) => set('date', e.target.value)}
              />
              <input
                id="cs-t-time"
                type="time"
                aria-label="Due time"
                className={bad('due')}
                value={form.time}
                onChange={(e) => set('time', e.target.value)}
              />
            </div>
            {err('due', 'cs-e-due')}
          </div>
          <div className="field">
            <label htmlFor="cs-t-est">Estimated time (minutes)</label>
            <input
              id="cs-t-est"
              inputMode="numeric"
              className={bad('est')}
              value={form.est}
              aria-invalid={!!errors.est}
              aria-describedby={errors.est ? 'cs-e-est' : undefined}
              onChange={(e) => set('est', e.target.value)}
            />
            {err('est', 'cs-e-est')}
          </div>
        </div>
        <div className="pair">
          <div className="field">
            <label>Priority</label>
            <Seg label="Priority" options={PRIORITY_OPTIONS} value={form.priority} onChange={(v) => set('priority', v)} />
            {err('priority', 'cs-e-priority')}
          </div>
          <div className="field">
            <label htmlFor="cs-t-notes">Notes</label>
            <input id="cs-t-notes" className={bad('notes')} value={form.notes} placeholder="Optional" onChange={(e) => set('notes', e.target.value)} />
            {err('notes', 'cs-e-notes')}
          </div>
        </div>
        <div className="footbuttons">
          <span className="cs-saved" role="status">
            {errors.general ? <span className="errtext">{errors.general}</span> : saved}
          </span>
          <span className="actions">
            {editingId && <button type="button" className="btn" onClick={reset}>Cancel</button>}
            <button type="submit" className="btn primary" disabled={saving}>
              {saving ? 'Saving…' : editingId ? 'Save changes' : 'Add task'}
            </button>
          </span>
        </div>
      </form>

      <div className="rowhead cs-list-head">
        <h2>Your tasks</h2>
        <span className="req">{tasks.length} total</span>
      </div>
      {tasks.length === 0 ? (
        <p className="hint">No tasks yet. Add one above, or pull them from a syllabus.</p>
      ) : (
        <ul className="list cs-tasks">
          {tasks.map((t) => (
            <li key={t.id} className={editingId === t.id ? 'cs-editing' : undefined}>
              <span className="cs-task-main">
                <strong>{t.title}</strong>
                <span className="muted">
                  {[
                    t.courseId ? courseLabel(cmap.get(t.courseId)) : '',
                    typeLabel(t.type),
                    `due ${formatDue(t.dueAt, offset)}`,
                    formatDuration(t.estimatedMinutes),
                    `${priorityLabel(t.priority)} priority`,
                    STATUS_OPTIONS.find((o) => o.value === t.status)?.label,
                  ]
                    .filter(Boolean)
                    .join(', ')}
                  {t.overdue && <b className="cs-late"> Overdue</b>}
                </span>
                {rowError?.id === t.id && <span className="errtext">{rowError.msg}</span>}
              </span>
              {confirmDelete === t.id ? (
                <span className="cs-row-actions">
                  <span className="cs-confirm">Delete?</span>
                  <button type="button" className="linkish" onClick={() => void remove(t)}>Yes, delete</button>
                  <button type="button" className="linkish" onClick={() => setConfirmDelete(null)}>Keep</button>
                </span>
              ) : (
                <span className="cs-row-actions">
                  <button type="button" className="linkish" onClick={() => startEdit(t)}>Edit</button>
                  <button type="button" className="linkish" onClick={() => setConfirmDelete(t.id)}>Delete</button>
                </span>
              )}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
});
