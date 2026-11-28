import { useCallback, useEffect, useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { Layout, PageHead } from '../components/Layout';
import { StudyTimer } from '../components/StudyTimer';
import { useAuth } from '../auth/AuthContext';
import * as api from '../api/client';
import type { Course, Dashboard, Task, TaskStatus, WeeklyReport } from '../api/types';
import { browserUtcOffsetMinutes, currentWeekStart, formatDuration, formatRange12, toMinutes } from '../lib/time';
import { Seg } from './schedule/view/controls';
import {
  blockLink,
  courseLabel,
  courseMap,
  errorMessage,
  formatDue,
  formatShortDate,
  localParts,
  relativeDay,
  STATUS_OPTIONS,
} from './schedule/view/shared';
import './Home.css';

const LONG_DAYS: Record<string, string> = {
  Mon: 'Monday', Tue: 'Tuesday', Wed: 'Wednesday', Thu: 'Thursday', Fri: 'Friday', Sat: 'Saturday', Sun: 'Sunday',
};
const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];

/**
 * The student's local Monday as "YYYY-MM-DD", for GET /api/reports/weekly?start=.
 * The API's window is 00:00 UTC on that date for 7 days, not the student's local midnight, so the
 * report's edges are off by the student's UTC offset (a few hours). That's close enough for a summary.
 */
function localMondayDate(offset: number): string {
  return new Date(Date.parse(currentWeekStart(offset)) + offset * 60_000).toISOString().slice(0, 10);
}

/** "Nov 23 to 29": the report's dates. weekStart is 00:00 UTC on the student's local Monday, so read it at offset 0. */
function reportRange(r: WeeklyReport): string {
  const a = formatShortDate(r.weekStart, 0);
  const b = formatShortDate(new Date(Date.parse(r.weekEnd) - 1).toISOString(), 0);
  return a.split(' ')[0] === b.split(' ')[0] ? `${a} to ${b.split(' ')[1]}` : `${a} to ${b}`;
}

function greeting(minutes: number): string {
  if (minutes < 12 * 60) return 'Good morning';
  if (minutes < 17 * 60) return 'Good afternoon';
  return 'Good evening';
}

export default function Home() {
  const { student } = useAuth();
  const [dash, setDash] = useState<Dashboard | null>(null);
  const [report, setReport] = useState<WeeklyReport | null>(null);
  const [courses, setCourses] = useState<Course[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState<string | null>(null); // task id being updated
  const [rowError, setRowError] = useState<{ id: string; msg: string } | null>(null);

  const studentOffset = student?.utcOffsetMinutes ?? browserUtcOffsetMinutes();
  const load = useCallback(async () => {
    setError(null);
    try {
      const [d, r, c] = await Promise.all([
        api.getDashboard(),
        api.getWeeklyReport(localMondayDate(studentOffset)),
        api.listCourses(),
      ]);
      setDash(d);
      setReport(r);
      setCourses(c);
    } catch (e) {
      setError(errorMessage(e));
    }
  }, [studentOffset]);

  useEffect(() => {
    void load();
  }, [load]);

  const offset = dash?.student.utcOffsetMinutes ?? student?.utcOffsetMinutes ?? browserUtcOffsetMinutes();
  const cmap = useMemo(() => courseMap(courses), [courses]);
  const now = localParts(new Date().toISOString(), offset);
  const name = (dash?.student.name ?? student?.name ?? '').split(' ')[0];

  async function setStatus(task: Task, status: TaskStatus) {
    setBusy(task.id);
    setRowError(null);
    try {
      await api.setTaskStatus(task.id, status);
      await load();
    } catch (e) {
      setRowError({ id: task.id, msg: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  }

  const meta = (t: Task) =>
    [t.courseId ? courseLabel(cmap.get(t.courseId)) : '', `due ${formatDue(t.dueAt, offset)}`].filter(Boolean).join(', ');

  const todayMinutes = (dash?.todaysBlocks ?? []).reduce((s, b) => s + toMinutes(b.end) - toMinutes(b.start), 0);

  return (
    <Layout>
      <PageHead
        crumb={['Home']}
        title={name ? `${greeting(now.minutes)}, ${name}` : 'Home'}
        subtitle={`${LONG_DAYS[now.day]}, ${MONTHS[now.month]} ${now.date}. Here's today's study plan and what's due next.`}
        actions={
          <>
            <Link className="btn vs-a" to="/planner/settings">Study settings</Link>
            <Link className="btn vs-a" to="/schedule/edit">Edit schedule</Link>
            <Link className="btn primary vs-a" to="/schedule/create">Create schedule</Link>
          </>
        }
      />
      <main className="hm-main">
        {error && (
          <div className="alert" role="alert">
            Couldn't load your dashboard: {error}
            <div className="actions">
              <button type="button" className="btn small" onClick={() => void load()}>Try again</button>
            </div>
          </div>
        )}
        {dash && !dash.hasPreferences && (
          <div className="alert hm-nudge" role="status">
            <div className="rowhead">
              <strong>Tell Cramberry when you can study.</strong>
              <span className="req hm-req-dark">UC-15</span>
            </div>
            It can't plan your week until you mark your study hours and personal time.
            <div className="actions">
              <Link className="btn small vs-a" to="/planner/settings">Open Study Planner settings</Link>
            </div>
          </div>
        )}
        {!dash && !error && <p className="muted">Loading…</p>}

        {dash && (
          <div className="hm-grid">
            <div className="stack">
              <section className="panel" aria-label="Today's study blocks">
                <div className="rowhead">
                  <h2>Today's study blocks</h2>
                  <span className="req">
                    {dash.todaysBlocks.length ? `${formatDuration(todayMinutes)} planned` : 'UC-05'}
                  </span>
                </div>
                {dash.todaysBlocks.length === 0 ? (
                  <p className="hm-empty">
                    Nothing planned for today.{' '}
                    <Link to="/schedule/create">Generate a schedule</Link> or{' '}
                    <Link to="/schedule">see the whole week</Link>.
                  </p>
                ) : (
                  <ul className="list hm-blocks">
                    {dash.todaysBlocks.map((b) => (
                      <li key={`${b.assignmentId}${b.start}`}>
                        <span className="hm-when">{formatRange12(b.start, b.end)}</span>
                        <span className="block study hm-chip">{b.title}</span>
                        <Link to={blockLink(b)}>Open in editor</Link>
                      </li>
                    ))}
                  </ul>
                )}
              </section>

              <section className="panel" aria-label="Upcoming tasks">
                <div className="rowhead">
                  <h2>Coming up</h2>
                  <span className="req">FR 23</span>
                </div>
                <p className="hint">Unfinished tasks due in the next 14 days, most important first.</p>
                {dash.upcoming.length === 0 ? (
                  <p className="hm-empty">
                    Nothing due in the next two weeks. <Link to="/schedule/create">Add a task</Link>.
                  </p>
                ) : (
                  <ul className="list hm-tasks">
                    {dash.upcoming.map((t) => (
                      <li key={t.id}>
                        <span className="hm-task">
                          <strong>{t.title}</strong>
                          <span className="muted">
                            {meta(t)} ({relativeDay(t.dueAt, offset)})
                          </span>
                          {rowError?.id === t.id && <span className="errtext">{rowError.msg}</span>}
                        </span>
                        <Seg
                          label={`Status of ${t.title}`}
                          options={STATUS_OPTIONS}
                          value={t.status}
                          disabled={busy === t.id}
                          onChange={(s) => void setStatus(t, s)}
                          className="hm-seg"
                        />
                      </li>
                    ))}
                  </ul>
                )}
              </section>

              {dash.overdue.length > 0 && (
                <section className="panel" aria-label="Overdue tasks">
                  <div className="rowhead">
                    <h2>Overdue</h2>
                    <span className="req">{dash.overdue.length} past due</span>
                  </div>
                  <ul className="list hm-tasks">
                    {dash.overdue.map((t) => (
                      <li key={t.id}>
                        <span className="hm-task">
                          <strong>{t.title}</strong>
                          <span className="muted">
                            {meta(t)} ({relativeDay(t.dueAt, offset)})
                          </span>
                          {rowError?.id === t.id && <span className="errtext">{rowError.msg}</span>}
                        </span>
                        <button
                          type="button"
                          className="btn small"
                          disabled={busy === t.id}
                          onClick={() => void setStatus(t, 'COMPLETED')}
                        >
                          {busy === t.id ? 'Saving…' : 'Mark done'}
                        </button>
                      </li>
                    ))}
                  </ul>
                </section>
              )}
            </div>

            <div className="stack">
              <section className="panel" aria-label="Task counts">
                <div className="rowhead">
                  <h2>Your tasks</h2>
                  <span className="req">this week</span>
                </div>
                <div className="hm-counts">
                  <div><b>{dash.counts.notStarted}</b><span>not started</span></div>
                  <div><b>{dash.counts.inProgress}</b><span>in progress</span></div>
                  <div><b>{dash.counts.completedThisWeek}</b><span>done this week</span></div>
                </div>
              </section>

              {report && (
                <section className="panel" aria-label="Weekly report">
                  <div className="rowhead">
                    <h2>Weekly report</h2>
                    <span className="req">
                      {reportRange(report)}
                    </span>
                  </div>
                  <ul className="list">
                    <li>Completed <strong>{report.completed.length}</strong></li>
                    <li>Still due this week <strong>{report.remaining.length}</strong></li>
                    <li>Overdue this week <strong>{report.overdue.length}</strong></li>
                    <li>
                      Average time to finish
                      <strong>
                        {report.averageCompletionMinutes == null
                          ? 'n/a'
                          : report.averageCompletionMinutes < 1
                            ? 'under 1 min'
                            : formatDuration(Math.round(report.averageCompletionMinutes))}
                      </strong>
                    </li>
                  </ul>
                  {report.completed.length > 0 && (
                    <div className="why hm-done">
                      Finished: {report.completed.slice(0, 5).map((t) => t.title).join(', ')}
                      {report.completed.length > 5 ? ` and ${report.completed.length - 5} more` : ''}.
                    </div>
                  )}
                </section>
              )}

              <StudyTimer />

              <section className="panel" aria-label="Quick actions">
                <h2>Quick actions</h2>
                <ul className="list">
                  <li>See the whole week <Link to="/schedule">View schedule</Link></li>
                  <li>Add courses and tasks <Link to="/schedule/create">Create schedule</Link></li>
                  <li>Pull deadlines from a syllabus <Link to="/schedule/create/upload">Upload syllabus</Link></li>
                  <li>Change study hours <Link to="/planner/settings">Study settings</Link></li>
                </ul>
              </section>
            </div>
          </div>
        )}
      </main>
    </Layout>
  );
}
