// Read-only week calendar in the Edit Schedule wireframe's look (.cal/.day/.hour/.slot/.block).
// Rows are 5 minutes (4px) so a half hour is 24px, exactly like the wireframe's half-hour rows,
// while blocks that start at 4:05 still land in the right place.
import type { Block, Day, TimeRange } from '../../../api/types';
import { DAYS, formatHourLabel, formatRange12, toMinutes, weekDates } from '../../../lib/time';
import { personalKey } from './shared';
import './shared.css';

const STEP = 5; // minutes per grid row
const ROWS_PER_HOUR = 60 / STEP;

export interface WeekCalendarProps {
  blocks: Block[];
  personal: TimeRange[];
  labels: Record<string, string>;
  weekStart?: string;
  utcOffsetMinutes: number;
  today?: Day;
  onOpen(block: Block): void;
  /** Default 8 am to 8 pm; widened to fit the data. */
  minHour?: number;
  maxHour?: number;
}

export function WeekCalendar({
  blocks,
  personal,
  labels,
  weekStart,
  utcOffsetMinutes,
  today,
  onOpen,
  minHour = 8,
  maxHour = 20,
}: WeekCalendarProps) {
  let first = minHour * 60;
  let last = maxHour * 60;
  for (const r of [...blocks, ...personal]) {
    first = Math.min(first, toMinutes(r.start));
    last = Math.max(last, toMinutes(r.end));
  }
  const startHour = Math.floor(first / 60);
  const endHour = Math.min(24, Math.ceil(last / 60));
  const hours = Array.from({ length: endHour - startHour }, (_, i) => startHour + i);
  const dates = weekStart ? weekDates(weekStart, utcOffsetMinutes) : null;

  const row = (hm: string) => 2 + Math.round((toMinutes(hm) - startHour * 60) / STEP);
  const col = (d: Day) => 2 + DAYS.indexOf(d);

  return (
    <div
      className="cal vs-cal"
      style={{ gridTemplateRows: `28px repeat(${hours.length * ROWS_PER_HOUR}, 4px)` }}
      aria-label="Week calendar"
    >
      <div style={{ gridRow: 1, gridColumn: 1, borderBottom: '2px solid var(--ink)' }} />
      {DAYS.map((d, i) => (
        <div key={d} className={`day${d === today ? ' vs-today' : ''}`} style={{ gridColumn: i + 2 }}>
          {d === today ? <b>{d}</b> : d} {dates && <small>{dates[i].date}</small>}
        </div>
      ))}
      {hours.map((h, i) => {
        const r = 2 + i * ROWS_PER_HOUR;
        return [
          <div key={`h${h}`} className="hour" style={{ gridRowStart: r, gridRowEnd: `span ${ROWS_PER_HOUR}` }}>
            {formatHourLabel(h)}
          </div>,
          ...DAYS.map((d, j) => (
            <div
              key={`s${h}${d}`}
              className="slot"
              style={{ gridColumn: j + 2, gridRowStart: r, gridRowEnd: `span ${ROWS_PER_HOUR}` }}
            />
          )),
        ];
      })}
      {personal.map((p) => (
        <div
          key={`p${personalKey(p)}`}
          className="block personal vs-personal"
          style={{ gridColumn: col(p.day), gridRowStart: row(p.start), gridRowEnd: row(p.end) }}
          title={`${labels[personalKey(p)] ?? 'Personal time'}, ${formatRange12(p.start, p.end)}`}
        >
          {labels[personalKey(p)] ?? 'Personal time'}
        </div>
      ))}
      {blocks.map((b) => {
        const long = toMinutes(b.end) - toMinutes(b.start) >= 45;
        return (
          <button
            key={`b${b.assignmentId}${b.day}${b.start}`}
            type="button"
            className="block study vs-block"
            style={{ gridColumn: col(b.day), gridRowStart: row(b.start), gridRowEnd: row(b.end) }}
            onClick={() => onOpen(b)}
            aria-label={`${b.title}, ${b.day} ${formatRange12(b.start, b.end)}. Open in the editor`}
          >
            {b.title}
            {long && <span className="vs-when">{formatRange12(b.start, b.end)}</span>}
          </button>
        );
      })}
    </div>
  );
}
