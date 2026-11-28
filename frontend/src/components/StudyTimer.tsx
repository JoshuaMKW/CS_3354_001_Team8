import { useEffect, useRef } from 'react';
// Study timer (FR-25) by Risha Mothukuru (RM): mountStudyTimer.mjs draws it and
// lib/studyTimer.mjs runs the countdown. This component mounts it on Home.
import { mountStudyTimer } from './mountStudyTimer.mjs';
import './StudyTimer.css';

export function StudyTimer() {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!ref.current) return;
    return mountStudyTimer(ref.current);
  }, []);
  return <div ref={ref} className="panel study-timer" />;
}
