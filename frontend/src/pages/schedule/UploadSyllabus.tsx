import { useEffect, useState, type ChangeEvent } from 'react';
import { Link } from 'react-router-dom';
import { Layout, PageHead } from '../../components/Layout';
import { useAuth } from '../../auth/AuthContext';
import * as api from '../../api/client';
import type { Course, TaskType } from '../../api/types';
import { browserUtcOffsetMinutes } from '../../lib/time';
import { Check } from './view/controls';
import { courseLabel, errorMessage, fromLocalInputs, TYPE_OPTIONS } from './view/shared';
import { extractDeadlines } from './create/syllabus';
import './view/shared.css';
import './UploadSyllabus.css';

interface Row {
  key: number;
  on: boolean;
  title: string;
  type: TaskType;
  date: string;
  time: string;
  est: string;
  source: string;
  line: number | null;
  error?: string;
}

const NEW_COURSE = '__new__';
let nextKey = 1;

export default function UploadSyllabus() {
  const { student } = useAuth();
  const offset = student?.utcOffsetMinutes ?? browserUtcOffsetMinutes();
  const [courses, setCourses] = useState<Course[]>([]);
  const [courseId, setCourseId] = useState('');
  const [newCourse, setNewCourse] = useState({ code: '', name: '', term: '' });
  const [text, setText] = useState('');
  const [fileName, setFileName] = useState<string | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [rows, setRows] = useState<Row[] | null>(null);
  const [adding, setAdding] = useState(false);
  const [topError, setTopError] = useState<string | null>(null);
  const [done, setDone] = useState<{ count: number; course: string } | null>(null);

  useEffect(() => {
    api.listCourses().then(setCourses, (e) => setTopError(errorMessage(e)));
  }, []);

  function onFile(e: ChangeEvent<HTMLInputElement>) {
    const f = e.target.files?.[0];
    e.target.value = '';
    setFileError(null);
    if (!f) return;
    if (/\.(pdf|docx?|rtf|odt|pages)$/i.test(f.name)) {
      setFileError(`${f.name} is a PDF or Word file. Those aren't supported yet: copy the text out of it and paste it below.`);
      return;
    }
    if (f.size > 1_000_000) {
      setFileError('That file is over 1 MB. Paste just the schedule part of the syllabus instead.');
      return;
    }
    f.text().then(
      (t) => {
        setText(t);
        setFileName(f.name);
      },
      () => setFileError("Couldn't read that file."),
    );
  }

  function findDeadlines() {
    setDone(null);
    setTopError(null);
    const found = extractDeadlines(text);
    setRows(
      found.map((d) => ({
        key: nextKey++,
        on: true,
        title: d.title,
        type: d.type,
        date: d.date,
        time: d.time,
        est: String(d.estimatedMinutes),
        source: d.source,
        line: d.line,
      })),
    );
  }

  const update = (key: number, patch: Partial<Row>) =>
    setRows((rs) => (rs ? rs.map((r) => (r.key === key ? { ...r, ...patch, error: undefined } : r)) : rs));

  function addBlankRow() {
    setRows((rs) => [
      ...(rs ?? []),
      { key: nextKey++, on: true, title: '', type: 'assignment', date: '', time: '23:59', est: '60', source: '', line: null },
    ]);
  }

  const selected = rows?.filter((r) => r.on) ?? [];

  async function addTasks() {
    if (!rows) return;
    setTopError(null);
    setDone(null);
    // Check every selected row first.
    let invalid = false;
    const checked = rows.map((r) => {
      if (!r.on) return r;
      let error: string | undefined;
      if (!r.title.trim()) error = 'Needs a title.';
      else if (!fromLocalInputs(r.date, r.time || '23:59', offset)) error = 'Needs a due date.';
      else if (!/^\d+$/.test(r.est.trim())) error = 'Estimate must be whole minutes.';
      if (error) invalid = true;
      return { ...r, error };
    });
    setRows(checked);
    if (invalid) {
      setTopError('Fix the highlighted rows, or untick them.');
      return;
    }

    setAdding(true);
    try {
      let cid = courseId;
      let cname = courseLabel(courses.find((c) => c.id === courseId));
      if (courseId === NEW_COURSE) {
        if (!newCourse.code.trim() && !newCourse.name.trim()) {
          setTopError('Give the new course a code or a name.');
          return;
        }
        const c = await api.createCourse({
          code: newCourse.code.trim(),
          name: newCourse.name.trim(),
          term: newCourse.term.trim(),
        });
        setCourses((cs) => [...cs, c]);
        setCourseId(c.id);
        setNewCourse({ code: '', name: '', term: '' });
        cid = c.id;
        cname = courseLabel(c);
      }

      let added = 0;
      const remaining: Row[] = [];
      for (const r of checked) {
        if (!r.on) {
          remaining.push(r);
          continue;
        }
        try {
          await api.createTask({
            title: r.title.trim(),
            dueAt: fromLocalInputs(r.date, r.time || '23:59', offset)!,
            type: r.type,
            courseId: cid || null,
            estimatedMinutes: Number(r.est),
            notes: r.line != null ? `From syllabus line ${r.line}: ${r.source}`.slice(0, 4000) : '',
          });
          added++;
        } catch (e) {
          remaining.push({ ...r, error: errorMessage(e) });
        }
      }
      setRows(remaining);
      setDone({ count: added, course: cname });
      if (remaining.some((r) => r.error)) setTopError('Some rows were not added. Fix them and try again.');
    } catch (e) {
      setTopError(errorMessage(e));
    } finally {
      setAdding(false);
    }
  }

  return (
    <Layout>
      <PageHead
        crumb={['Schedule', 'Create Schedule', 'Upload Syllabus']}
        title="Upload syllabus"
        subtitle="Cramberry finds the deadlines in your syllabus. Check them, fix anything it got wrong, then add them as tasks."
        actions={<Link className="btn vs-a" to="/schedule/create">Back to Create schedule</Link>}
      />
      <main className="us-grid">
        <section className="panel" aria-label="Your syllabus">
          <div className="rowhead">
            <h2>Your syllabus</h2>
            <span className="req">step 1</span>
          </div>
          <div className="why us-note">
            Plain text only: a <b>.txt</b> or <b>.md</b> file, or paste the text. PDF and Word files aren't
            supported yet. Dates are found on this device; nothing is sent until you add the tasks.
          </div>

          <div className="field">
            <label htmlFor="us-course">Course</label>
            <select id="us-course" className="input" value={courseId} onChange={(e) => setCourseId(e.target.value)}>
              <option value="">No course</option>
              {courses.map((c) => (
                <option key={c.id} value={c.id}>{courseLabel(c)}{c.code && c.name ? `, ${c.name}` : ''}</option>
              ))}
              <option value={NEW_COURSE}>New course…</option>
            </select>
          </div>
          {courseId === NEW_COURSE && (
            <div className="us-newcourse">
              <div className="field">
                <label htmlFor="us-nc-code">Code</label>
                <input id="us-nc-code" className="input" placeholder="CS 3354" value={newCourse.code} onChange={(e) => setNewCourse({ ...newCourse, code: e.target.value })} />
              </div>
              <div className="field">
                <label htmlFor="us-nc-name">Name</label>
                <input id="us-nc-name" className="input" placeholder="Software Engineering" value={newCourse.name} onChange={(e) => setNewCourse({ ...newCourse, name: e.target.value })} />
              </div>
              <div className="field">
                <label htmlFor="us-nc-term">Term</label>
                <input id="us-nc-term" className="input" placeholder="Fall 2026" value={newCourse.term} onChange={(e) => setNewCourse({ ...newCourse, term: e.target.value })} />
              </div>
            </div>
          )}

          <div className="field">
            <span className="us-label">Syllabus file</span>
            <div className="us-file">
              <label className="btn small us-pick">
                Choose .txt or .md
                <input type="file" accept=".txt,.md,.markdown,text/plain,text/markdown" onChange={onFile} className="sr-only" />
              </label>
              <span className="muted">{fileName ?? 'No file chosen'}</span>
            </div>
            {fileError && <p className="errtext" role="alert">{fileError}</p>}
          </div>
          <div className="field">
            <label htmlFor="us-text">Or paste the text</label>
            <textarea
              id="us-text"
              className="input us-text"
              rows={12}
              value={text}
              placeholder={'Homework 5 due Dec 3\nMidterm exam: Thursday, Dec 10, 10 am\nProject report due 12/14 by 11:59 pm'}
              onChange={(e) => {
                setText(e.target.value);
                setFileName(null);
              }}
            />
          </div>
          <div className="footbuttons">
            <span className="hint">Looks for dates next to words like due, exam, quiz or project.</span>
            <button type="button" className="btn primary" disabled={!text.trim()} onClick={findDeadlines}>
              Find deadlines
            </button>
          </div>
        </section>

        <section className="panel" aria-label="Deadlines found">
          <div className="rowhead">
            <h2>Deadlines found</h2>
            <span className="req">step 2: check and fix</span>
          </div>
          {done && done.count > 0 && (
            <div className="alert us-done" role="status">
              Added {done.count} {done.count === 1 ? 'task' : 'tasks'}{done.course ? ` to ${done.course}` : ''}. Cramberry
              will re-plan your week.
              <div className="actions">
                <Link className="btn small primary vs-a" to="/schedule/create">Generate my schedule</Link>
                <Link className="btn small vs-a" to="/schedule">View schedule</Link>
              </div>
            </div>
          )}
          {rows == null ? (
            <p className="hint">Pick a file or paste your syllabus, then press Find deadlines.</p>
          ) : rows.length === 0 && !done ? (
            <p className="hint">
              No deadlines found. Each one needs a date (like Dec 3, 12/03 or 2026-12-03) on the same line as a word
              like due, exam or quiz. <button type="button" className="linkish" onClick={addBlankRow}>Add one by hand</button>
            </p>
          ) : (
            <>
              {rows.length > 0 && (
                <div className="us-row us-head" aria-hidden="true">
                  <span />
                  <span>Title</span>
                  <span>Type</span>
                  <span>Due date</span>
                  <span>Time</span>
                  <span>Minutes</span>
                </div>
              )}
              <ul className="us-rows">
                {rows.map((r) => (
                  <li key={r.key} className={`us-item${r.on ? '' : ' us-off'}`}>
                    <div className="us-row">
                      <Check checked={r.on} onChange={(on) => update(r.key, { on })} label={`Add ${r.title || 'this deadline'}`} />
                      <input className={`input${r.error && /title/i.test(r.error) ? ' bad' : ''}`} aria-label="Title" value={r.title} onChange={(e) => update(r.key, { title: e.target.value })} />
                      <select className="input" aria-label="Type" value={r.type} onChange={(e) => update(r.key, { type: e.target.value as TaskType })}>
                        {TYPE_OPTIONS.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
                      </select>
                      <input type="date" className={`input${r.error && /date|due/i.test(r.error) ? ' bad' : ''}`} aria-label="Due date" value={r.date} onChange={(e) => update(r.key, { date: e.target.value })} />
                      <input type="time" className="input" aria-label="Due time" value={r.time} onChange={(e) => update(r.key, { time: e.target.value })} />
                      <input className={`input${r.error && /estimat|minutes/i.test(r.error) ? ' bad' : ''}`} inputMode="numeric" aria-label="Estimated minutes" value={r.est} onChange={(e) => update(r.key, { est: e.target.value })} />
                    </div>
                    {r.line != null && <p className="us-source">Line {r.line}: {r.source}</p>}
                    {r.error && <p className="errtext">{r.error}</p>}
                  </li>
                ))}
              </ul>
              {rows.length > 0 && (
                <p className="us-more">
                  <button type="button" className="linkish" onClick={addBlankRow}>Add a deadline it missed</button>
                </p>
              )}
            </>
          )}
          {rows != null && rows.length > 0 && (
            <div className="summary">
              {topError ? <p className="errtext" role="alert">{topError}</p> : <span className="hint us-count">{selected.length} of {rows.length} selected</span>}
              <button type="button" className="btn primary" disabled={!selected.length || adding} onClick={() => void addTasks()}>
                {adding ? 'Adding…' : `Add ${selected.length} ${selected.length === 1 ? 'task' : 'tasks'}`}
              </button>
            </div>
          )}
          {rows != null && rows.length === 0 && topError && <p className="errtext" role="alert">{topError}</p>}
        </section>
      </main>
    </Layout>
  );
}
