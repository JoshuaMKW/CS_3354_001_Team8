// The week grid of the Edit Schedule wireframe (.cal/.day/.hour/.slot/.block), with drag to move
// and handles to resize. Internally the grid uses 5-minute rows of 4px (6 rows = the wireframe's
// 24px half hour), so 30-minute-aligned data renders exactly like the wireframe while 5-minute
// positions stay exact; times off the 5-minute grid get a sub-row margin offset.

import { useEffect, useRef, useState, type CSSProperties, type KeyboardEvent, type PointerEvent } from 'react';
import { DAYS, formatHourLabel, formatRange12 } from '../../../lib/time';
import type { Slot } from './rules';

export interface CalBlock {
  key: string;
  title: string;
  slot: Slot;
  kind: 'study' | 'selected' | 'personal';
}

type Mode = 'move' | 'top' | 'bottom';

interface Drag {
  key: string;
  mode: Mode;
  x0: number;
  y0: number;
  orig: Slot;
  preview: Slot;
  moved: boolean;
}

const STEP = 5; // minutes per internal grid row
const ROW_PX = 4; // 6 rows x 4px = 24px per half hour, as in the wireframe
const PX_PER_MIN = ROW_PX / STEP;
const HEAD_PX = 28;
const HOUR_COL_PX = 54;
const AUTOSCROLL_PX = 32; // dragging this close to a scrolled calendar's edge scrolls it
const EDGE_PX = 7; // grab zone for resizing at the selected block's edges

export interface WeekCalendarProps {
  blocks: CalBlock[];
  /** Visible range in minutes since midnight, whole hours. */
  rangeStart: number;
  rangeEnd: number;
  /** Disables dragging (e.g. while a request is in flight). */
  locked?: boolean;
  ariaLabel: string;
  onSelect(key: string): void;
  /** A drag or resize ended somewhere new. */
  onCommit(key: string, slot: Slot): void;
}

export function WeekCalendar({ blocks, rangeStart, rangeEnd, locked, ariaLabel, onSelect, onCommit }: WeekCalendarProps) {
  const calRef = useRef<HTMLDivElement>(null);
  const dragRef = useRef<Drag | null>(null);
  const [drag, setDrag] = useState<Drag | null>(null);

  // When the calendar scrolls sideways (narrow screens), bring the selected block's day into view.
  const selectedKey = blocks.find((b) => b.kind === 'selected')?.key;
  useEffect(() => {
    const cal = calRef.current;
    const el = cal?.querySelector<HTMLElement>('.block.selected');
    if (!cal || !el || cal.scrollWidth <= cal.clientWidth) return;
    const left = el.offsetLeft - HOUR_COL_PX;
    const right = el.offsetLeft + el.offsetWidth;
    if (left < cal.scrollLeft || right > cal.scrollLeft + cal.clientWidth) {
      cal.scrollLeft = Math.max(0, el.offsetLeft - (cal.clientWidth - el.offsetWidth) / 2);
    }
  }, [selectedKey]);

  const rows = (rangeEnd - rangeStart) / STEP;
  const hours: number[] = [];
  for (let h = rangeStart / 60; h < rangeEnd / 60; h++) hours.push(h);

  const rowOf = (m: number) => 2 + Math.floor((m - rangeStart) / STEP);
  const rowEndOf = (m: number) => 2 + Math.ceil((m - rangeStart) / STEP);

  function placement(s: Slot): CSSProperties {
    const start = Math.max(rangeStart, Math.min(s.start, rangeEnd));
    const end = Math.max(start, Math.min(s.end, rangeEnd));
    const style: CSSProperties = {
      gridColumn: 2 + DAYS.indexOf(s.day),
      gridRow: `${rowOf(start)}/${Math.max(rowEndOf(end), rowOf(start) + 1)}`,
    };
    const topOff = ((start - rangeStart) % STEP) * PX_PER_MIN;
    const bottomOff = (STEP - ((end - rangeStart) % STEP || STEP)) * PX_PER_MIN;
    if (topOff) style.marginTop = 1 + topOff;
    if (bottomOff) style.marginBottom = 1 + bottomOff;
    return style;
  }

  function slotAt(d: Drag, clientX: number, clientY: number): Slot {
    const snap = (m: number) => Math.round(m / STEP) * STEP;
    const dMin = (clientY - d.y0) / PX_PER_MIN;
    const len = d.orig.end - d.orig.start;
    if (d.mode === 'top') {
      const start = Math.min(Math.max(snap(d.orig.start + dMin), rangeStart), d.orig.end - STEP);
      return { ...d.orig, start };
    }
    if (d.mode === 'bottom') {
      const end = Math.max(Math.min(snap(d.orig.end + dMin), rangeEnd), d.orig.start + STEP);
      return { ...d.orig, end };
    }
    let day = d.orig.day;
    const cal = calRef.current;
    if (cal) {
      // On narrow screens .cal scrolls sideways inside its border, so measure columns in the
      // scrolled content (scrollWidth, scrollLeft) rather than the visible box.
      const r = cal.getBoundingClientRect();
      const colW = (cal.scrollWidth - HOUR_COL_PX) / 7;
      const x = clientX - r.left - cal.clientLeft + cal.scrollLeft;
      const col = Math.floor((x - HOUR_COL_PX) / colW);
      day = DAYS[Math.max(0, Math.min(6, col))];
    }
    const start = Math.min(Math.max(snap(d.orig.start + dMin), rangeStart), rangeEnd - len);
    return { day, start, end: start + len };
  }

  function onPointerDown(e: PointerEvent<HTMLDivElement>, b: CalBlock) {
    if (b.kind === 'personal' || e.button !== 0) return;
    // .block clips its handles (overflow: hidden, as in the wireframe), so the top/bottom edge of
    // the selected block also counts as a handle.
    let handle = (e.target as HTMLElement).dataset.handle as Mode | undefined;
    if (!handle && b.kind === 'selected') {
      const r = e.currentTarget.getBoundingClientRect();
      if (e.clientY - r.top < EDGE_PX) handle = 'top';
      else if (r.bottom - e.clientY < EDGE_PX) handle = 'bottom';
    }
    if (b.kind !== 'selected') onSelect(b.key);
    if (locked) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    const d: Drag = { key: b.key, mode: handle ?? 'move', x0: e.clientX, y0: e.clientY, orig: b.slot, preview: b.slot, moved: false };
    dragRef.current = d;
    setDrag(d);
  }

  function onPointerMove(e: PointerEvent<HTMLDivElement>) {
    const d = dragRef.current;
    if (!d) return;
    const moved = d.moved || Math.abs(e.clientX - d.x0) > 3 || Math.abs(e.clientY - d.y0) > 3;
    const cal = calRef.current;
    if (moved && d.mode === 'move' && cal && cal.scrollWidth > cal.clientWidth) {
      const r = cal.getBoundingClientRect();
      if (e.clientX > r.right - AUTOSCROLL_PX) cal.scrollLeft += 12;
      else if (e.clientX < r.left + HOUR_COL_PX + AUTOSCROLL_PX) cal.scrollLeft -= 12;
    }
    const next = { ...d, moved, preview: moved ? slotAt(d, e.clientX, e.clientY) : d.preview };
    dragRef.current = next;
    setDrag(next);
  }

  function onPointerUp(e: PointerEvent<HTMLDivElement>) {
    const d = dragRef.current;
    if (!d) return;
    dragRef.current = null;
    setDrag(null);
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId);
    const p = d.preview;
    if (d.moved && (p.day !== d.orig.day || p.start !== d.orig.start || p.end !== d.orig.end)) onCommit(d.key, p);
  }

  function onPointerCancel() {
    dragRef.current = null;
    setDrag(null);
  }

  function onKeyDown(e: KeyboardEvent<HTMLDivElement>, b: CalBlock) {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      onSelect(b.key);
    }
  }

  return (
    <div
      ref={calRef}
      className={`cal es-cal${drag ? ' es-dragging' : ''}`}
      role="group"
      aria-label={ariaLabel}
      style={{ gridTemplateRows: `${HEAD_PX}px repeat(${rows}, ${ROW_PX}px)` }}
    >
      <div style={{ gridRow: 1, gridColumn: 1, borderBottom: '2px solid var(--ink)' }} />
      {DAYS.map((d, i) => (
        <div key={d} className="day" style={{ gridColumn: i + 2 }}>
          {d}
        </div>
      ))}
      {hours.map((h) => {
        const row = rowOf(h * 60);
        const span = `span ${60 / STEP}`;
        return [
          <div key={`h${h}`} className="hour" style={{ gridRowStart: row, gridRowEnd: span }}>
            {formatHourLabel(h)}
          </div>,
          ...DAYS.map((d, i) => (
            <div key={`s${h}${d}`} className="slot" style={{ gridColumn: i + 2, gridRowStart: row, gridRowEnd: span }} />
          )),
        ];
      })}
      {blocks.map((b) => {
        const slot = drag && drag.key === b.key ? drag.preview : b.slot;
        const time = `${b.slot.day} ${formatRange12(slot.start, slot.end)}`;
        if (b.kind === 'personal') {
          return (
            <div key={b.key} className="block personal" style={placement(slot)} title={`${b.title}, ${time}`}>
              {b.title}
            </div>
          );
        }
        const selected = b.kind === 'selected';
        return (
          <div
            key={b.key}
            className={`block ${selected ? 'selected' : 'study'}`}
            style={placement(slot)}
            role="button"
            tabIndex={0}
            aria-pressed={selected}
            aria-label={`${b.title}, ${slot.day} ${formatRange12(slot.start, slot.end)}`}
            data-block={b.key}
            onPointerDown={(e) => onPointerDown(e, b)}
            onPointerMove={onPointerMove}
            onPointerUp={onPointerUp}
            onPointerCancel={onPointerCancel}
            onKeyDown={(e) => onKeyDown(e, b)}
          >
            {b.title}
            {selected && (
              <>
                <span className="handle top" data-handle="top" aria-hidden="true" />
                <span className="handle bottom" data-handle="bottom" aria-hidden="true" />
              </>
            )}
          </div>
        );
      })}
    </div>
  );
}
