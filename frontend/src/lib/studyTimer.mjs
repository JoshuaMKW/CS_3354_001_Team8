/** @typedef {'idle'|'running'|'paused'|'finished'} TimerStatus */

/** A dependency-free countdown model. Times are milliseconds. */
export class StudyTimer {
  /** @param {number} minutes @param {() => number} now */
  constructor(minutes = 25, now = () => Date.now()) {
    this.now = now;
    this.configure(minutes);
  }

  /** @param {number} minutes */
  configure(minutes) {
    if (!Number.isInteger(minutes) || minutes < 1 || minutes > 180) {
      throw new RangeError('Duration must be a whole number from 1 to 180 minutes.');
    }
    this.durationMs = minutes * 60_000;
    this.reset();
  }

  reset() {
    this.remainingMs = this.durationMs;
    this.deadline = null;
    /** @type {TimerStatus} */
    this.status = 'idle';
  }

  start() {
    this.tick();
    if (this.status === 'running' || this.status === 'finished') return;
    this.deadline = this.now() + this.remainingMs;
    this.status = 'running';
  }

  pause() {
    this.tick();
    if (this.status !== 'running') return;
    this.deadline = null;
    this.status = 'paused';
  }

  tick() {
    if (this.status === 'running' && this.deadline !== null) {
      this.remainingMs = Math.max(0, this.deadline - this.now());
      if (this.remainingMs === 0) {
        this.deadline = null;
        this.status = 'finished';
      }
    }
    return { status: this.status, seconds: Math.ceil(this.remainingMs / 1000) };
  }
}

/** @param {number} seconds */
export function formatTime(seconds) {
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}
