import { useState, type FormEvent } from 'react';
import { Link, Navigate, useNavigate } from 'react-router-dom';
import { ApiError } from '../api/client';
import { useAuth } from '../auth/AuthContext';
import { AuthLayout } from '../components/Layout';
import { browserUtcOffsetMinutes } from '../lib/time';
import './Auth.css';

type Field = 'name' | 'email' | 'password';

export default function Signup() {
  const { student, loading, register } = useAuth();
  const navigate = useNavigate();
  const [name, setName] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [badField, setBadField] = useState<Field | null>(null);
  const [busy, setBusy] = useState(false);

  if (!loading && student) return <Navigate to="/" replace />;

  function guessField(message: string): Field | null {
    const m = message.toLowerCase();
    if (m.includes('password')) return 'password';
    if (m.includes('email')) return 'email';
    if (m.includes('name')) return 'name';
    return null;
  }

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setBadField(null);
    setBusy(true);
    try {
      await register({ name: name.trim(), email: email.trim(), password, utcOffsetMinutes: browserUtcOffsetMinutes() });
      navigate('/', { replace: true });
    } catch (err) {
      const message = err instanceof ApiError ? err.message : "Couldn't reach Cramberry. Try again.";
      setError(message);
      setBadField(guessField(message));
    } finally {
      setBusy(false);
    }
  }

  const cls = (f: Field) => `input${badField === f ? ' bad' : ''}`;
  const errFor = (f: Field) =>
    error && badField === f ? <p id={`signup-${f}-err`} className="errtext">{error}</p> : null;

  return (
    <AuthLayout>
      <main className="auth-wrap">
        <section className="panel auth-card" aria-label="Create an account">
          <h1>Sign up</h1>
          <p className="sub">Make an account to start planning.</p>
          <form onSubmit={onSubmit} noValidate>
            <div className="field">
              <label htmlFor="signup-name">Name</label>
              <input id="signup-name" className={cls('name')} autoComplete="name" value={name}
                aria-invalid={badField === 'name'} onChange={(e) => setName(e.target.value)} />
              {errFor('name')}
            </div>
            <div className="field">
              <label htmlFor="signup-email">Email</label>
              <input id="signup-email" className={cls('email')} type="email" autoComplete="email" value={email}
                aria-invalid={badField === 'email'} onChange={(e) => setEmail(e.target.value)} />
              {errFor('email')}
            </div>
            <div className="field">
              <label htmlFor="signup-password">Password</label>
              <input id="signup-password" className={cls('password')} type="password" autoComplete="new-password"
                value={password} aria-invalid={badField === 'password'} onChange={(e) => setPassword(e.target.value)} />
              {errFor('password') ?? <p className="hint">At least 8 characters, with a letter and a number.</p>}
            </div>
            {error && !badField && <p className="errtext" role="alert">{error}</p>}
            <button className="btn primary" type="submit" disabled={busy}>{busy ? 'Creating account...' : 'Create account'}</button>
          </form>
          <p className="switch">Already have an account? <Link to="/login">Sign in</Link></p>
        </section>
      </main>
    </AuthLayout>
  );
}
