import { useCallback, useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { Layout, PageHead } from '../../components/Layout';
import { useAuth } from '../../auth/AuthContext';
import * as api from '../../api/client';
import type { Course, GenerateResponse, Task } from '../../api/types';
import { browserUtcOffsetMinutes, DAYS, formatDuration, toMinutes } from '../../lib/time';
import { CoursesPanel } from './create/CoursesPanel';
import { TasksPanel } from './create/TasksPanel';
import { errorMessage } from './view/shared';
import './view/shared.css';
import './CreateSchedule.css';

interface GenFailure {
  message: string;
  details: string[];
  status: number;
}

function engineLabel(engine: string): string {
  return engine === 'local' ? 'the built-in planner' : engine;
}

export default function CreateSchedule() {
  const { student } = useAuth();
  const offset = student?.utcOffsetMinutes ?? browserUtcOffsetMinutes();
  const [courses, setCourses] = useState<Course[]>([]);
  const [tasks, setTasks] = useState<Task[]>([]);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [generating, setGenerating] = useState(false);
  const [result, setResult] = useState<GenerateResponse | null>(null);
  const [failure, setFailure] = useState<GenFailure | null>(null);
  const titleRef = useRef<HTMLInputElement>(null);

  const load = useCallback(async () => {
    try {
      const [c, t] = await Promise.all([api.listCourses(), api.listTasks()]);
      setCourses(c);
      setTasks(t);
      setLoadError(null);
    } catch (e) {
      setLoadError(errorMessage(e));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  async function generate() {
    setGenerating(true);
    setResult(null);
    setFailure(null);
    try {
      setResult(await api.generateSchedule());
    } catch (e) {
      setFailure({
        message: errorMessage(e),
        details: e instanceof api.ApiError ? e.details ?? [] : [],
        status: e instanceof api.ApiError ? e.status : 0,
      });
    } finally {
      setGenerating(false);
    }
  }

  const open = tasks.filter((t) => t.status !== 'COMPLETED').length;
  const planned = result?.schedule ?? [];
  const plannedMinutes = planned.reduce((s, b) => s + toMinutes(b.end) - toMinutes(b.start), 0);
  const plannedDays = DAYS.filter((d) => planned.some((b) => b.day === d));
  const needsSettings = failure && /preferences|study hours/i.test(failure.message);
  const needsTasks = failure && /no unfinished tasks|couldn't fit/i.test(failure.message);

  return (
    <Layout>
      <PageHead
        crumb={['Schedule', 'Create Schedule']}
        title="Create schedule"
        subtitle="Add your courses and tasks yourself, then let Cramberry plan the week around them."
        actions={<Link className="btn vs-a" to="/schedule">View schedule</Link>}
      />
      {loadError && (
        <div className="alert" role="alert">
          Couldn't load your courses and tasks: {loadError}
          <div className="actions">
            <button type="button" className="btn small" onClick={() => void load()}>Try again</button>
          </div>
        </div>
      )}
      <main className="cs-grid">
        <div className="stack">
          <p className="cs-part">Manual creation</p>
          <CoursesPanel courses={courses} onChanged={load} />
          <TasksPanel ref={titleRef} tasks={tasks} courses={courses} offset={offset} onChanged={load} />
        </div>

        <div className="stack">
          <p className="cs-part">Auto create</p>
          <section className="panel" aria-label="Generate my schedule">
            <div className="rowhead">
              <h2>Let Cramberry plan it</h2>
            </div>
            <p className="cs-copy">
              Plans every unfinished task due in the next 7 days into the study hours you have left this week.
              It replaces this week's blocks.
            </p>
            <p className="hint">
              {open} unfinished {open === 1 ? 'task' : 'tasks'}. This takes a few seconds.
            </p>
            <button
              type="button"
              className="btn primary cs-generate"
              onClick={() => void generate()}
              disabled={generating}
              aria-busy={generating}
            >
              {generating ? 'Planning your week…' : 'Generate my schedule'}
            </button>

            {result && (
              <div className="cs-result" role="status">
                <p className="cs-engine">Planned by {engineLabel(result.engine)}</p>
                <p>
                  {planned.length} study {planned.length === 1 ? 'block' : 'blocks'}, {formatDuration(plannedMinutes)}
                  {plannedDays.length ? ` across ${plannedDays.join(', ')}` : ''}.
                </p>
                {result.warnings.length > 0 && (
                  <div className="why">
                    <strong>Heads up</strong>
                    <ul className="cs-details">
                      {result.warnings.map((w, i) => <li key={i}>{w}</li>)}
                    </ul>
                  </div>
                )}
                <div className="actions cs-result-actions">
                  <Link className="btn small primary vs-a" to="/schedule">View schedule</Link>
                  <Link className="btn small vs-a" to="/schedule/edit">Edit schedule</Link>
                </div>
              </div>
            )}

            {failure && (
              <div className="alert cs-fail" role="alert">
                <strong>{failure.status === 422 ? "Couldn't plan your week." : 'Something went wrong.'}</strong>{' '}
                {failure.message}
                {failure.details.length > 0 && (
                  <ul className="cs-details">
                    {failure.details.map((d, i) => <li key={i}>{d}</li>)}
                  </ul>
                )}
                <div className="actions">
                  {needsSettings && <Link className="btn small vs-a" to="/planner/settings">Fix study settings</Link>}
                  {needsTasks && (
                    <button type="button" className="btn small" onClick={() => titleRef.current?.focus()}>
                      Add or change tasks
                    </button>
                  )}
                  {failure.status !== 422 && (
                    <button type="button" className="btn small" onClick={() => void generate()}>Try again</button>
                  )}
                </div>
              </div>
            )}
          </section>

          <section className="panel" aria-label="Upload syllabus">
            <div className="rowhead">
              <h2>Upload a syllabus</h2>
              <span className="req">.txt or .md</span>
            </div>
            <p className="cs-copy">Paste or pick your syllabus and Cramberry finds the due dates in it, so you don't type every deadline.</p>
            <div className="actions cs-result-actions">
              <Link className="btn vs-a" to="/schedule/create/upload">Upload syllabus</Link>
            </div>
          </section>

          <section className="panel" aria-label="Study settings">
            <div className="rowhead">
              <h2>Study hours</h2>
            </div>
            <p className="cs-copy">Cramberry only plans inside the hours you marked, and never over personal time.</p>
            <div className="actions cs-result-actions">
              <Link className="btn vs-a" to="/planner/settings">Study Planner settings</Link>
            </div>
          </section>
        </div>
      </main>
    </Layout>
  );
}
