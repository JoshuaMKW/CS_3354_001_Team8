// Clickable versions of the wireframe's `.seg` toggle and `.check`/`.box` checkbox.
import type { ReactNode } from 'react';
import './shared.css';

export interface SegProps<T extends string | number> {
  label: string;
  options: { value: T; label: string }[];
  value: T;
  onChange(value: T): void;
  disabled?: boolean;
  className?: string;
}

export function Seg<T extends string | number>({ label, options, value, onChange, disabled, className }: SegProps<T>) {
  return (
    <div className={`seg vs-seg${className ? ` ${className}` : ''}`} role="radiogroup" aria-label={label}>
      {options.map((o) => (
        <button
          key={String(o.value)}
          type="button"
          role="radio"
          aria-checked={o.value === value}
          className={o.value === value ? 'on' : undefined}
          disabled={disabled}
          onClick={() => o.value !== value && onChange(o.value)}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function Check({
  checked,
  onChange,
  children,
  label,
}: {
  checked: boolean;
  onChange(checked: boolean): void;
  children?: ReactNode;
  /** Accessible name when there are no visible children. */
  label?: string;
}) {
  return (
    <button
      type="button"
      role="checkbox"
      aria-checked={checked}
      aria-label={label}
      className="check vs-check"
      onClick={() => onChange(!checked)}
    >
      <span className={`box${checked ? ' on' : ''}`} aria-hidden="true">
        {checked ? 'x' : ''}
      </span>
      {children != null && <span>{children}</span>}
    </button>
  );
}
