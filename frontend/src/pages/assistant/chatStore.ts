// Per-student chat history, kept in localStorage under cramberry.chats.<studentId>.
// The backend never sees this history: each chat request carries only the new message.

export interface ChatLink {
  to: string;
  label: string;
}

export interface ChatMessage {
  role: 'user' | 'assistant';
  text: string;
  at: number;
  link?: ChatLink;
}

export interface Chat {
  id: string;
  /** The first user message (trimmed). */
  title: string;
  messages: ChatMessage[];
  updatedAt: number;
}

export const chatsKey = (studentId: string) => `cramberry.chats.${studentId}`;

function isChat(c: unknown): c is Chat {
  if (!c || typeof c !== 'object') return false;
  const o = c as Record<string, unknown>;
  return typeof o.id === 'string' && typeof o.title === 'string' && Array.isArray(o.messages) && typeof o.updatedAt === 'number';
}

export function loadChats(studentId: string): Chat[] {
  try {
    const raw = localStorage.getItem(chatsKey(studentId));
    if (!raw) return [];
    const parsed: unknown = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed.filter(isChat).sort((a, b) => b.updatedAt - a.updatedAt) : [];
  } catch {
    return [];
  }
}

export function saveChats(studentId: string, chats: Chat[]): void {
  try {
    localStorage.setItem(chatsKey(studentId), JSON.stringify(chats.slice(0, 50)));
  } catch {
    /* storage full or unavailable: history lasts for this page only */
  }
}

export function newChatId(): string {
  try {
    return crypto.randomUUID();
  } catch {
    return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
  }
}

export function titleFrom(text: string): string {
  const t = text.replace(/\s+/g, ' ').trim();
  return t.length > 60 ? `${t.slice(0, 57)}…` : t;
}
