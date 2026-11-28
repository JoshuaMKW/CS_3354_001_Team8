import type { ReactNode } from 'react';
import { NavLink } from 'react-router-dom';
import { useAuth } from '../auth/AuthContext';

/**
 * App chrome: a full-width `header.topbar` (its contents centred in `.topbar-inner`), then the
 * centred `.frame` that holds the page (PageHead, <main>, ...). At 1280px wide this renders like
 * the wireframes. NavLink sets aria-current="page" on the active top-level section.
 */
export function Layout({ children }: { children: ReactNode }) {
  const { student } = useAuth();
  return (
    <>
      <header className="topbar">
        <div className="topbar-inner">
          <span className="brand">Cramberry</span>
          <nav aria-label="Main">
            <NavLink to="/" end>Home</NavLink>
            <NavLink to="/schedule">Schedule</NavLink>
            <NavLink to="/planner">Study Planner</NavLink>
            <NavLink to="/assistant">AI Assistant</NavLink>
            <NavLink to="/account">Account</NavLink>
          </nav>
          <span className="who">{student?.name ?? ''}</span>
        </div>
      </header>
      <div className="frame">{children}</div>
    </>
  );
}

/** Chrome for signed-out pages (Login, Sign up): brand only, no nav. */
export function AuthLayout({ children }: { children: ReactNode }) {
  return (
    <>
      <header className="topbar">
        <div className="topbar-inner">
          <span className="brand">Cramberry</span>
        </div>
      </header>
      <div className="frame">{children}</div>
    </>
  );
}

export interface PageHeadProps {
  /** Breadcrumb parts, rendered as "Schedule > Edit Schedule". */
  crumb: string[];
  title: string;
  subtitle?: ReactNode;
  /** Right-side buttons, rendered inside `.actions`. */
  actions?: ReactNode;
}

/** `p.crumb` + `.pagehead` (h1, subtitle p, optional `.actions`), matching the wireframe markup. */
export function PageHead({ crumb, title, subtitle, actions }: PageHeadProps) {
  return (
    <>
      <p className="crumb">{crumb.join(' > ')}</p>
      <div className="pagehead">
        <div>
          <h1>{title}</h1>
          {subtitle != null && <p>{subtitle}</p>}
        </div>
        {actions != null && <div className="actions">{actions}</div>}
      </div>
    </>
  );
}

/** Unobtrusive `.linkish` sign-out control (used on the Account page). */
export function SignOutButton({ label = 'Sign out' }: { label?: string }) {
  const { logout } = useAuth();
  return (
    <button type="button" className="linkish" onClick={() => void logout()}>
      {label}
    </button>
  );
}
