import { useCallback, useEffect, useMemo, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { Layout, PageHead } from '../../components/Layout';
import { useAuth } from '../../auth/AuthContext';
import * as api from '../../api/client';
import type { Course, Schedule, Task, TimeRange } from '../../api/types';
import { browserUtcOffsetMinutes, formatDuration, formatWeekRange, localDay, toMinutes } from '../../lib/time';
import { WeekCalendar } from './view/WeekCalendar';
import {
  blockLink,
  courseLabel,
  courseMap,
  errorMessage,
  formatDue,
  loadPersonalLabels,
  relativeDay,
  STATUS_OPTIONS,
  typeLabel,
} from './view/shared';
import './ViewSchedule.css';

interface Data {
  schedule: Schedule;
  personal: TimeRange[];
  tasks: Task[];
  courses: Course[];
}

export default function ViewSchedule() {
  const { student } = useAuth();
  const navigate = useNavigate();
  const [data, setData] = useState<Data | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [exporting, setExporting] = useState(false);
  const [exportError, setExportError] = useState<string | null>(null);

  const load = useCallback(async () => {
    setError(null);
    try {
      const [schedule, prefs, tasks, courses] = await Promise.all([
        api.getSchedule(),
        api.getPreferences(),
        api.listTasks(),
        api.listCourses(),
      ]);
      setData({ schedule, personal: prefs.preferences.personalTime, tasks, courses });
    } catch (e) {
      setError(errorMessage(e));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const offset = data?.schedule.utcOffsetMinutes ?? student?.utcOffsetMinutes ?? browserUtcOffsetMinutes();
  const labels = useMemo(() => loadPersonalLabels(student?.id), [student?.id]);

  // The tasks behind this week's blocks, with how much time each one got.
  const behind = useMemo(() => {
    if (!data) return [];
    const byId = new Map(data.tasks.map((t) => [t.id, t]));
    const agg = new Map<string, { title: string; minutes: number; blocks: number; task?: Task }>();
    for (const b of data.schedule.blocks) {
      const a = agg.get(b.assignmentId) ?? { title: b.title, minutes: 0, blocks: 0, task: byId.get(b.assignmentId) };
      a.minutes += toMinutes(b.end) - toMinutes(b.start);
      a.blocks += 1;
      agg.set(b.assignmentId, a);
    }
    return [...agg.entries()]
      .map(([id, a]) => ({ id, ...a }))
      .sort((x, y) => (x.task?.dueAt ?? '9').localeCompare(y.task?.dueAt ?? '9'));
  }, [data]);

  const courses = useMemo(() => courseMap(data?.courses ?? []), [data]);

  async function exportIcs() {
    setExporting(true);
    setExportError(null);
    try {
      await api.downloadScheduleIcs();
    } catch (e) {
      setExportError(errorMessage(e));
    } finally {
      setExporting(false);
    }
  }

  const blocks = data?.schedule.blocks ?? [];
  const totalMinutes = blocks.reduce((s, b) => s + toMinutes(b.end) - toMinutes(b.start), 0);

  return (
    <Layout>
      <PageHead
        crumb={['Schedule', 'View Schedule']}
        title="Your schedule"
        subtitle="This week's study blocks at a glance. Click a block to change it."
        actions={
          <>
            <Link className="btn vs-a" to="/schedule/create">Create schedule</Link>
            <Link className="btn vs-a" to="/schedule/edit">Edit schedule</Link>
            <span className="req">FR 10</span>
            <button type="button" className="btn primary" onClick={() => void exportIcs()} disabled={exporting}>
              {exporting ? 'Exporting…' : 'Export .ics'}
            </button>
          </>
        }
      />
      <main className="vs-main">
        {error && (
          <div className="alert" role="alert">
            Couldn't load your schedule: {error}
            <div className="actions">
              <button type="button" className="btn small" onClick={() => void load()}>Try again</button>
            </div>
          </div>
        )}
        {exportError && (
          <p className="errtext vs-export-err" role="alert">Export failed: {exportError}</p>
        )}

        <section aria-label="Week view">
          <div className="weekbar">
            <strong>{data ? formatWeekRange(data.schedule.weekStart, offset) : 'This week'}</strong>
            {data && (
              <span className="vs-total">
                {blocks.length} {blocks.length === 1 ? 'block' : 'blocks'}, {formatDuration(totalMinutes)} of study
              </span>
            )}
            <div className="legend">
              <span><i style={{ background: 'var(--pink)' }} />Study block</span>
              <span><i className="swatch-personal" />Personal time</span>
            </div>
          </div>
          {data ? (
            <WeekCalendar
              blocks={blocks}
              personal={data.personal}
              labels={labels}
              weekStart={data.schedule.weekStart}
              utcOffsetMinutes={offset}
              today={localDay(offset)}
              onOpen={(b) => navigate(blockLink(b))}
            />
          ) : (
            !error && <p className="muted">Loading your week…</p>
          )}
          {data && blocks.length === 0 && (
            <div className="alert vs-empty">
              No study blocks this week yet. Add your tasks and let Cramberry plan them.
              <div className="actions">
                <Link className="btn small vs-a" to="/schedule/create">Create schedule</Link>
              </div>
            </div>
          )}
        </section>

        {data && behind.length > 0 && (
          <section className="panel vs-tasks" aria-label="Tasks on this schedule">
            <div className="rowhead">
              <h2>Tasks behind these blocks</h2>
              <span className="req">FR 23</span>
            </div>
            <ul className="list">
              {behind.map((r) => {
                const t = r.task;
                const course = t?.courseId ? courseLabel(courses.get(t.courseId)) : '';
                return (
                  <li key={r.id}>
                    <span className="vs-task-main">
                      <strong>{t?.title ?? r.title}</strong>
                      <span className="muted">
                        {[course, t ? typeLabel(t.type) : null].filter(Boolean).join(', ')}
                      </span>
                    </span>
                    <span className="vs-task-due">
                      {t ? (
                        <>
                          Due {formatDue(t.dueAt, offset)}{' '}
                          <span className={t.overdue ? 'vs-late' : 'muted'}>({relativeDay(t.dueAt, offset)})</span>
                        </>
                      ) : (
                        <span className="muted">Task not found</span>
                      )}
                    </span>
                    <span className="vs-task-time">
                      {formatDuration(r.minutes)} in {r.blocks} {r.blocks === 1 ? 'block' : 'blocks'}
                    </span>
                    <span className="vs-task-status">
                      {t ? STATUS_OPTIONS.find((o) => o.value === t.status)?.label : ''}
                    </span>
                  </li>
                );
              })}
            </ul>
          </section>
        )}
      </main>
    </Layout>
  );
}
