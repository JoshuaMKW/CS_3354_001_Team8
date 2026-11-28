import { useEffect, useRef, useState, type FormEvent, type KeyboardEvent } from 'react';
import { Link } from 'react-router-dom';
import { Layout, PageHead } from '../components/Layout';
import { useAuth } from '../auth/AuthContext';
import { ApiError, chat as askAssistant, generateSchedule, listCourses } from '../api/client';
import type { GenerateResponse } from '../api/types';
import { formatRange12 } from '../lib/time';
import { loadChats, newChatId, saveChats, titleFrom, type Chat, type ChatMessage } from './assistant/chatStore';
import { RichText } from './assistant/RichText';
import './AIAssistant.css';

const MAX_LEN = 2000;
const GENERATE_PROMPT = 'Generate a schedule for me';
const NEXT_PROMPT = 'Tell me what I have to do next';

interface Pending {
  chatId: string;
  label: string;
}

function describeGenerate(r: GenerateResponse): string {
  const n = r.schedule.length;
  const who = r.engine === 'local' ? 'the built-in planner' : r.engine;
  const lines = [`Done. I planned ${n} study block${n === 1 ? '' : 's'} for this week using ${who}.`];
  if (n) {
    lines.push('');
    for (const b of r.schedule) lines.push(`- ${b.day} ${formatRange12(b.start, b.end)}: ${b.title}`);
  }
  if (r.warnings.length) {
    lines.push('', 'Warnings:');
    for (const w of r.warnings) lines.push(`- ${w}`);
  }
  return lines.join('\n');
}

function relTime(ms: number): string {
  const mins = Math.round((Date.now() - ms) / 60000);
  if (mins < 1) return 'just now';
  if (mins < 60) return `${mins} min ago`;
  const h = Math.round(mins / 60);
  if (h < 24) return `${h} h ago`;
  return new Date(ms).toLocaleDateString('en-US', { month: 'short', day: 'numeric' });
}

export default function AIAssistant() {
  const { student } = useAuth();
  const studentId = student?.id ?? '';

  const [chats, setChats] = useState<Chat[]>(() => (studentId ? loadChats(studentId) : []));
  const [activeId, setActiveId] = useState<string | null>(() => chats[0]?.id ?? null);
  const [draft, setDraft] = useState('');
  const [pending, setPending] = useState<Pending | null>(null);
  const [error, setError] = useState<{ chatId: string | null; message: string } | null>(null);
  const [courseCode, setCourseCode] = useState<string | null>(null);
  const threadRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    if (studentId) saveChats(studentId, chats);
  }, [studentId, chats]);

  useEffect(() => {
    listCourses()
      .then((cs) => setCourseCode(cs.find((c) => c.code.trim())?.code.trim() ?? null))
      .catch(() => setCourseCode(null));
  }, []);

  const active = chats.find((c) => c.id === activeId) ?? null;
  const messages = active?.messages ?? [];
  const showPending = pending && pending.chatId === activeId;
  const visibleError = error && error.chatId === activeId ? error.message : null;

  useEffect(() => {
    const el = threadRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [messages.length, showPending, activeId]);

  /** Adds a message to a chat (creating it if needed) and moves it to the top of Recent chats. */
  function append(chatId: string, msg: ChatMessage) {
    setChats((list) => {
      const existing = list.find((c) => c.id === chatId);
      const updated: Chat = existing
        ? { ...existing, messages: [...existing.messages, msg], updatedAt: msg.at }
        : { id: chatId, title: titleFrom(msg.text), messages: [msg], updatedAt: msg.at };
      return [updated, ...list.filter((c) => c.id !== chatId)];
    });
  }

  function startTurn(text: string, label: string): string {
    const chatId = activeId ?? newChatId();
    setActiveId(chatId);
    setError(null);
    append(chatId, { role: 'user', text, at: Date.now() });
    setPending({ chatId, label });
    return chatId;
  }

  async function send(raw: string) {
    const text = raw.trim();
    if (!text || pending) return;
    if (text.length > MAX_LEN) {
      setError({ chatId: activeId, message: `Keep your message under ${MAX_LEN} characters.` });
      return;
    }
    setDraft('');
    const chatId = startTurn(text, 'Thinking…');
    try {
      const r = await askAssistant(text);
      append(chatId, { role: 'assistant', text: r.reply, at: Date.now() });
    } catch (e) {
      setError({ chatId, message: e instanceof Error ? e.message : 'The AI assistant is unavailable right now.' });
    } finally {
      setPending(null);
      inputRef.current?.focus();
    }
  }

  async function generate() {
    if (pending) return;
    const chatId = startTurn(GENERATE_PROMPT, 'Building your schedule… this can take up to 10 seconds.');
    try {
      const r = await generateSchedule();
      append(chatId, { role: 'assistant', text: describeGenerate(r), at: Date.now(), link: { to: '/schedule', label: 'View Schedule' } });
    } catch (e) {
      if (e instanceof ApiError && e.status === 422) {
        const details = e.details?.length ? `\n\n${e.details.map((d) => `- ${d}`).join('\n')}` : '';
        const needsPrefs = /preferences/i.test(e.message);
        append(chatId, {
          role: 'assistant',
          text: `I couldn't build a schedule: ${e.message}${details}`,
          at: Date.now(),
          link: needsPrefs ? { to: '/planner/settings', label: 'Set study settings' } : { to: '/schedule', label: 'View Schedule' },
        });
      } else {
        setError({ chatId, message: e instanceof Error ? e.message : 'Could not generate a schedule.' });
      }
    } finally {
      setPending(null);
    }
  }

  function newChat() {
    setActiveId(null);
    setDraft('');
    setError(null);
    inputRef.current?.focus();
  }

  function removeChat(id: string) {
    setChats((list) => list.filter((c) => c.id !== id));
    if (activeId === id) setActiveId(null);
    if (error?.chatId === id) setError(null);
  }

  function onSubmit(e: FormEvent) {
    e.preventDefault();
    void send(draft);
  }

  function onKeyDown(e: KeyboardEvent<HTMLTextAreaElement>) {
    if (e.key === 'Enter' && !e.shiftKey && !e.nativeEvent.isComposing) {
      e.preventDefault();
      void send(draft);
    }
  }

  const examPrompt = `Tell me about my ${courseCode ?? 'next'} exam`;
  const suggestions = [
    { text: GENERATE_PROMPT, note: 'Plans this week from your tasks and study hours.', run: () => void generate() },
    { text: NEXT_PROMPT, note: 'Picks the task to start with and why.', run: () => void send(NEXT_PROMPT) },
    { text: examPrompt, note: 'What is due, when, and how to prepare.', run: () => void send(examPrompt) },
  ];

  return (
    <Layout>
      <PageHead
        crumb={['AI Assistant']}
        title="AI assistant"
        subtitle="Ask about your tasks and schedule. The assistant can read them but never changes them."
      />
      <main className="ai-grid">
        <aside className="panel ai-side" aria-label="Chats">
          <button type="button" className="btn primary ai-new" onClick={newChat}>
            + New chat
          </button>
          <div className="rowhead ai-recent-head">
            <h2>Recent chats</h2>
            <span className="req">Saved on this device</span>
          </div>
          {chats.length === 0 ? (
            <p className="hint">No chats yet.</p>
          ) : (
            <ul className="list ai-recent">
              {chats.map((c) => (
                <li key={c.id} className={c.id === activeId ? 'ai-current' : ''}>
                  <button type="button" className="ai-chatlink" onClick={() => { setActiveId(c.id); setError(null); }} aria-current={c.id === activeId ? 'true' : undefined}>
                    <span>{c.title}</span>
                    <small>{relTime(c.updatedAt)}</small>
                  </button>
                  <button type="button" className="linkish ai-del" onClick={() => removeChat(c.id)} aria-label={`Delete chat "${c.title}"`} disabled={pending?.chatId === c.id}>
                    Delete
                  </button>
                </li>
              ))}
            </ul>
          )}
        </aside>

        <section className="panel ai-main" aria-label="Conversation">
          <div className="ai-thread" ref={threadRef} aria-live="polite">
            {messages.length === 0 && !showPending ? (
              <div className="ai-hello">
                <h2>Hello! What would you like to do today?</h2>
                <p className="hint">Try one of these to get started</p>
                <div className="ai-cards">
                  {suggestions.map((s) => (
                    <button key={s.text} type="button" className="ai-card" onClick={s.run} disabled={!!pending}>
                      <strong>{s.text}</strong>
                      <span className="ai-card-note">{s.note}</span>
                    </button>
                  ))}
                </div>
              </div>
            ) : (
              <>
                {messages.map((m, i) => (
                  <div key={i} className={`ai-msg ai-${m.role}`}>
                    <span className="ai-who">{m.role === 'user' ? 'You' : 'Cramberry'}</span>
                    <div className="ai-bubble">
                      {m.role === 'assistant' ? <RichText text={m.text} /> : <p>{m.text}</p>}
                      {m.link && (
                        <p className="ai-link">
                          <Link to={m.link.to} className="btn small ai-btnlink">{m.link.label}</Link>
                        </p>
                      )}
                    </div>
                  </div>
                ))}
                {showPending && (
                  <div className="ai-msg ai-assistant">
                    <span className="ai-who">Cramberry</span>
                    <div className="ai-bubble ai-pending" role="status">{pending!.label}</div>
                  </div>
                )}
              </>
            )}
          </div>

          {visibleError && (
            <div className="alert ai-alert" role="alert">
              {visibleError}
              <div className="actions">
                <button type="button" className="btn small" onClick={() => setError(null)}>Dismiss</button>
              </div>
            </div>
          )}

          {messages.length > 0 && (
            <div className="ai-chips" aria-label="Suggestions">
              <button type="button" className="btn small soft" onClick={() => void generate()} disabled={!!pending}>{GENERATE_PROMPT}</button>
              <button type="button" className="btn small" onClick={() => void send(NEXT_PROMPT)} disabled={!!pending}>{NEXT_PROMPT}</button>
            </div>
          )}

          <form className="ai-composer" onSubmit={onSubmit}>
            <label htmlFor="ai-input" className="sr-only">Message</label>
            <textarea
              id="ai-input"
              ref={inputRef}
              className="input"
              rows={2}
              maxLength={MAX_LEN}
              placeholder="Ask Cramberry anything..."
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
              onKeyDown={onKeyDown}
            />
            <button type="submit" className="btn primary" disabled={!draft.trim() || !!pending}>
              Send
            </button>
          </form>
          <p className="ai-foot">
            <span>Enter sends, Shift+Enter adds a line. The assistant only sees your tasks and schedule, not earlier messages.</span>
            <span>{draft.length}/{MAX_LEN}</span>
          </p>
        </section>
      </main>
    </Layout>
  );
}
