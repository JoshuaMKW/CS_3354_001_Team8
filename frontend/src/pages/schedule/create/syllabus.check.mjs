// Unit checks for the syllabus deadline extractor.
// Run: node src/pages/schedule/create/syllabus.check.mjs   (Node 22.18+/23.6+ strips the TS types)
import assert from 'node:assert/strict';
import { extractDeadlines, guessType } from './syllabus.ts';

const today = new Date(2026, 10, 27); // Fri Nov 27 2026, local
let n = 0;
function check(name, fn) {
  fn();
  n++;
  console.log(`ok ${n} - ${name}`);
}

const one = (line) => {
  const r = extractDeadlines(line, today);
  assert.equal(r.length, 1, `expected one hit for ${JSON.stringify(line)}, got ${JSON.stringify(r)}`);
  return r[0];
};

check('short month name, no time', () => {
  const d = one('Homework 5 due Dec 3');
  assert.equal(d.date, '2026-12-03');
  assert.equal(d.time, '23:59');
  assert.equal(d.title, 'Homework 5');
  assert.equal(d.type, 'assignment');
});

check('long month name with ordinal and 12-hour time', () => {
  const d = one('Project 2: final report due December 10th at 11:59 pm');
  assert.equal(d.date, '2026-12-10');
  assert.equal(d.time, '23:59');
  assert.equal(d.title, 'Project 2: final report');
  assert.equal(d.type, 'assignment');
  assert.equal(d.estimatedMinutes, 240);
});

check('slash date with 24-hour time', () => {
  const d = one('Quiz 4 - 12/08 14:30');
  assert.equal(d.date, '2026-12-08');
  assert.equal(d.time, '14:30');
  assert.equal(d.type, 'exam');
  assert.equal(d.estimatedMinutes, 60);
});

check('ISO date, midterm, weekday stripped', () => {
  const d = one('Midterm exam: Thursday, 2026-12-03, 10am');
  assert.equal(d.date, '2026-12-03');
  assert.equal(d.time, '10:00');
  assert.equal(d.type, 'exam');
  assert.equal(d.title, 'Midterm exam');
});

check('January after a fall term rolls into next year', () => {
  assert.equal(one('Final exam Jan 12, 9 am').date, '2027-01-12');
});

check('a date earlier this fall stays in this year', () => {
  assert.equal(one('Assignment 1 due Sep 10').date, '2026-09-10');
});

check('explicit year wins', () => {
  assert.equal(one('Paper due March 3, 2027').date, '2027-03-03');
});

check('day-first form "3 December"', () => {
  assert.equal(one('Lab report due 3 December').date, '2026-12-03');
});

check('noon', () => {
  assert.equal(one('Presentation slides due Dec 1 by noon').time, '12:00');
});

check('ignores lines without a deadline word or without a date', () => {
  const text = [
    'CS 3354 Software Engineering, Fall 2026',
    'Office hours: Tue 2-3 pm',
    'Lecture on Dec 2: design patterns',
    'Exams are closed book.',
    'Assignment 3 due Dec 5',
  ].join('\n');
  const r = extractDeadlines(text, today);
  assert.equal(r.length, 1);
  assert.equal(r[0].line, 5);
});

check('invalid dates are rejected', () => {
  assert.equal(extractDeadlines('Homework due 13/45', today).length, 0);
  assert.equal(extractDeadlines('Homework due Feb 30', today).length, 0);
});

check('bullet and table rows', () => {
  const r = extractDeadlines('- HW 6 | due Dec 9\n* Quiz 5 | Dec 11', today);
  assert.equal(r.length, 2);
  assert.equal(r[0].title, 'HW 6');
  assert.equal(r[1].title, 'Quiz 5');
});

check('guessType', () => {
  assert.equal(guessType('Read chapter 4 before quiz'), 'exam');
  assert.equal(guessType('Read chapter 4'), 'study');
  assert.equal(guessType('Bring a calculator'), 'other');
  assert.equal(guessType('Review session for project'), 'study');
});

check('a number before a month is an item number, not the day', () => {
  assert.equal(one('Week 12 Dec 1: Project proposal due').date, '2026-12-01');
  assert.equal(one('Quiz 2 Oct 14').date, '2026-10-14');
  assert.equal(one('Homework 3 Dec 10').date, '2026-12-10');
});

check('the verb "may" is not a month', () => {
  assert.equal(one('Project 1 may be submitted until Dec 5').date, '2026-12-05');
});

check('an item number with a slash is not a date', () => {
  assert.equal(one('Homework 1/2 due Dec 3').date, '2026-12-03');
});

check('"May" followed by a day is still a month', () => {
  assert.equal(one('Final project due May 4').date, '2027-05-04');
});

console.log(`${n} checks passed`);
