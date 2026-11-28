import { useState, type FormEvent } from 'react';
import { Link, Navigate, useLocation, useNavigate } from 'react-router-dom';
import { ApiError } from '../api/client';
import { useAuth } from '../auth/AuthContext';
import { AuthLayout } from '../components/Layout';
import './Auth.css';

export default function Login() {
  const { student, loading, login } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();
  const from = (location.state as { from?: string } | null)?.from ?? '/';
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  if (!loading && student) return <Navigate to={from} replace />;

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    if (!email.trim() || !password) {
      setError('Enter your email and password.');
      return;
    }
    setBusy(true);
    try {
      await login(email.trim(), password);
      navigate(from, { replace: true });
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Couldn't reach Cramberry. Try again.");
    } finally {
      setBusy(false);
    }
  }

  return (
    <AuthLayout>
      <main className="auth-wrap">
        <section className="panel auth-card" aria-label="Sign in">
          <h1>Sign in</h1>
          <p className="sub">Welcome back to Cramberry.</p>
          <form onSubmit={onSubmit} noValidate>
            <div className="field">
              <label htmlFor="login-email">Email</label>
              <input id="login-email" className={`input${error ? ' bad' : ''}`} type="email" autoComplete="email"
                value={email} onChange={(e) => setEmail(e.target.value)} />
            </div>
            <div className="field">
              <label htmlFor="login-password">Password</label>
              <input id="login-password" className={`input${error ? ' bad' : ''}`} type="password"
                autoComplete="current-password" value={password} onChange={(e) => setPassword(e.target.value)} />
            </div>
            {error && <p className="errtext" role="alert">{error}</p>}
            <button className="btn primary" type="submit" disabled={busy}>{busy ? 'Signing in...' : 'Sign in'}</button>
          </form>
          <p className="switch">New to Cramberry? <Link to="/signup">Create an account</Link></p>
        </section>
      </main>
    </AuthLayout>
  );
}
