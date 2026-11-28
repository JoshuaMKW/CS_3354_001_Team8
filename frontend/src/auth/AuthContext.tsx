import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
import { Navigate, useLocation, useNavigate } from 'react-router-dom';
import * as api from '../api/client';
import type { Student } from '../api/types';
import { browserUtcOffsetMinutes } from '../lib/time';

export interface AuthState {
  /** Signed-in student, or null. */
  student: Student | null;
  /** True while the boot-time /api/users/me check is running. */
  loading: boolean;
  /**
   * Set when /api/users/me failed for a reason other than 401 (server down, network).
   * The token is kept; RequireAuth shows it with a Retry button.
   */
  error: string | null;
  login(email: string, password: string): Promise<Student>;
  /** utcOffsetMinutes defaults to the browser's offset. */
  register(input: { name: string; email: string; password: string; utcOffsetMinutes?: number }): Promise<Student>;
  logout(): Promise<void>;
  /** Re-fetch /api/users/me. Only a 401 signs the student out. */
  refresh(): Promise<void>;
  /** Replace the cached student (e.g. after PATCH /api/users/me). */
  setStudent(student: Student | null): void;
}

const AuthContext = createContext<AuthState | null>(null);

/** The path (with query string) to come back to after signing in. Same shape RequireAuth uses. */
function currentFrom(): string {
  return window.location.pathname + window.location.search;
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [student, setStudent] = useState<Student | null>(null);
  const [loading, setLoading] = useState<boolean>(() => !!api.getToken());
  const [error, setError] = useState<string | null>(null);
  const navigate = useNavigate();

  /** Sign-out seen from outside (401, or another tab removed the token): go to /login and remember where we were. */
  const toLogin = useCallback(() => {
    setStudent(null);
    setError(null);
    if (window.location.pathname === '/login' || window.location.pathname === '/signup') return;
    navigate('/login', { replace: true, state: { from: currentFrom() } });
  }, [navigate]);

  useEffect(() => {
    api.setUnauthorizedHandler(toLogin);
  }, [toLogin]);

  const refresh = useCallback(async () => {
    if (!api.getToken()) {
      setStudent(null);
      setError(null);
      return;
    }
    try {
      setStudent(await api.getMe());
      setError(null);
    } catch (e) {
      if (e instanceof api.ApiError && e.status === 401) {
        // request() already cleared the token and called the unauthorized handler.
        setStudent(null);
        setError(null);
        return;
      }
      // 5xx or network failure: we don't know that the session is bad, so keep the token.
      setError(e instanceof api.ApiError ? e.message : "Couldn't reach Cramberry.");
    }
  }, []);

  useEffect(() => {
    if (!api.getToken()) return;
    refresh().finally(() => setLoading(false));
  }, [refresh]);

  // Sign-in or sign-out in another tab. `storage` only fires for changes made by other documents.
  useEffect(() => {
    function onStorage(e: StorageEvent) {
      if (e.key !== api.TOKEN_KEY && e.key !== null) return; // null: localStorage.clear()
      const token = api.getToken();
      if (!token) {
        toLogin();
        return;
      }
      if (e.key !== null && e.oldValue === e.newValue) return;
      // A different student (or a fresh session). Drop the old student first so RequireAuth unmounts
      // the pages, and nothing keeps writing under the previous student's id while /me loads.
      setStudent(null);
      setLoading(true);
      refresh().finally(() => setLoading(false));
    }
    window.addEventListener('storage', onStorage);
    return () => window.removeEventListener('storage', onStorage);
  }, [refresh, toLogin]);

  const value = useMemo<AuthState>(
    () => ({
      student,
      loading,
      error,
      async login(email, password) {
        const r = await api.login({ email, password });
        setError(null);
        setStudent(r.student);
        return r.student;
      },
      async register(input) {
        const r = await api.register({ utcOffsetMinutes: browserUtcOffsetMinutes(), ...input });
        setError(null);
        setStudent(r.student);
        return r.student;
      },
      async logout() {
        await api.logout().catch(() => undefined);
        setStudent(null);
        setError(null);
        navigate('/login', { replace: true });
      },
      refresh,
      setStudent,
    }),
    [student, loading, error, refresh, navigate],
  );

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used inside <AuthProvider>');
  return ctx;
}

/** Renders children only for a signed-in student; otherwise redirects to /login. */
export function RequireAuth({ children }: { children: ReactNode }) {
  const { student, loading, error, refresh } = useAuth();
  const location = useLocation();
  const [retrying, setRetrying] = useState(false);
  if (loading) return null;
  if (!student && error && api.getToken()) {
    return (
      <div className="frame">
        <header className="topbar">
          <span className="brand">Cramberry</span>
        </header>
        <main style={{ padding: '18px 22px' }}>
          <div className="alert" role="alert">
            Couldn't check your sign-in: {error}
            <div className="actions">
              <button
                type="button"
                className="btn small"
                disabled={retrying}
                onClick={() => {
                  setRetrying(true);
                  void refresh().finally(() => setRetrying(false));
                }}
              >
                {retrying ? 'Retrying...' : 'Retry'}
              </button>
            </div>
          </div>
        </main>
      </div>
    );
  }
  if (!student) return <Navigate to="/login" replace state={{ from: location.pathname + location.search }} />;
  return <>{children}</>;
}
