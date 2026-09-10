import type { NextConfig } from 'next';

/*
 * GitHub Pages serves a project repo under /<repo>/, so every absolute link
 * and asset URL needs that prefix. The build reads it from the environment,
 * which keeps local development at the root where it belongs.
 */
const nextConfig: NextConfig = {
  basePath: process.env.PAGES_BASE_PATH || undefined,
};

export default nextConfig;
