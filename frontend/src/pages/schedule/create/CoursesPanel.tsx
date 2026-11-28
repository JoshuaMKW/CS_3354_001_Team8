import { useState, type FormEvent } from 'react';
import * as api from '../../../api/client';
import type { Course } from '../../../api/types';
import { errorMessage } from '../view/shared';

interface Props {
  courses: Course[];
  onChanged(): Promise<void> | void;
}

const empty = { code: '', name: '', term: '' };

/** Add / rename / delete courses (name, code, term). */
export function CoursesPanel({ courses, onChanged }: Props) {
  const [draft, setDraft] = useState(empty);
  const [addError, setAddError] = useState<string | null>(null);
  const [adding, setAdding] = useState(false);
  const [editing, setEditing] = useState<string | null>(null);
  const [edit, setEdit] = useState(empty);
  const [editError, setEditError] = useState<string | null>(null);
  const [confirmDelete, setConfirmDelete] = useState<string | null>(null);
  const [rowBusy, setRowBusy] = useState<string | null>(null);

  async function add(e: FormEvent) {
    e.preventDefault();
    setAddError(null);
    if (!draft.code.trim() && !draft.name.trim()) {
      setAddError('Give the course a code or a name.');
      return;
    }
    setAdding(true);
    try {
      await api.createCourse({ code: draft.code.trim(), name: draft.name.trim(), term: draft.term.trim() });
      setDraft(empty);
      await onChanged();
    } catch (err) {
      setAddError(errorMessage(err));
    } finally {
      setAdding(false);
    }
  }

  async function save(id: string) {
    setEditError(null);
    setRowBusy(id);
    try {
      await api.updateCourse(id, { code: edit.code.trim(), name: edit.name.trim(), term: edit.term.trim() });
      setEditing(null);
      await onChanged();
    } catch (err) {
      setEditError(errorMessage(err));
    } finally {
      setRowBusy(null);
    }
  }

  async function remove(id: string) {
    setRowBusy(id);
    try {
      await api.deleteCourse(id);
      setConfirmDelete(null);
      await onChanged();
    } catch (err) {
      setEditError(errorMessage(err));
    } finally {
      setRowBusy(null);
    }
  }

  return (
    <section className="panel" aria-label="Courses">
      <div className="rowhead">
        <h2>Courses</h2>
        <span className="req">UC-11</span>
      </div>
      {courses.length === 0 ? (
        <p className="hint">No courses yet. Add the ones you're taking so tasks can be grouped by course.</p>
      ) : (
        <ul className="list cs-courses">
          {courses.map((c) =>
            editing === c.id ? (
              <li key={c.id} className="cs-course-edit">
                <div className="cs-course-fields">
                  <input className="input" aria-label="Course code" value={edit.code} onChange={(e) => setEdit({ ...edit, code: e.target.value })} />
                  <input className="input" aria-label="Course name" value={edit.name} onChange={(e) => setEdit({ ...edit, name: e.target.value })} />
                  <input className="input" aria-label="Term" value={edit.term} onChange={(e) => setEdit({ ...edit, term: e.target.value })} />
                </div>
                <span className="cs-row-actions">
                  <button type="button" className="btn small" onClick={() => setEditing(null)}>Cancel</button>
                  <button type="button" className="btn small primary" disabled={rowBusy === c.id} onClick={() => void save(c.id)}>
                    Save
                  </button>
                </span>
                {editError && <p className="errtext cs-full">{editError}</p>}
              </li>
            ) : (
              <li key={c.id}>
                <span>
                  <strong className="cs-code">{c.code || c.name}</strong>
                  {c.code && c.name ? ` ${c.name}` : ''}
                  {c.term && <span className="muted">, {c.term}</span>}
                </span>
                {confirmDelete === c.id ? (
                  <span className="cs-row-actions">
                    <span className="cs-confirm">Delete? Its tasks are kept.</span>
                    <button type="button" className="linkish" disabled={rowBusy === c.id} onClick={() => void remove(c.id)}>Yes, delete</button>
                    <button type="button" className="linkish" onClick={() => setConfirmDelete(null)}>Keep</button>
                  </span>
                ) : (
                  <span className="cs-row-actions">
                    <button
                      type="button"
                      className="linkish"
                      onClick={() => {
                        setEditing(c.id);
                        setEdit({ code: c.code, name: c.name, term: c.term });
                        setEditError(null);
                      }}
                    >
                      Rename
                    </button>
                    <button type="button" className="linkish" onClick={() => setConfirmDelete(c.id)}>Delete</button>
                  </span>
                )}
              </li>
            ),
          )}
        </ul>
      )}
      <form className="cs-course-add" onSubmit={(e) => void add(e)} aria-label="Add a course">
        <div className="field">
          <label htmlFor="cs-c-code">Code</label>
          <input id="cs-c-code" className="input" placeholder="CS 3354" value={draft.code} onChange={(e) => setDraft({ ...draft, code: e.target.value })} />
        </div>
        <div className="field">
          <label htmlFor="cs-c-name">Name</label>
          <input id="cs-c-name" className="input" placeholder="Software Engineering" value={draft.name} onChange={(e) => setDraft({ ...draft, name: e.target.value })} />
        </div>
        <div className="field">
          <label htmlFor="cs-c-term">Term</label>
          <input id="cs-c-term" className="input" placeholder="Fall 2026" value={draft.term} onChange={(e) => setDraft({ ...draft, term: e.target.value })} />
        </div>
        <button type="submit" className="btn" disabled={adding}>{adding ? 'Adding…' : 'Add course'}</button>
      </form>
      {addError && <p className="errtext" role="alert">{addError}</p>}
    </section>
  );
}
