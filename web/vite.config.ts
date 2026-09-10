import tailwindcss from '@tailwindcss/postcss';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';

/*
 * GitHub Pages serves a project repo under /<repo>/, so every asset URL and
 * every route the router builds needs that prefix. Vite publishes it to the
 * app as BASE_URL, which is what main.tsx hands the router and what
 * lib/matches.ts prefixes its fetches with, so the three can never disagree.
 * Unset means the domain root, which is where local development lives.
 */
const base = process.env.PAGES_BASE_PATH
  ? `${process.env.PAGES_BASE_PATH.replace(/\/$/, '')}/`
  : '/';

export default defineConfig({
  base,
  css: { postcss: { plugins: [tailwindcss()] } },
  plugins: [react()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('.', import.meta.url)).replace(/\/$/, ''),
    },
  },
  build: { outDir: 'dist', emptyOutDir: true },
});
