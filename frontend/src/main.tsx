import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { BrowserRouter } from 'react-router-dom';
import './styles/base.css'; // tokens, chrome and shared vocabulary (the wireframes' look, fluid)
import './styles/app.css'; // shared additions in the same vocabulary
import { AuthProvider } from './auth/AuthContext';
import App from './App';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BrowserRouter>
      <AuthProvider>
        <App />
      </AuthProvider>
    </BrowserRouter>
  </StrictMode>,
);
