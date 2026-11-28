import { StudyTimer, formatTime } from '../lib/studyTimer.mjs';

/** Mount the timer in any frontend container. Returns a cleanup function. */
export function mountStudyTimer(container) {
  const timer = new StudyTimer();
  container.innerHTML = `
    <section class="cb-timer" aria-label="Study timer">
      <h2>Study timer</h2>
      <p>One focused session at a time.</p>
      <div class="cb-timer__presets" role="group" aria-label="Timer presets">
        <button type="button" data-minutes="25">Focus · 25 min</button>
        <button type="button" data-minutes="5">Break · 5 min</button>
      </div>
      <label>Duration (minutes) <input type="number" min="1" max="180" step="1" value="25"></label>
      <button type="button" data-action="apply">Set duration</button>
      <p class="cb-timer__error" role="alert"></p>
      <div class="cb-timer__clock" role="timer" aria-label="Time remaining">25:00</div>
      <p class="cb-timer__status" role="status">Ready to focus</p>
      <div class="cb-timer__controls">
        <button type="button" data-action="start">Start</button>
        <button type="button" data-action="pause">Pause</button>
        <button type="button" data-action="reset">Reset</button>
      </div>
    </section>`;
  const input = container.querySelector('input');
  const clock = container.querySelector('[role="timer"]');
  const status = container.querySelector('[role="status"]');
  const error = container.querySelector('[role="alert"]');
  const start = container.querySelector('[data-action="start"]');
  const pause = container.querySelector('[data-action="pause"]');
  const apply = container.querySelector('[data-action="apply"]');
  const presets = [...container.querySelectorAll('[data-minutes]')];

  function render() {
    const state = timer.tick();
    clock.textContent = formatTime(state.seconds);
    const messages = { idle: 'Ready to focus', running: 'Session in progress', paused: 'Paused', finished: 'Session complete!' };
    if (status.textContent !== messages[state.status]) status.textContent = messages[state.status];
    start.textContent = state.status === 'paused' ? 'Resume' : 'Start';
    start.disabled = state.status === 'running' || state.status === 'finished';
    pause.disabled = state.status !== 'running';
    input.disabled = apply.disabled = state.status === 'running';
    presets.forEach(button => { button.disabled = state.status === 'running'; });
  }
  function configure(minutes) {
    try {
      timer.configure(minutes);
      input.value = String(minutes);
      error.textContent = '';
      render();
    } catch (err) {
      error.textContent = err.message;
    }
  }
  function handleClick(event) {
    const button = event.target.closest('button');
    if (!button || button.disabled) return;
    if (button.dataset.minutes) configure(Number(button.dataset.minutes));
    else if (button.dataset.action === 'apply') configure(input.valueAsNumber);
    else if (button.dataset.action === 'start') timer.start();
    else if (button.dataset.action === 'pause') timer.pause();
    else if (button.dataset.action === 'reset') { timer.reset(); error.textContent = ''; }
    render();
  }
  container.addEventListener('click', handleClick);
  const interval = setInterval(render, 200);
  render();
  return () => { clearInterval(interval); container.removeEventListener('click', handleClick); container.replaceChildren(); };
}
