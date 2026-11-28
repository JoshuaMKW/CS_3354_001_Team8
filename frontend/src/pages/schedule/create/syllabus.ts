// Finds candidate deadlines in plain syllabus text. Runs entirely in the browser.
// A line counts when it has a date ("Dec 3", "December 3rd", "12/03", "2026-12-03")
// and a deadline word (due, exam, quiz, project, assignment, midterm, final, ...).
// Pure functions with no imports, so `node create/syllabus.check.mjs` can test it directly.

export type DraftType = 'assignment' | 'exam' | 'study' | 'other';

export interface DraftDeadline {
  /** 1-based line number in the source text. */
  line: number;
  source: string;
  title: string;
  type: DraftType;
  /** Local "YYYY-MM-DD". */
  date: string;
  /** Local "HH:MM" (23:59 when the line has no time). */
  time: string;
  estimatedMinutes: number;
}

const MONTH_NAMES = ['jan', 'feb', 'mar', 'apr', 'may', 'jun', 'jul', 'aug', 'sep', 'oct', 'nov', 'dec'];

const KEYWORD = /\b(due|deadline|exams?|quiz(?:zes)?|projects?|assignments?|midterms?|finals?|homework|hw\s*\d*|papers?|essays?|labs?|presentations?|reports?|tests?|submit|submission)\b/i;

const MONTH_RE =
  '(jan(?:uary)?|feb(?:ruary)?|mar(?:ch)?|apr(?:il)?|may|june?|july?|aug(?:ust)?|sep(?:t(?:ember)?)?|oct(?:ober)?|nov(?:ember)?|dec(?:ember)?)';
const ISO_DATE = /\b(\d{4})-(\d{1,2})-(\d{1,2})\b/g;
const NAMED_DATE = new RegExp(`\\b${MONTH_RE}\\.?\\s+(\\d{1,2})(?:st|nd|rd|th)?\\b(?:,?\\s+(\\d{4})\\b)?`, 'gi');
const DAY_FIRST_DATE = new RegExp(`\\b(\\d{1,2})(?:st|nd|rd|th)?\\s+(?:of\\s+)?${MONTH_RE}\\b\\.?(?:,?\\s+(\\d{4})\\b)?`, 'gi');
const SLASH_DATE = /\b(\d{1,2})\/(\d{1,2})(?:\/(\d{2}|\d{4}))?\b/g;
/** A number right after one of these words is an item number ("Week 12", "Quiz 2"), not a day. */
const LABEL_BEFORE =
  /\b(?:weeks?|quiz(?:zes)?|homeworks?|hw|projects?|exams?|chapters?|ch|labs?|units?|assignments?|modules?|lectures?|tests?|midterms?|parts?|problems?|sections?)\.?\s*#?\s*$/i;
/** Words a deadline date sits next to ("due Dec 3", "Exam: Dec 10"). */
const ANCHOR = /\b(?:due|deadline|exams?|midterms?|finals?|quiz(?:zes)?|tests?|submit(?:ted)?|submission)\b/gi;
const TIME_12 = /\b(\d{1,2})(?::(\d{2}))?\s*([ap])\.?\s*m\b\.?/i;
const TIME_24 = /\b([01]?\d|2[0-3]):([0-5]\d)\b/;
const NOON = /\b(noon|midnight)\b/i;
const WEEKDAY = /\b(mon|tue|tues|wed|thu|thur|thurs|fri|sat|sun)(day|nesday|rsday|urday|sday)?\b\.?,?/gi;

const pad = (n: number) => String(n).padStart(2, '0');

function validDate(y: number, m: number, d: number): boolean {
  if (m < 1 || m > 12 || d < 1 || d > 31) return false;
  const t = new Date(Date.UTC(y, m - 1, d));
  return t.getUTCMonth() === m - 1 && t.getUTCDate() === d;
}

/** Year for a month/day with no year: the one closest to `today` (so Jan after a Fall term rolls over). */
function guessYear(m: number, d: number, today: Date): number {
  const y = today.getFullYear();
  const base = Date.UTC(y, today.getMonth(), today.getDate());
  let best = y;
  let bestDist = Infinity;
  for (const cand of [y - 1, y, y + 1]) {
    const dist = Math.abs(Date.UTC(cand, m - 1, d) - base);
    if (dist < bestDist) {
      bestDist = dist;
      best = cand;
    }
  }
  return best;
}

interface DateHit {
  y: number;
  m: number;
  d: number;
  text: string;
}

/** "may" is a month only as "May"/"MAY" (so "Project 1 may be submitted" isn't May 1). */
function monthNumber(word: string, needCapitalMay: boolean): number {
  const mo = MONTH_NAMES.indexOf(word.slice(0, 3).toLowerCase()) + 1;
  if (mo === 5 && needCapitalMay && word !== 'May' && word !== 'MAY') return 0;
  return mo;
}

interface Candidate {
  idx: number;
  end: number;
  hit: DateHit;
}

function findDate(line: string, today: Date): DateHit | null {
  const candidates: Candidate[] = [];
  const push = (m: RegExpExecArray, y: number, mo: number, d: number) => {
    if (validDate(y, mo, d)) candidates.push({ idx: m.index, end: m.index + m[0].length, hit: { y, m: mo, d, text: m[0] } });
  };
  const labelled = (idx: number) => LABEL_BEFORE.test(line.slice(0, idx));

  for (const m of line.matchAll(ISO_DATE)) push(m, +m[1], +m[2], +m[3]);

  // Month-first ("Dec 3") is the preferred form. The regex already requires a day after the month.
  let monthFirst = 0;
  for (const m of line.matchAll(NAMED_DATE)) {
    const mo = monthNumber(m[1], true);
    if (!mo) continue;
    monthFirst++;
    const d = +m[2];
    push(m, m[3] ? +m[3] : guessYear(mo, d, today), mo, d);
  }

  // Day-first ("3 December") only when it can't be a misread "Week 12 Dec 1" or "Quiz 2 Oct 14":
  // no month-first date anywhere in the line, and the number isn't an item number.
  // "may" never counts here: "1 may" is almost always the verb.
  if (!monthFirst) {
    for (const m of line.matchAll(DAY_FIRST_DATE)) {
      const mo = MONTH_NAMES.indexOf(m[2].slice(0, 3).toLowerCase()) + 1;
      if (mo === 5 || labelled(m.index)) continue;
      const d = +m[1];
      push(m, m[3] ? +m[3] : guessYear(mo, d, today), mo, d);
    }
  }

  for (const m of line.matchAll(SLASH_DATE)) {
    if (labelled(m.index)) continue; // "Homework 1/2" is an item number
    const mo = +m[1];
    const d = +m[2];
    let y = m[3] ? +m[3] : guessYear(mo, d, today);
    if (y < 100) y += 2000;
    push(m, y, mo, d);
  }
  if (!candidates.length) return null;

  // Prefer the date nearest a due/exam word; on a tie, one that follows the word, then the earliest.
  const anchors = [...line.matchAll(ANCHOR)].map((a) => ({ idx: a.index, end: a.index + a[0].length }));
  const score = (c: Candidate): [number, number] => {
    let best = Infinity;
    let after = 1;
    for (const a of anchors) {
      const gap = c.idx >= a.end ? c.idx - a.end : a.idx >= c.end ? a.idx - c.end : 0;
      const isAfter = c.idx >= a.end ? 0 : 1;
      if (gap < best || (gap === best && isAfter < after)) {
        best = gap;
        after = isAfter;
      }
    }
    return [best, after];
  };
  candidates.sort((a, b) => {
    const [ga, fa] = score(a);
    const [gb, fb] = score(b);
    return ga - gb || fa - fb || a.idx - b.idx;
  });
  return candidates[0].hit;
}

function findTime(line: string): { hm: string; text: string } | null {
  let m = TIME_12.exec(line);
  if (m) {
    let h = +m[1] % 12;
    if (m[3].toLowerCase() === 'p') h += 12;
    const min = m[2] ? +m[2] : 0;
    if (+m[1] >= 1 && +m[1] <= 12 && min < 60) return { hm: `${pad(h)}:${pad(min)}`, text: m[0] };
  }
  m = NOON.exec(line);
  if (m) return { hm: m[1].toLowerCase() === 'noon' ? '12:00' : '23:59', text: m[0] };
  m = TIME_24.exec(line);
  if (m) return { hm: `${pad(+m[1])}:${m[2]}`, text: m[0] };
  return null;
}

export function guessType(text: string): DraftType {
  const workWord = /\b(projects?|reports?|papers?|essays?|presentations?|assignments?|homework)\b/i.test(text);
  if (/\b(exams?|midterms?|quiz(?:zes)?|tests?)\b/i.test(text)) return 'exam';
  if (/\bfinals?\b/i.test(text) && !workWord) return 'exam';
  if (/\b(read(?:ing)?|review|study)\b/i.test(text) && !/\b(due|submit)\b/i.test(text)) return 'study';
  if (KEYWORD.test(text)) return 'assignment';
  return 'other';
}

export function guessMinutes(text: string): number {
  if (/\bfinals?\b/i.test(text) && /\bexam\b/i.test(text)) return 300;
  if (/\b(midterms?|finals?)\b/i.test(text)) return 240;
  if (/\b(projects?|presentations?)\b/i.test(text)) return 240;
  if (/\bexams?\b|\btests?\b/i.test(text)) return 180;
  if (/\b(papers?|essays?|reports?)\b/i.test(text)) return 180;
  if (/\bquiz(?:zes)?\b/i.test(text)) return 60;
  return 90;
}

function cleanTitle(line: string, removed: string[]): string {
  let t = line;
  for (const r of removed) t = t.replace(r, ' ');
  t = t
    .replace(WEEKDAY, ' ')
    .replace(/^\s*(?:[-*•>]+|\d+[.)]|week\s*\d+\s*[:.-]?)\s*/i, '')
    .replace(/\(\s*\)/g, ' ')
    .replace(/\b(is\s+)?(due|due\s+(on|by|at)|on|by|at)\s*([,:;.|-]\s*)?$/i, '')
    .replace(/\s+(on|by|at)\s+(?=[,:;|-]|$)/gi, ' ')
    .replace(/\s*[|]\s*/g, ' - ')
    .replace(/\s{2,}/g, ' ')
    .replace(/^[\s,:;.|-]+|[\s,:;|(-]+$/g, '')
    .replace(/\s+(due|on|by|at)$/i, '')
    .replace(/[\s,:;|(-]+$/g, '')
    .trim();
  if (t.length > 200) t = t.slice(0, 200).trim();
  return t;
}

export function extractDeadlines(text: string, today: Date = new Date()): DraftDeadline[] {
  const out: DraftDeadline[] = [];
  const lines = text.split(/\r?\n/);
  lines.forEach((raw, i) => {
    const line = raw.trim();
    if (!line || !KEYWORD.test(line)) return;
    const date = findDate(line, today);
    if (!date) return;
    const time = findTime(line.replace(date.text, ' '));
    let title = cleanTitle(line, [date.text, ...(time ? [time.text] : [])]);
    if (!title) {
      const k = KEYWORD.exec(line)?.[0] ?? 'Deadline';
      title = k[0].toUpperCase() + k.slice(1);
    }
    out.push({
      line: i + 1,
      source: line,
      title,
      type: guessType(line),
      date: `${date.y}-${pad(date.m)}-${pad(date.d)}`,
      time: time?.hm ?? '23:59',
      estimatedMinutes: guessMinutes(line),
    });
  });
  return out;
}
