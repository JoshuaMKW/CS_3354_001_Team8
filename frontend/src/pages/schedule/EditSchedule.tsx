import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import * as api from '../../api/client';
import { ApiError } from '../../api/client';
import type { Block, Course, PreferencesResponse, Schedule, Task, UpdateTaskRequest } from '../../api/types';
import { useAuth } from '../../auth/AuthContext';
import { Layout, PageHead } from '../../components/Layout';
import { formatRange12, formatWeekRange, toHM, toMinutes, weekDates } from '../../lib/time';
import { BlockPanel, type Draft } from './edit/BlockPanel';
import { personalLabelFor, readPersonalLabels, whyText } from './edit/format';
import {
  blockKey,
  checkPlacement,
  findNextFree,
  rangeOf,
  sameBlock,
  sameSlot,
  slotOf,
  type RuleContext,
  type Slot,
} from './edit/rules';
import { WeekCalendar, type CalBlock } from './edit/WeekCalendar';
import './EditSchedule.css';

interface Loaded {
  schedule: Schedule;
  prefs: PreferencesResponse;
  tasks: Task[];
  courses: Course[];
}

/** One applied block change that Undo can send back (removals can't be re-added through the API). */
interface UndoEntry {
  assignmentId: string;
  before: { slot: Slot; title: string };
  after: { slot: Slot; title: string };
}

/** How the background re-plan after a task change turned out. */
type ReplanOutcome =
  | { kind: 'done' }
  | { kind: 'failed'; details: string[] }
  | { kind: 'none'; reason: string | null }
  | { kind: 'timeout' };

type BusyKind = 'move' | 'save' | 'adjust' | 'remove' | 'undo';

type AlertState =
  | { kind: 'clash'; problems: string[]; block: Block; target: Slot; title?: string; fromSave: boolean }
  | { kind: 'info'; text: string; lines: string[] };

/** Longest wait for the background re-plan a task change starts (an AI attempt can take about
 *  10 s before the built-in planner takes over). */
const REPLAN_WAIT_MS = 20_000;
const POLL_MS = 400;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function errorLines(e: unknown): string[] {
  if (e instanceof ApiError) return [e.message, ...(e.details ?? [])];
  return [e instanceof Error ? e.message : 'Something went wrong. Please try again.'];
}

function draftFor(b: Block, task: Task | undefined): Draft {
  const s = slotOf(b);
  return {
    title: b.title,
    courseId: task?.courseId ?? '',
    day: s.day,
    start: s.start,
    duration: s.end - s.start,
    status: task?.status ?? 'NOT_STARTED',
    notes: task?.notes ?? '',
  };
}

const draftSlot = (d: Draft): Slot => ({ day: d.day, start: d.start, end: d.start + d.duration });

/** Parses the ?block=<assignmentId>|<Day>|<HH:MM> deep link into a block key. */
function keyFromParam(param: string | null): string | null {
  if (!param) return null;
  const parts = param.split('|');
  if (parts.length < 3) return null;
  const start = parts.pop() as string;
  const day = parts.pop() as Block['day'];
  try {
    return blockKey({ assignmentId: parts.join('|'), day, start: toMinutes(start) });
  } catch {
    return null;
  }
}

export default function EditSchedule() {
  const { student } = useAuth();
  const [searchParams, setSearchParams] = useSearchParams();
  const [data, setData] = useState<Loaded | null>(null);
  const [loadError, setLoadError] = useState<string[] | null>(null);
  const [selectedKey, setSelectedKey] = useState<string | null>(null);
  const [draft, setDraft] = useState<Draft | null>(null);
  const [rebalance, setRebalance] = useState(true);
  const [alert, setAlert] = useState<AlertState | null>(null);
  const [busy, setBusyState] = useState<BusyKind | null>(null);
  // Guards against re-entry: state updates land on the next render, so a second click on the same
  // tick would still see busy === null.
  const busyRef = useRef<BusyKind | null>(null);
  const setBusy = (k: BusyKind | null) => {
    busyRef.current = k;
    setBusyState(k);
  };
  const [errors, setErrors] = useState<string[]>([]);
  const [notice, setNotice] = useState<string | null>(null);
  const [undo, setUndo] = useState<UndoEntry[]>([]);
  const dataRef = useRef<Loaded | null>(null);
  dataRef.current = data;

  const labels = useMemo(() => readPersonalLabels(student?.id), [student?.id]);

  /* ---------- loading ---------- */

  const fetchAll = useCallback(async (): Promise<Loaded> => {
    const [schedule, prefs, tasks, courses] = await Promise.all([
      api.getSchedule(),
      api.getPreferences(),
      api.listTasks(),
      api.listCourses(),
    ]);
    return { schedule, prefs, tasks, courses };
  }, []);

  const load = useCallback(async (): Promise<Loaded> => {
    const next = await fetchAll();
    setData(next);
    return next;
  }, [fetchAll]);

  useEffect(() => {
    let cancelled = false;
    load()
      .then((d) => {
        if (cancelled) return;
        const key = keyFromParam(searchParams.get('block'));
        const b = key ? d.schedule.blocks.find((x) => blockKey(x) === key) : undefined;
        if (b) {
          setSelectedKey(key);
          setDraft(draftFor(b, d.tasks.find((t) => t.id === b.assignmentId)));
        }
      })
      .catch((e) => !cancelled && setLoadError(errorLines(e)));
    return () => {
      cancelled = true;
    };
    // Load once on mount; the deep link is only read then.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  /* ---------- derived ---------- */

  const blocks = data?.schedule.blocks ?? [];
  const taskMap = useMemo(() => new Map((data?.tasks ?? []).map((t) => [t.id, t])), [data?.tasks]);
  const selected = selectedKey ? blocks.find((b) => blockKey(b) === selectedKey) ?? null : null;
  const selectedTask = selected ? taskMap.get(selected.assignmentId) : undefined;

  const ruleCtx = useCallback(
    (d: Loaded = dataRef.current as Loaded): RuleContext => ({
      prefs: d.prefs.preferences,
      blocks: d.schedule.blocks,
      tasks: new Map(d.tasks.map((t) => [t.id, t])),
      weekStart: d.schedule.weekStart,
      personalLabel: (r) => personalLabelFor(labels, r),
    }),
    [labels],
  );

  /** Select a block (or nothing), resetting the panel to its saved values and syncing ?block=. */
  const select = useCallback(
    (key: string | null, d: Loaded | null = dataRef.current, keepNotice = false) => {
      const b = key && d ? d.schedule.blocks.find((x) => blockKey(x) === key) : undefined;
      setSelectedKey(b ? key : null);
      setDraft(b && d ? draftFor(b, d.tasks.find((t) => t.id === b.assignmentId)) : null);
      setErrors([]);
      if (!keepNotice) setNotice(null);
      setSearchParams(
        (prev) => {
          const next = new URLSearchParams(prev);
          if (b) next.set('block', `${b.assignmentId}|${b.day}|${toHM(toMinutes(b.start))}`);
          else next.delete('block');
          return next;
        },
        { replace: true },
      );
    },
    [setSearchParams],
  );

  const setBlocks = (next: Block[]) =>
    setData((d) => (d ? { ...d, schedule: { ...d.schedule, blocks: next } } : d));

  /* ---------- moving ---------- */

  /** PATCH one block; on success replace state from the response and record it for Undo. */
  async function applyMove(
    block: Block,
    to: Slot,
    title?: string,
    recordUndo = true,
    show = true,
  ): Promise<Block[] | null> {
    const newTitle = title && title !== block.title ? title : undefined;
    const res = await api.moveBlock({
      assignmentId: block.assignmentId,
      from: rangeOf(slotOf(block)),
      to: rangeOf(to),
      ...(newTitle ? { title: newTitle } : {}),
    });
    if (show) setBlocks(res.blocks);
    if (recordUndo) {
      setUndo((u) => [
        ...u,
        {
          assignmentId: block.assignmentId,
          before: { slot: slotOf(block), title: block.title },
          after: { slot: to, title: newTitle ?? block.title },
        },
      ]);
    }
    return res.blocks;
  }

  /** A drag/resize ended, or "Move to next free time"/"Keep it" was chosen. */
  async function commitMove(block: Block, to: Slot, force = false) {
    const d = dataRef.current;
    if (busyRef.current || !d || sameSlot(slotOf(block), to)) return;
    setErrors([]);
    setNotice(null);
    setDraft((dr) => (dr ? { ...dr, day: to.day, start: to.start, duration: to.end - to.start } : dr));
    if (!force) {
      const problems = checkPlacement(ruleCtx(d), block, to);
      if (problems.length) {
        setAlert({ kind: 'clash', problems, block, target: to, fromSave: false });
        return;
      }
    }
    setAlert(null);
    setBusy('move');
    try {
      const next = await applyMove(block, to, undefined);
      const key = blockKey({ assignmentId: block.assignmentId, day: to.day, start: to.start });
      setSelectedKey(key);
      setSearchParams(
        (prev) => {
          const p = new URLSearchParams(prev);
          p.set('block', `${block.assignmentId}|${to.day}|${toHM(to.start)}`);
          return p;
        },
        { replace: true },
      );
      // Keep any unsaved panel edits; only the time fields follow the move.
      const moved = next?.find((b) => blockKey(b) === key);
      if (moved) setDraft((dr) => (dr ? { ...dr, ...pickTime(draftFor(moved, undefined)) } : dr));
      setNotice(`Moved to ${to.day} ${formatRange12(to.start, to.end)}.`);
    } catch (e) {
      // Revert: the block never moved on the server.
      setDraft((dr) => (dr ? { ...dr, ...pickTime(draftFor(block, undefined)) } : dr));
      setAlert({ kind: 'info', text: "Couldn't move that block", lines: errorLines(e) });
    } finally {
      setBusy(null);
    }
  }

  function onCalendarCommit(key: string, slot: Slot) {
    const b = dataRef.current?.schedule.blocks.find((x) => blockKey(x) === key);
    if (b) void commitMove(b, slot);
  }

  function moveToNextFree() {
    if (alert?.kind !== 'clash') return;
    const free = findNextFree(ruleCtx(), alert.block, alert.target);
    if (!free) {
      setAlert({ ...alert, problems: [...alert.problems, 'No free time left this week fits your study preferences.'] });
      return;
    }
    if (alert.fromSave) {
      setDraft((dr) => (dr ? { ...dr, day: free.day, start: free.start, duration: free.end - free.start } : dr));
      setAlert(null);
      void save(true, free);
    } else {
      void commitMove(alert.block, free, true);
    }
  }

  function keepIt() {
    if (alert?.kind !== 'clash') return;
    if (alert.fromSave) {
      setAlert(null);
      void save(true);
    } else {
      void commitMove(alert.block, alert.target, true);
    }
  }

  /* ---------- saving the panel ---------- */

  /**
   * Waits for the background re-plan a task change queued. The server answers for its ticket once
   * a re-plan that includes the change has finished, or says right away that it skipped it.
   */
  async function waitForReplan(ticket: number): Promise<ReplanOutcome> {
    const t0 = Date.now();
    while (Date.now() - t0 < REPLAN_WAIT_MS) {
      const status = await api.getReplan(ticket).catch(() => null);
      if (status?.state === 'done') return { kind: 'done' };
      if (status?.state === 'skipped') return { kind: 'none', reason: status.message ?? null };
      if (status?.state === 'failed') {
        return { kind: 'failed', details: [status.message ?? '', ...status.details].filter(Boolean) };
      }
      if (status?.state === 'unknown') return { kind: 'none', reason: null }; // e.g. the server restarted
      await sleep(POLL_MS);
    }
    return { kind: 'timeout' };
  }

  async function save(force = false, slotOverride?: Slot) {
    const d = dataRef.current;
    if (busyRef.current || !d || !selected || !draft) return;
    const block = selected;
    const task = taskMap.get(block.assignmentId);
    const target = slotOverride ?? draftSlot(draft);
    const timeChanged = !sameSlot(slotOf(block), target);
    const title = draft.title.trim();
    if (!title) {
      setErrors(['Give the block a title.']);
      return;
    }
    const titleChanged = title !== block.title;

    const taskPatch: UpdateTaskRequest = {};
    if (task) {
      if (draft.notes !== task.notes) taskPatch.notes = draft.notes;
      if ((draft.courseId || null) !== task.courseId) taskPatch.courseId = draft.courseId || null;
      if (draft.status !== task.status) taskPatch.status = draft.status;
    }
    const taskChanged = Object.keys(taskPatch).length > 0;

    if (!timeChanged && !titleChanged && !taskChanged && !rebalance) {
      setNotice('Nothing to save.');
      return;
    }
    if (timeChanged && !force) {
      const problems = checkPlacement(ruleCtx(d), block, target);
      if (problems.length) {
        setAlert({ kind: 'clash', problems, block, target, fromSave: true });
        return;
      }
    }

    setBusy('save');
    setErrors([]);
    setNotice(null);
    setAlert(null);
    const report: string[] = [];
    const hadManualEdits = undo.length > 0;
    let replan: ReplanOutcome | null = null;
    let rebalanced = false;
    let current = d;
    try {
      // 1. Task fields first. Marking the task completed (or undoing that) makes the server
      //    re-plan the week in the background, which replaces the saved blocks: wait for that
      //    re-plan before touching blocks. It is the rebalance, so no generate call follows it.
      //    Notes, course and Not started / In progress don't change the plan; no ticket comes back.
      if (task && taskChanged) {
        const onlyStatus = Object.keys(taskPatch).length === 1 && taskPatch.status;
        const { task: updated, replanTicket } = onlyStatus
          ? await api.setTaskStatusWithReplan(task.id, taskPatch.status!)
          : await api.updateTaskWithReplan(task.id, taskPatch);
        setData((x) => (x ? { ...x, tasks: x.tasks.map((t) => (t.id === updated.id ? updated : t)) } : x));
        replan = replanTicket === null ? null : await waitForReplan(replanTicket);
        if (replan?.kind === 'none') {
          const reason = replan.reason?.replace(/\.?$/, '.');
          report.push(reason ? `Your week was not re-planned: ${reason}` : 'Your week was not re-planned.');
          if (updated.status === 'COMPLETED') {
            report.push(`The study blocks for "${updated.title}" stay on your schedule until you remove them.`);
          }
        } else if (replan?.kind === 'failed') {
          report.push("The planner couldn't re-plan your week after this change, so your schedule was left as it was.", ...replan.details);
        } else if (replan?.kind === 'timeout') {
          report.push("The background re-plan hasn't finished yet. Refresh in a moment to see your new week.");
        }
      }
      // 2. Rebalance = an explicit re-plan, when no task change already started one: after the
      //    block moved, or when Save was pressed with nothing changed. A new title, notes or
      //    status don't move anything, so they don't rebalance.
      if (rebalance && !replan && (timeChanged || (!titleChanged && !taskChanged))) {
        try {
          const r = await api.generateSchedule();
          rebalanced = true;
          report.push(`Rebalanced by ${r.engine === 'local' ? 'the built-in planner' : r.engine}.`, ...r.warnings);
        } catch (e) {
          report.push(...errorLines(e).map((l, i) => (i === 0 ? `Couldn't rebalance: ${l}` : l)));
        }
      }
      const replanned = rebalanced || replan?.kind === 'done';
      // Not shown until the block step below is done, so the panel doesn't lose its block meanwhile.
      if (taskChanged || replanned) current = await fetchAll();
      if (replanned) setUndo([]);

      // 3. The block's own time and title go on top of whatever plan is now saved. After a
      //    rebalance the block keeps the student's time and title even if only others were to move.
      let finalKey: string | null = blockKey(block);
      let blockErrors: string[] = [];
      let blockKept = false;
      const pin = timeChanged || titleChanged || (replanned && rebalance);
      if (pin) {
        const to = timeChanged ? target : slotOf(block);
        const mine = current.schedule.blocks.filter((b) => b.assignmentId === block.assignmentId);
        const from = replanned
          ? mine.find((b) => sameSlot(slotOf(b), to)) ??
            mine.find((b) => sameBlock(b, block)) ??
            [...mine].sort((a, b) => score(a, to) - score(b, to))[0]
          : mine.find((b) => sameBlock(b, block));
        if (!from) {
          finalKey = null;
          const name = task?.title ?? block.title;
          if (timeChanged || titleChanged) {
            report.push(
              `The new plan has no block for "${name}"${draft.status === 'COMPLETED' ? ' because it is completed' : ''}, so your time and title changes were not applied.`,
            );
          } else if (draft.status === 'COMPLETED') {
            report.push(`"${name}" is completed, so its study blocks were taken off your schedule.`);
          } else {
            report.push(`The new plan has no block for "${name}".`);
          }
        } else if (sameSlot(slotOf(from), to) && from.title === title) {
          finalKey = blockKey(from);
          blockKept = true;
        } else {
          try {
            const next = await applyMove(from, to, title, true, false);
            if (next) current = { ...current, schedule: { ...current.schedule, blocks: next } };
            finalKey = blockKey({ assignmentId: from.assignmentId, day: to.day, start: to.start });
            blockKept = true;
          } catch (e) {
            finalKey = blockKey(from);
            const [first, ...rest] = errorLines(e);
            const where = `${to.day} ${formatRange12(to.start, to.end)}`;
            blockErrors = replanned
              ? [
                  `The new plan has no room for this block at ${where}, so it stays where the planner put it (${from.day} ${formatRange12(slotOf(from).start, slotOf(from).end)}).`,
                  ...(titleChanged ? ['Its new title was not saved either.'] : []),
                  first,
                  ...rest,
                ]
              : [`Couldn't save this block's ${timeChanged ? 'time' : 'title'}: ${first}`, ...rest];
          }
        }
      } else if (replanned) {
        const mine = current.schedule.blocks.filter((b) => b.assignmentId === block.assignmentId);
        const keep = mine.find((b) => sameBlock(b, block)) ?? [...mine].sort((a, b) => score(a, slotOf(block)) - score(b, slotOf(block)))[0];
        finalKey = keep ? blockKey(keep) : null;
        if (!keep) report.push(`The new plan has no block for "${task?.title ?? block.title}".`);
      }

      // `current` is what was just fetched/returned; dataRef only catches up on the next render.
      dataRef.current = current;
      setData(current);
      select(finalKey, current, true);
      setErrors(blockErrors); // select() clears errors; the block's own failure must still show
      const lost = replanned && hadManualEdits
        ? ['Earlier manual moves of other blocks were replaced by the new plan, so Undo was cleared.']
        : [];
      if (replanned) {
        const what = rebalanced
          ? 'your other study blocks were rebalanced'
          : 'changing the task made the planner re-plan your week';
        const lead = blockErrors.length
          ? `${taskChanged ? 'Your task changes were saved and ' : 'Saved: '}${what}, but this block couldn't keep the time you picked.`
          : blockKept && rebalance
            ? `Saved. ${capitalize(what)}, and this block kept your time and title.`
            : blockKept
              ? `Saved. ${capitalize(what)}, and your changes to this block were applied on top of the new plan.`
              : `Saved. ${capitalize(what)}.`;
        setAlert({ kind: 'info', text: lead, lines: [...report, ...lost] });
      } else if (blockErrors.length) {
        if (taskChanged) {
          setAlert({ kind: 'info', text: "Your task changes were saved, but this block's change wasn't.", lines: report });
        } else if (report.length) {
          setAlert({ kind: 'info', text: "This block's change wasn't saved.", lines: report });
        }
      } else if (report.length) {
        setAlert({ kind: 'info', text: 'Saved.', lines: report });
      } else {
        setNotice('Saved.');
      }
    } catch (e) {
      setErrors(errorLines(e));
    } finally {
      setBusy(null);
    }
  }

  function discard() {
    if (!selected) return;
    setDraft(draftFor(selected, selectedTask));
    setAlert(null);
    setErrors([]);
    setNotice(null);
  }

  async function remove() {
    if (busyRef.current || !selected) return;
    const b = selected;
    const ok = window.confirm(
      `Remove "${b.title}" (${b.day} ${formatRange12(b.start, b.end)}) from your schedule? This can't be undone.`,
    );
    if (!ok) return;
    setBusy('remove');
    setAlert(null);
    try {
      const res = await api.removeBlock(b);
      setBlocks(res.blocks);
      setUndo((u) => u.filter((x) => !(x.assignmentId === b.assignmentId && sameSlot(x.after.slot, slotOf(b)))));
      select(null, null);
      setNotice(null);
      setAlert({
        kind: 'info',
        text: `Removed "${b.title}".`,
        lines: ["Removing can't be undone. Ask AI to adjust plans that time again."],
      });
    } catch (e) {
      setErrors(errorLines(e));
    } finally {
      setBusy(null);
    }
  }

  /* ---------- header actions ---------- */

  async function doUndo() {
    if (busyRef.current) return;
    const entry = undo[undo.length - 1];
    const d = dataRef.current;
    if (!entry || !d) {
      setNotice('Nothing to undo yet.');
      return;
    }
    const block = d.schedule.blocks.find(
      (b) => b.assignmentId === entry.assignmentId && sameSlot(slotOf(b), entry.after.slot),
    );
    setUndo((u) => u.slice(0, -1));
    if (!block) {
      setAlert({ kind: 'info', text: "Couldn't undo", lines: ['That block is no longer on your schedule.'] });
      return;
    }
    setBusy('undo');
    setAlert(null);
    try {
      const res = await api.moveBlock({
        assignmentId: block.assignmentId,
        from: rangeOf(slotOf(block)),
        to: rangeOf(entry.before.slot),
        ...(entry.before.title !== block.title ? { title: entry.before.title } : {}),
      });
      const next = { ...d, schedule: { ...d.schedule, blocks: res.blocks } };
      setData(next);
      dataRef.current = next;
      const wasSelected = selectedKey === blockKey(block);
      if (wasSelected) select(blockKey({ ...block, day: entry.before.slot.day, start: entry.before.slot.start }), next, true);
      setNotice(`Undid the change to "${entry.before.title}".`);
    } catch (e) {
      setAlert({ kind: 'info', text: "Couldn't undo", lines: errorLines(e) });
    } finally {
      setBusy(null);
    }
  }

  async function askAi() {
    if (busyRef.current) return;
    setBusy('adjust');
    setAlert(null);
    setErrors([]);
    try {
      const r = await api.generateSchedule();
      const next = await load();
      setUndo([]);
      const keep = selectedKey && next.schedule.blocks.some((b) => blockKey(b) === selectedKey) ? selectedKey : null;
      select(keep, next, true);
      setAlert({
        kind: 'info',
        text: `Re-planned by ${r.engine === 'local' ? 'the built-in planner' : r.engine}: ${r.schedule.length} block${r.schedule.length === 1 ? '' : 's'}.`,
        lines: r.warnings,
      });
    } catch (e) {
      setAlert({ kind: 'info', text: "Couldn't adjust your schedule", lines: errorLines(e) });
    } finally {
      setBusy(null);
    }
  }

  /* ---------- render ---------- */

  const head = (
    <PageHead
      crumb={['Schedule', 'Edit Schedule']}
      title="Edit schedule"
      subtitle="Drag a block to move it, or pull its edges to change how long it runs."
      actions={
        <>
          <button
            type="button"
            className="btn"
            onClick={() => void doUndo()}
            aria-disabled={undo.length === 0 || busy !== null}
            title={undo.length ? 'Undo your last move or rename' : 'Nothing to undo yet. Removed blocks cannot be restored.'}
          >
            Undo
          </button>
          <button
            type="button"
            className="btn"
            onClick={() => void askAi()}
            disabled={busy !== null && busy !== 'adjust'}
            aria-busy={busy === 'adjust'}
            aria-disabled={busy === 'adjust'}
          >
            {busy === 'adjust' ? 'Adjusting…' : 'Ask AI to adjust'}
          </button>
        </>
      }
    />
  );

  if (loadError) {
    return (
      <Layout>
        {head}
        <main>
          <div className="panel es-empty" role="alert">
            <h2>Couldn't load your schedule</h2>
            {loadError.map((l, i) => <p key={i} className="errtext">{l}</p>)}
          </div>
        </main>
      </Layout>
    );
  }

  if (!data) {
    return (
      <Layout>
        {head}
        <main><p className="muted">Loading your schedule…</p></main>
      </Layout>
    );
  }

  const { schedule, prefs } = data;
  const alertBox = alert && (
    <div className="alert" role="alert">
      {alert.kind === 'clash' ? (
        <>
          <div className="rowhead"><span>{alert.problems[0]}</span></div>
          {alert.problems.length > 1 && (
            <ul className="es-alertlist">{alert.problems.slice(1).map((p, i) => <li key={i}>{p}</li>)}</ul>
          )}
          <div className="actions">
            <button type="button" className="btn small" onClick={moveToNextFree} disabled={busy !== null}>Move to next free time</button>
            <button type="button" className="btn small" onClick={keepIt} disabled={busy !== null}>Keep it</button>
          </div>
        </>
      ) : (
        <>
          <div className="rowhead">
            <span>{alert.text}</span>
            <button type="button" className="linkish es-dismiss" onClick={() => setAlert(null)}>Dismiss</button>
          </div>
          {alert.lines.length > 0 && (
            <ul className="es-alertlist">{alert.lines.map((p, i) => <li key={i}>{p}</li>)}</ul>
          )}
        </>
      )}
    </div>
  );

  if (!prefs.saved || schedule.blocks.length === 0) {
    return (
      <Layout>
        {head}
        <main>
          {alertBox}
          <div className="panel es-empty">
            <h2>Nothing to edit yet</h2>
            <p>
              {!prefs.saved
                ? 'Set your study hours first. Then create a schedule and its study blocks show up here to edit.'
                : 'There are no study blocks on your schedule this week. Create a schedule and the planner will place them.'}
            </p>
            <div className="actions es-emptyactions">
              <Link className="btn" to="/planner/settings">Study Planner settings</Link>
              <Link className="btn primary" to="/schedule/create">Create schedule</Link>
            </div>
          </div>
        </main>
      </Layout>
    );
  }

  const week = weekDates(schedule.weekStart, schedule.utcOffsetMinutes);
  const shown: Slot | null = selected && draft ? draftSlot(draft) : null;
  const personal = prefs.preferences.personalTime.map((r) => ({ r, s: slotOf(r) }));

  // Default 8 am to 8 pm like the wireframe, widened to whatever the data needs.
  const spans: Slot[] = [
    ...blocks.map(slotOf),
    ...prefs.preferences.studyHours.map(slotOf),
    ...personal.map((p) => p.s),
    ...(shown ? [shown] : []),
  ];
  const rangeStart = Math.min(8 * 60, ...spans.map((s) => Math.floor(s.start / 60) * 60));
  const rangeEnd = Math.min(1440, Math.max(20 * 60, ...spans.map((s) => Math.ceil(s.end / 60) * 60)));

  // Stable order (study blocks, then personal time): moving the selected block's DOM node would
  // drop its pointer capture mid-drag. .block.selected's z-index keeps it on top anyway.
  const calBlocks: CalBlock[] = [
    ...blocks.map((b): CalBlock => {
      const key = blockKey(b);
      return key === selectedKey && shown
        ? { key, title: draft?.title || b.title, slot: shown, kind: 'selected' }
        : { key, title: b.title, slot: slotOf(b), kind: 'study' };
    }),
    ...personal.map(({ r, s }): CalBlock => ({
      key: `personal|${r.day}|${r.start}|${r.end}`,
      title: personalLabelFor(labels, r),
      slot: s,
      kind: 'personal',
    })),
  ];

  // Only completing a task (or undoing that) changes the plan; the panel warns before that re-plan.
  const taskDirty =
    !!selectedTask && !!draft && (draft.status === 'COMPLETED') !== (selectedTask.status === 'COMPLETED');

  return (
    <Layout>
      {head}
      <main className="editor">
        <section aria-label="Week view">
          <div className="weekbar">
            <button type="button" className="btn small" aria-label="Previous week" disabled
              title="Only this week's schedule is saved, so only this week can be edited.">&lt;</button>
            <strong>{formatWeekRange(schedule.weekStart, schedule.utcOffsetMinutes)}</strong>
            <button type="button" className="btn small" aria-label="Next week" disabled
              title="Only this week's schedule is saved, so only this week can be edited.">&gt;</button>
            <div className="legend">
              <span><i style={{ background: 'var(--ink)' }}></i>Class</span>
              <span><i style={{ background: 'var(--pink)' }}></i>Study block</span>
              <span><i style={{ background: 'var(--hot)' }}></i>Selected</span>
              <span><i className="swatch-personal"></i>Personal time</span>
            </div>
          </div>
          <WeekCalendar
            blocks={calBlocks}
            rangeStart={rangeStart}
            rangeEnd={rangeEnd}
            locked={busy !== null}
            ariaLabel={selected ? 'Weekly schedule with one study block selected' : 'Weekly schedule'}
            onSelect={(key) => {
              if (key === selectedKey) return;
              setAlert(null);
              select(key);
            }}
            onCommit={onCalendarCommit}
          />
        </section>
        <aside className="panel" aria-label="Selected study block">
          {alertBox}
          {selected && draft ? (
            <BlockPanel
              draft={draft}
              hasTask={!!selectedTask}
              courses={data.courses}
              week={week}
              sessionMinutes={prefs.preferences.sessionMinutes}
              why={whyText(selectedTask, schedule.utcOffsetMinutes)}
              rebalance={rebalance}
              taskDirty={taskDirty}
              busy={busy !== null}
              saving={busy === 'save'}
              errors={errors}
              notice={notice}
              onChange={(patch) => setDraft((dr) => (dr ? { ...dr, ...patch } : dr))}
              onRebalance={setRebalance}
              onRemove={() => void remove()}
              onDiscard={discard}
              onSave={() => void save()}
            />
          ) : (
            <>
              <div className="rowhead"><h2>Study block</h2></div>
              <p className="hint">Select a study block on the calendar to change its time, title, status or notes.</p>
              {notice && <p className="hint" role="status">{notice}</p>}
              {errors.length > 0 && (
                <div className="errtext" role="alert">{errors.map((e, i) => <div key={i}>{e}</div>)}</div>
              )}
            </>
          )}
        </aside>
      </main>
    </Layout>
  );
}

function pickTime(d: Draft): Pick<Draft, 'day' | 'start' | 'duration'> {
  return { day: d.day, start: d.start, duration: d.duration };
}

/** How far a re-planned block is from a slot (same day first, then start time). */
function score(b: Block, s: Slot): number {
  return (b.day === s.day ? 0 : 10_000) + Math.abs(toMinutes(b.start) - s.start);
}

const capitalize = (t: string) => t.charAt(0).toUpperCase() + t.slice(1);
