import { useEffect, useState, type FormEvent, type ReactNode } from 'react';
import {
  ApiError,
  getNotificationSettings,
  listNotifications,
  markAllNotificationsRead,
  markNotificationRead,
  saveNotificationSettings,
} from '../../api/client';
import type { AppNotification, NotificationSettings } from '../../api/types';
import { useAuth } from '../../auth/AuthContext';
import { browserUtcOffsetMinutes } from '../../lib/time';
import { formatDue } from '../schedule/view/shared';
import { AccountShell } from './AccountShell';
import './Notifications.css';

const LEAD_OPTIONS: Array<{ minutes: number; label: string }> = [
  { minutes: 60, label: '1 hour before' },
  { minutes: 180, label: '3 hours before' },
  { minutes: 720, label: '12 hours before' },
  { minutes: 1440, label: '1 day before' },
  { minutes: 2880, label: '2 days before' },
  { minutes: 4320, label: '3 days before' },
  { minutes: 10080, label: '1 week before' },
];

const MSG_PHONE = 'Enter a valid phone number';
const MSG_LEAD = 'Choose when to be reminded before a deadline';
const MSG_SOURCE = 'Turn on at least one notification source';

function leadLabel(m: number): string {
  const known = LEAD_OPTIONS.find((o) => o.minutes === m);
  if (known) return known.label;
  return m % 60 === 0 ? `${m / 60} hours before` : `${m} min before`;
}

/** "Thu, Nov 26, 4:05 pm" in the student's local time (their fixed offset, not the browser's). */
function formatSent(iso: string, offset: number): string {
  if (Number.isNaN(Date.parse(iso))) return iso;
  return formatDue(iso, offset);
}

function Toggle(props: { on: boolean; onChange(v: boolean): void; children: ReactNode; hint?: string }) {
  return (
    <button type="button" className="ac-toggle" role="checkbox" aria-checked={props.on} onClick={() => props.onChange(!props.on)}>
      <span className={`box${props.on ? ' on' : ''}`} aria-hidden="true">{props.on ? 'x' : ''}</span>
      <span>
        {props.children}
        {props.hint && <small>{props.hint}</small>}
      </span>
    </button>
  );
}

export default function Notifications() {
  const { student } = useAuth();
  const offset = student?.utcOffsetMinutes ?? browserUtcOffsetMinutes();
  const [settings, setSettings] = useState<NotificationSettings | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [saved, setSaved] = useState<string | null>(null);

  const [inbox, setInbox] = useState<AppNotification[] | null>(null);
  const [inboxError, setInboxError] = useState<string | null>(null);

  useEffect(() => {
    getNotificationSettings()
      .then(setSettings)
      .catch((e) => setLoadError(e instanceof Error ? e.message : 'Could not load your settings.'));
    listNotifications()
      .then(setInbox)
      .catch((e) => setInboxError(e instanceof Error ? e.message : 'Could not load your inbox.'));
  }, []);

  function patch(p: Partial<NotificationSettings>) {
    setSettings((s) => (s ? { ...s, ...p } : s));
    setSaveError(null);
    setSaved(null);
  }

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    if (!settings) return;
    setSaving(true);
    setSaveError(null);
    setSaved(null);
    try {
      const r = await saveNotificationSettings(settings);
      setSettings(r.settings);
      setSaved(r.message);
    } catch (err) {
      setSaveError(err instanceof ApiError ? err.message : 'Could not save your settings.');
    } finally {
      setSaving(false);
    }
  }

  async function markRead(id: string) {
    try {
      await markNotificationRead(id);
      setInbox((list) => list?.map((n) => (n.id === id ? { ...n, read: true } : n)) ?? null);
    } catch (e) {
      setInboxError(e instanceof Error ? e.message : 'Could not mark it read.');
    }
  }

  async function markAll() {
    try {
      await markAllNotificationsRead();
      setInbox((list) => list?.map((n) => ({ ...n, read: true })) ?? null);
    } catch (e) {
      setInboxError(e instanceof Error ? e.message : 'Could not mark them read.');
    }
  }

  const phoneError = saveError === MSG_PHONE ? saveError : null;
  const leadError = saveError === MSG_LEAD ? saveError : null;
  const sourceError = saveError === MSG_SOURCE ? saveError : null;
  const otherError = saveError && !phoneError && !leadError && !sourceError ? saveError : null;
  const unread = inbox?.filter((n) => !n.read).length ?? 0;
  const leadOptions =
    settings && !LEAD_OPTIONS.some((o) => o.minutes === settings.deadlineLeadMinutes) && settings.deadlineLeadMinutes > 0
      ? [...LEAD_OPTIONS, { minutes: settings.deadlineLeadMinutes, label: leadLabel(settings.deadlineLeadMinutes) }].sort(
          (a, b) => a.minutes - b.minutes,
        )
      : LEAD_OPTIONS;

  return (
    <AccountShell crumb="Notifications" title="Notifications" subtitle="Choose how Cramberry reminds you about deadlines.">
      {loadError && <div className="alert" role="alert">{loadError}</div>}
      {!settings && !loadError && <p className="muted">Loading your settings…</p>}

      {settings && (
        <form className="ac-nt-cols" onSubmit={onSubmit} aria-label="Notification settings">
          <section className="panel" aria-label="Enable notifications">
            <div className="rowhead">
              <h2>Enable Notifications</h2>
              <span className="req">FR 12, FR 13</span>
            </div>
            <p className="hint">Turn this off to stop every reminder. Your choices below are kept.</p>
            <div className="seg ac-seg" role="radiogroup" aria-label="Notifications">
              <button type="button" role="radio" aria-checked={settings.enabled} className={settings.enabled ? 'on' : ''} onClick={() => patch({ enabled: true })}>
                On
              </button>
              <button type="button" role="radio" aria-checked={!settings.enabled} className={!settings.enabled ? 'on' : ''} onClick={() => patch({ enabled: false })}>
                Off
              </button>
            </div>
          </section>

          <section className={`panel${settings.enabled ? '' : ' ac-off'}`} aria-label="Overview">
            <div className="rowhead">
              <h2>Overview</h2>
              <span className="req">Use case 16</span>
            </div>
            <Toggle on={settings.deadlineReminders} onChange={(v) => patch({ deadlineReminders: v })} hint="A reminder before each unfinished task is due.">
              Deadline Reminders
            </Toggle>
            <div className="field ac-indent ac-narrow">
              <label htmlFor="ac-lead">Remind me</label>
              <select
                id="ac-lead"
                className={`input${leadError ? ' bad' : ''}`}
                value={settings.deadlineLeadMinutes}
                disabled={!settings.deadlineReminders}
                onChange={(e) => patch({ deadlineLeadMinutes: Number(e.target.value) })}
              >
                {leadOptions.map((o) => (
                  <option key={o.minutes} value={o.minutes}>
                    {o.label}
                  </option>
                ))}
              </select>
              {leadError && <p className="errtext">{leadError}</p>}
            </div>
          </section>

          <section className={`panel${settings.enabled ? '' : ' ac-off'}`} aria-label="Notification sources">
            <div className="rowhead">
              <h2>Notification Sources</h2>
              <span className="req">FR 12</span>
            </div>
            <Toggle on={settings.email} onChange={(v) => patch({ email: v })}>Email</Toggle>
            <Toggle on={settings.desktop} onChange={(v) => patch({ desktop: v })} hint="Shows in your Cramberry inbox.">
              Desktop Notification
            </Toggle>
            <Toggle on={settings.sms} onChange={(v) => patch({ sms: v })}>SMS</Toggle>
            <div className="field ac-indent ac-narrow">
              <label htmlFor="ac-phone">Phone number</label>
              <input
                id="ac-phone"
                className={`input${phoneError ? ' bad' : ''}`}
                type="tel"
                autoComplete="tel"
                placeholder="+1 214 555 0123"
                value={settings.phoneNumber}
                disabled={!settings.sms}
                onChange={(e) => patch({ phoneNumber: e.target.value })}
              />
              {phoneError && <p className="errtext">{phoneError}</p>}
            </div>
            {sourceError && <p className="errtext">{sourceError}</p>}
          </section>

          <div className="summary">
            {otherError && <p className="errtext" role="alert">{otherError}</p>}
            {saveError && !otherError && <p className="errtext" role="alert">Fix the highlighted setting to save.</p>}
            {saved && <p className="ac-saved" role="status">{saved}</p>}
            <button type="submit" className="btn primary" disabled={saving}>
              {saving ? 'Saving…' : 'Save settings'}
            </button>
          </div>
        </form>
      )}

      <section className="panel" aria-label="Inbox">
        <div className="rowhead">
          <h2>Inbox{unread > 0 ? ` (${unread} unread)` : ''}</h2>
          <button type="button" className="btn small" onClick={() => void markAll()} disabled={!unread}>
            Mark all read
          </button>
        </div>
        {inboxError && <p className="errtext">{inboxError}</p>}
        {!inbox && !inboxError && <p className="muted">Loading…</p>}
        {inbox && inbox.length === 0 && (
          <p className="hint">No notifications yet. Deadline reminders will show up here.</p>
        )}
        {inbox && inbox.length > 0 && (
          <ul className="list ac-inbox">
            {inbox.map((n) => (
              <li key={n.id} className={n.read ? '' : 'ac-unread'}>
                <div>
                  <strong>{n.subject}</strong>
                  <span className="req"> · {n.channel} · {formatSent(n.sentAt, offset)}</span>
                  <p>{n.body}</p>
                </div>
                {n.read ? (
                  <span className="req">Read</span>
                ) : (
                  <button type="button" className="linkish" onClick={() => void markRead(n.id)}>
                    Mark read
                  </button>
                )}
              </li>
            ))}
          </ul>
        )}
      </section>
    </AccountShell>
  );
}
