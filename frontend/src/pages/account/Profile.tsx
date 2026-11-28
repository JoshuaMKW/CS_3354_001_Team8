import { useState, type FormEvent } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { SignOutButton } from '../../components/Layout';
import { useAuth } from '../../auth/AuthContext';
import { ApiError, deleteMe, downloadScheduleIcs } from '../../api/client';
import { AccountShell, formatUtcOffset } from './AccountShell';
import './Profile.css';

export default function Profile() {
  const { student, setStudent } = useAuth();
  const navigate = useNavigate();

  const [exporting, setExporting] = useState(false);
  const [exportError, setExportError] = useState<string | null>(null);

  const [confirming, setConfirming] = useState(false);
  const [password, setPassword] = useState('');
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);

  async function exportIcs() {
    setExporting(true);
    setExportError(null);
    try {
      await downloadScheduleIcs();
    } catch (e) {
      setExportError(e instanceof ApiError ? e.message : 'Could not export your schedule.');
    } finally {
      setExporting(false);
    }
  }

  async function onDelete(e: FormEvent) {
    e.preventDefault();
    if (!password) {
      setDeleteError('Enter your password to delete your account.');
      return;
    }
    setDeleting(true);
    setDeleteError(null);
    try {
      await deleteMe(password);
      try {
        localStorage.removeItem(`cramberry.chats.${student?.id ?? ''}`);
      } catch {
        /* storage unavailable */
      }
      setStudent(null);
      navigate('/signup', { replace: true });
    } catch (err) {
      setDeleteError(
        err instanceof ApiError && err.status === 401 ? 'Password is incorrect' : err instanceof Error ? err.message : 'Could not delete your account.',
      );
      setDeleting(false);
    }
  }

  if (!student) return null;

  return (
    <AccountShell crumb="Profile" title="Profile" subtitle="Your account details, data and sign-in.">
      <section className="panel" aria-label="Account info">
        <div className="rowhead">
          <h2>Account Info</h2>
        </div>
        <ul className="ac-rows">
          <li>
            <span className="ac-k">Name</span>
            <span>{student.name}</span>
            <Link to="/account/edit" className="btn small ac-btnlink">Edit</Link>
          </li>
          <li>
            <span className="ac-k">Email</span>
            <span>{student.email}</span>
            <span className="req">Can't be changed</span>
          </li>
          <li>
            <span className="ac-k">Time zone</span>
            <span>{formatUtcOffset(student.utcOffsetMinutes)}</span>
            <Link to="/account/edit" className="btn small ac-btnlink">Edit</Link>
          </li>
        </ul>
      </section>

      <section className="panel" aria-label="Security and data">
        <div className="rowhead">
          <h2>Security and Data</h2>
        </div>
        <ul className="ac-rows">
          <li>
            <span className="ac-k">Calendar</span>
            <span>Download this week's study blocks as an .ics file for any calendar app.</span>
            <button type="button" className="btn small" onClick={() => void exportIcs()} disabled={exporting}>
              {exporting ? 'Exporting…' : 'Export my schedule'}
            </button>
          </li>
          <li>
            <span className="ac-k">Session</span>
            <span>Signed in as {student.email} on this device.</span>
            <SignOutButton />
          </li>
        </ul>
        {exportError && <p className="errtext">{exportError}</p>}
      </section>

      <section className="panel" aria-label="Account">
        <div className="rowhead">
          <h2>Account</h2>
        </div>
        {!confirming ? (
          <ul className="ac-rows">
            <li>
              <span className="ac-k">Delete</span>
              <span>Removes your courses, tasks, preferences, schedule and notifications for good.</span>
              <button type="button" className="btn small ac-danger" onClick={() => setConfirming(true)}>
                Delete Account
              </button>
            </li>
          </ul>
        ) : (
          <form className="ac-confirm" onSubmit={onDelete}>
            <p><strong>Delete your account?</strong> This can't be undone. Enter your password to confirm.</p>
            <div className="field ac-narrow">
              <label htmlFor="ac-password">Password</label>
              <input
                id="ac-password"
                className={`input${deleteError ? ' bad' : ''}`}
                type="password"
                autoComplete="current-password"
                value={password}
                autoFocus
                onChange={(e) => {
                  setPassword(e.target.value);
                  setDeleteError(null);
                }}
              />
              {deleteError && <p className="errtext" role="alert">{deleteError}</p>}
            </div>
            <div className="actions">
              <button
                type="button"
                className="btn"
                onClick={() => {
                  setConfirming(false);
                  setPassword('');
                  setDeleteError(null);
                }}
              >
                Cancel
              </button>
              <button type="submit" className="btn primary" disabled={deleting}>
                {deleting ? 'Deleting…' : 'Delete my account'}
              </button>
            </div>
          </form>
        )}
      </section>
    </AccountShell>
  );
}
