import type { ReactNode } from 'react';
import { NavLink } from 'react-router-dom';
import { Layout, PageHead } from '../../components/Layout';
import './Account.css';

const SECTIONS = [
  { to: '/account', label: 'Profile' },
  { to: '/account/edit', label: 'Edit Account' },
  { to: '/account/notifications', label: 'Notifications' },
];

/** Account page chrome: PageHead + left "Account Sections" panel + right content column. */
export function AccountShell(props: { crumb: string; title: string; subtitle: string; children: ReactNode }) {
  return (
    <Layout>
      <PageHead crumb={['Account', props.crumb]} title={props.title} subtitle={props.subtitle} />
      <main className="ac-grid">
        <nav className="panel ac-sections" aria-label="Account sections">
          <h2>Account Sections</h2>
          {SECTIONS.map((s) => (
            <NavLink key={s.to} to={s.to} end className={({ isActive }) => `btn ac-section${isActive ? ' soft on' : ''}`}>
              {s.label}
            </NavLink>
          ))}
        </nav>
        <div className="stack">{props.children}</div>
      </main>
    </Layout>
  );
}

/** −360 → "UTC−06:00" (true minus sign), 330 → "UTC+05:30". */
export function formatUtcOffset(minutes: number): string {
  const sign = minutes < 0 ? '−' : '+';
  const abs = Math.abs(minutes);
  return `UTC${sign}${String(Math.floor(abs / 60)).padStart(2, '0')}:${String(abs % 60).padStart(2, '0')}`;
}

/** −12:00 … +14:00 in 30-minute steps. */
export const UTC_OFFSETS: number[] = Array.from({ length: (840 + 720) / 30 + 1 }, (_, i) => -720 + i * 30);
