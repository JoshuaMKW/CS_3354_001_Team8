import { useState, type FormEvent } from 'react';
import { Link } from 'react-router-dom';
import { useAuth } from '../../auth/AuthContext';
import { ApiError, updateMe } from '../../api/client';
import type { UpdateMeRequest } from '../../api/types';
import { browserUtcOffsetMinutes } from '../../lib/time';
import { AccountShell, UTC_OFFSETS, formatUtcOffset } from './AccountShell';
import './EditAccount.css';

const DEVICE = 'device';

export default function EditAccount() {
  const { student, setStudent } = useAuth();
  const [name, setName] = useState(student?.name ?? '');
  const [offset, setOffset] = useState<string>(String(student?.utcOffsetMinutes ?? 0));
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState<string | null>(null);

  if (!student) return null;

  const deviceOffset = browserUtcOffsetMinutes();
  const chosenOffset = offset === DEVICE ? deviceOffset : Number(offset);
  const nameTrim = name.trim();
  const dirty = nameTrim !== student.name || chosenOffset !== student.utcOffsetMinutes;
  const offsetChanged = chosenOffset !== student.utcOffsetMinutes;
  const nameError = nameTrim ? null : 'Enter your name';
  // Device offset may fall outside the half-hour list (e.g. +05:45); include it so the select can show it.
  const offsets = UTC_OFFSETS.includes(student.utcOffsetMinutes)
    ? UTC_OFFSETS
    : [...UTC_OFFSETS, student.utcOffsetMinutes].sort((a, b) => a - b);

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    if (nameError || !dirty || !student) return;
    const body: UpdateMeRequest = {};
    if (nameTrim !== student.name) body.name = nameTrim;
    if (offsetChanged) body.utcOffsetMinutes = chosenOffset;
    setSaving(true);
    setError(null);
    setSaved(null);
    try {
      const updated = await updateMe(body);
      setStudent(updated);
      setName(updated.name);
      setOffset(String(updated.utcOffsetMinutes));
      setSaved('Account saved.');
    } catch (err) {
      setError(err instanceof ApiError ? err.message : 'Could not save your account.');
    } finally {
      setSaving(false);
    }
  }

  return (
    <AccountShell crumb="Edit Account" title="Edit account" subtitle="Change your name and time zone.">
      <form className="panel ac-form" onSubmit={onSubmit} aria-label="Edit account">
        <div className="rowhead">
          <h2>Account Info</h2>
        </div>
        <div className="pair">
          <div className="field">
            <label htmlFor="ac-name">Name</label>
            <input
              id="ac-name"
              className={`input${nameError ? ' bad' : ''}`}
              value={name}
              maxLength={100}
              autoComplete="name"
              onChange={(e) => {
                setName(e.target.value);
                setSaved(null);
                setError(null);
              }}
            />
            {nameError && <p className="errtext">{nameError}</p>}
          </div>
          <div className="field">
            <label htmlFor="ac-email">Email</label>
            <input id="ac-email" className="input" value={student.email} readOnly disabled />
          </div>
        </div>
        <div className="pair">
          <div className="field">
            <label htmlFor="ac-tz">Time zone</label>
            <select
              id="ac-tz"
              className="input"
              value={offset}
              onChange={(e) => {
                setOffset(e.target.value);
                setSaved(null);
                setError(null);
              }}
            >
              <option value={DEVICE}>Use this device's time zone ({formatUtcOffset(deviceOffset)})</option>
              {offsets.map((m) => (
                <option key={m} value={String(m)}>
                  {formatUtcOffset(m)}
                </option>
              ))}
            </select>
          </div>
        </div>
        <div className="why">
          Your study hours are local times. Changing your time zone shifts how they map to real times, so
          {' '}a 4 pm study block stays at 4 pm in the new zone. Regenerate your schedule after a change.
        </div>
        {offsetChanged && (
          <div className="alert" role="status">
            Time zone will change from {formatUtcOffset(student.utcOffsetMinutes)} to {formatUtcOffset(chosenOffset)}.
          </div>
        )}
        <div className="summary">
          {error && <p className="errtext" role="alert">{error}</p>}
          {saved && <p className="ac-note" role="status">{saved}</p>}
          <span style={{ flex: 1 }} />
          <Link to="/account" className="btn ac-btnlink">Cancel</Link>
          <button type="submit" className="btn primary" disabled={saving || !dirty || !!nameError}>
            {saving ? 'Saving…' : 'Save changes'}
          </button>
        </div>
      </form>
    </AccountShell>
  );
}
