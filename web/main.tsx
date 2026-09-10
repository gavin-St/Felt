import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { RouterProvider, createBrowserRouter } from 'react-router';

import { routes } from '@/routes';
import './globals.css';

/*
 * GitHub Pages serves a project repo under /<repo>/, so the router has to be
 * told where the site starts or every path it builds would be one level too
 * high. Vite already knows -- it is the `base` the build was given -- and
 * publishes it as BASE_URL, so the two can never drift apart.
 */
const router = createBrowserRouter(routes, {
  basename: import.meta.env.BASE_URL,
});

const container = document.getElementById('root');
if (!container) throw new Error('index.html is missing #root');

createRoot(container).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
);
