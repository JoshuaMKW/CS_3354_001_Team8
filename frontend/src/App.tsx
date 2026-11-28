import type { ReactNode } from 'react';
import { Navigate, Route, Routes } from 'react-router-dom';
import { RequireAuth } from './auth/AuthContext';
import Login from './pages/Login';
import Signup from './pages/Signup';
import Home from './pages/Home';
import ViewSchedule from './pages/schedule/ViewSchedule';
import EditSchedule from './pages/schedule/EditSchedule';
import CreateSchedule from './pages/schedule/CreateSchedule';
import UploadSyllabus from './pages/schedule/UploadSyllabus';
import StudySettings from './pages/planner/StudySettings';
import AIAssistant from './pages/AIAssistant';
import Profile from './pages/account/Profile';
import EditAccount from './pages/account/EditAccount';
import Notifications from './pages/account/Notifications';

// Route table. Page agents own their page file (and its sibling .css); they should not need to edit this.
const authed = (el: ReactNode) => <RequireAuth>{el}</RequireAuth>;

export default function App() {
  return (
    <Routes>
      <Route path="/login" element={<Login />} />
      <Route path="/signup" element={<Signup />} />

      <Route path="/" element={authed(<Home />)} />

      <Route path="/schedule" element={authed(<ViewSchedule />)} />
      <Route path="/schedule/edit" element={authed(<EditSchedule />)} />
      <Route path="/schedule/create" element={authed(<CreateSchedule />)} />
      <Route path="/schedule/create/upload" element={authed(<UploadSyllabus />)} />

      <Route path="/planner" element={<Navigate to="/planner/settings" replace />} />
      <Route path="/planner/settings" element={authed(<StudySettings />)} />

      <Route path="/assistant" element={authed(<AIAssistant />)} />

      <Route path="/account" element={authed(<Profile />)} />
      <Route path="/account/edit" element={authed(<EditAccount />)} />
      <Route path="/account/notifications" element={authed(<Notifications />)} />

      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  );
}
