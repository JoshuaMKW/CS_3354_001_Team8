import type { ReactNode } from 'react';

// Renders assistant text with line breaks, "- " / "* " / "1. " lists and **bold**.
// Everything goes through React text nodes, so no HTML from the reply is ever injected.

function inline(text: string): ReactNode[] {
  return text.split(/(\*\*[^*]+\*\*)/g).map((part, i) =>
    part.startsWith('**') && part.endsWith('**') && part.length > 4 ? <strong key={i}>{part.slice(2, -2)}</strong> : part,
  );
}

export function RichText({ text }: { text: string }) {
  const out: ReactNode[] = [];
  let bullets: string[] = [];
  let ordered = false;

  const flush = () => {
    if (!bullets.length) return;
    const items = bullets.map((b, i) => <li key={i}>{inline(b)}</li>);
    out.push(ordered ? <ol key={out.length}>{items}</ol> : <ul key={out.length}>{items}</ul>);
    bullets = [];
  };

  for (const raw of text.replace(/\r\n/g, '\n').split('\n')) {
    const line = raw.trimEnd();
    const ul = /^\s*[-*•]\s+(.*)$/.exec(line);
    const ol = /^\s*\d+[.)]\s+(.*)$/.exec(line);
    if (ul || ol) {
      const isOl = !ul;
      if (bullets.length && isOl !== ordered) flush();
      ordered = isOl;
      bullets.push((ul ?? ol)![1]);
      continue;
    }
    flush();
    if (!line.trim()) continue;
    const heading = /^#{1,6}\s+(.*)$/.exec(line);
    out.push(<p key={out.length}>{heading ? <strong>{inline(heading[1])}</strong> : inline(line)}</p>);
  }
  flush();
  return <>{out}</>;
}
