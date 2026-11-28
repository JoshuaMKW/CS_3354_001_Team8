import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Dev server on :3000; /api is proxied to the Drogon backend so the browser
// never makes a cross-origin request.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    strictPort: true,
    proxy: {
      '/api': { target: 'http://127.0.0.1:8080', changeOrigin: false },
    },
  },
  preview: { port: 3000, strictPort: true },
});
