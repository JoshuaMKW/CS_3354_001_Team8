import { test } from 'node:test';
import assert from 'node:assert/strict';
import { StudyTimer, formatTime } from './studyTimer.mjs';

function setup(minutes = 25) {
  let time = 0;
  return { timer: new StudyTimer(minutes, () => time), advance: ms => { time += ms; } };
}

test('FR-25 starts and counts down accurately', () => {
  const { timer, advance } = setup();
  timer.start(); advance(10_000);
  assert.deepEqual(timer.tick(), { status: 'running', seconds: 1490 });
});
test('FR-25 pause and resume preserve remaining time', () => {
  const { timer, advance } = setup(5);
  timer.start(); advance(20_000); timer.pause(); advance(90_000);
  assert.deepEqual(timer.tick(), { status: 'paused', seconds: 280 });
  timer.start(); advance(30_000);
  assert.equal(timer.tick().seconds, 250);
});
test('FR-25 reset restores configured duration', () => {
  const { timer, advance } = setup(5);
  timer.start(); advance(20_000); timer.reset();
  assert.deepEqual(timer.tick(), { status: 'idle', seconds: 300 });
});
test('FR-25 countdown finishes at zero even after delayed update', () => {
  const { timer, advance } = setup(1);
  timer.start(); advance(90_000);
  assert.deepEqual(timer.tick(), { status: 'finished', seconds: 0 });
  timer.start(); assert.equal(timer.tick().seconds, 0);
});
test('FR-25 custom duration and invalid values', () => {
  const { timer } = setup();
  for (const value of [0, -1, 181, 1.5, NaN, Infinity]) assert.throws(() => timer.configure(value), RangeError);
  assert.equal(timer.tick().seconds, 1500);
  timer.configure(12); assert.equal(timer.tick().seconds, 720);
});
test('FR-25 formats countdown', () => {
  assert.equal(formatTime(65), '1:05');
  assert.equal(formatTime(0), '0:00');
});
