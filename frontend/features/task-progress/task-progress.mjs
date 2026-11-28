import { statuses } from './api.mjs';

const labels = { NOT_STARTED: 'Not started', IN_PROGRESS: 'In progress', COMPLETED: 'Completed' };
function node(tag, text, className) {
  const el = document.createElement(tag);
  if (text !== undefined) el.textContent = text;
  if (className) el.className = className;
  return el;
}

/** Mounts UC-13, UC-14, FR-24 and FR-26. Supply the application's authenticated API. */
export function mountTaskProgress(container, api) {
  container.innerHTML = `<section class="cb-progress" aria-label="Task progress">
    <h2>Tasks</h2><form class="cb-progress__filters">
      <label>Course <select name="courseId"><option value="">All courses</option></select></label>
      <label>Due from (UTC) <input name="from" type="date"></label>
      <label>Due before (UTC) <input name="to" type="date"></label>
      <label>Status <select name="status"><option value="">All statuses</option><option value="NOT_STARTED">Not started</option><option value="IN_PROGRESS">In progress</option><option value="COMPLETED">Completed</option></select></label>
      <label><input name="overdue" type="checkbox"> Overdue only</label>
      <button type="submit">Apply filters</button><button type="reset">Clear</button>
    </form><p data-task-message role="status"></p><ul data-tasks class="cb-progress__list"></ul>
    <h2>Weekly progress</h2><form data-report-form class="cb-progress__filters">
      <label>Week starting (UTC) <input name="start" type="date"></label><button type="submit">View report</button>
    </form><p data-report-message role="status"></p><div data-report></div>
  </section>`;
  const filters = container.querySelector('form');
  const reportForm = container.querySelector('[data-report-form]');
  const tasks = container.querySelector('[data-tasks]');
  const report = container.querySelector('[data-report]');
  const taskMessage = container.querySelector('[data-task-message]');
  const reportMessage = container.querySelector('[data-report-message]');
  let disposed = false, taskVersion = 0, reportVersion = 0, saving = false;
  async function loadTasks() {
    const version = ++taskVersion;
    taskMessage.textContent = 'Loading tasks…';
    try {
      const values = Object.fromEntries(new FormData(filters));
      values.overdue = filters.elements.overdue.checked;
      const rows = await api.listTasks(values);
      if (disposed || version !== taskVersion) return;
      tasks.replaceChildren();
      taskMessage.textContent = rows.length ? `${rows.length} task(s)` : 'No matching tasks.';
      for (const task of rows) {
        const li = node('li', undefined, 'cb-progress__task');
        const detail = node('div');
        detail.append(node('strong', task.title), node('p', `Due: ${new Date(task.dueAt).toLocaleString()}`));
        if (task.overdue) detail.append(node('span', 'Overdue', 'cb-progress__overdue'));
        const select = node('select');
        select.setAttribute('aria-label', `Status for ${task.title}`);
        for (const status of statuses) { const option = node('option', labels[status]); option.value = status; select.append(option); }
        select.value = task.status;
        select.dataset.taskId = task.id;
        li.append(detail, select); tasks.append(li);
      }
    } catch (error) { if (!disposed && version === taskVersion) taskMessage.textContent = error.message; }
  }
  async function loadReport() {
    const version = ++reportVersion;
    reportMessage.textContent = 'Loading report…';
    try {
      const result = await api.weeklyReport(reportForm.elements.start.value);
      if (disposed || version !== reportVersion) return;
      report.replaceChildren();
      reportMessage.textContent = `${result.weekStart.slice(0, 10)} to ${result.weekEnd.slice(0, 10)} (end exclusive)`;
      for (const [key, label] of [['completed', 'Completed'], ['overdue', 'Overdue'], ['remaining', 'Remaining']]) {
        const section = node('section', undefined, 'cb-progress__summary');
        section.append(node('h3', `${label} (${result[key].length})`));
        const list = node('ul');
        for (const item of result[key]) list.append(node('li', item.title));
        if (!result[key].length) list.append(node('li', 'None'));
        section.append(list); report.append(section);
      }
      report.append(node('p', result.averageCompletionMinutes == null ? 'Average completion time: unavailable' : `Average completion time: ${result.averageCompletionMinutes} minutes`));
    } catch (error) { if (!disposed && version === reportVersion) reportMessage.textContent = error.message; }
  }
  async function handleStatus(event) {
    const select = event.target;
    if (!select.dataset.taskId || saving) return;
    saving = true;
    const controls = [...tasks.querySelectorAll('select')];
    controls.forEach(control => { control.disabled = true; });
    try { await api.updateStatus(select.dataset.taskId, select.value); await Promise.all([loadTasks(), loadReport()]); }
    catch (error) { await loadTasks(); if (!disposed) taskMessage.textContent = `Status was not saved: ${error.message}`; }
    finally { saving = false; controls.forEach(control => { control.disabled = false; }); }
  }
  const submitTasks = event => { event.preventDefault(); loadTasks(); };
  const resetTasks = () => queueMicrotask(loadTasks);
  const submitReport = event => { event.preventDefault(); loadReport(); };
  filters.addEventListener('submit', submitTasks); filters.addEventListener('reset', resetTasks);
  reportForm.addEventListener('submit', submitReport); tasks.addEventListener('change', handleStatus);
  api.listCourses().then(courses => {
    if (disposed) return;
    for (const course of courses) { const option = node('option', course.code || course.name); option.value = course.id; filters.elements.courseId.append(option); }
  }).catch(error => { if (!disposed) taskMessage.textContent = error.message; });
  loadTasks(); loadReport();
  return () => { disposed = true; filters.removeEventListener('submit', submitTasks); filters.removeEventListener('reset', resetTasks); reportForm.removeEventListener('submit', submitReport); tasks.removeEventListener('change', handleStatus); container.replaceChildren(); };
}
