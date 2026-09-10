import type { NextConfig } from 'next';

/*
 * GitHub Pages serves a project repo under /<repo>/, so every absolute link
 * and asset URL needs that prefix. The build reads it from the environment,
 * which keeps local development at the root where it belongs.
 */
const developing = process.env.NODE_ENV !== 'production';

const nextConfig: NextConfig = {
  basePath: process.env.PAGES_BASE_PATH || undefined,
  /*
   * The published site is a folder of files, so the build renders every route
   * to disk rather than leaving them to a server. This replaced crawling a
   * running Worker for each of the 844 URLs: the export knows the route list
   * from generateStaticParams, and it writes the RSC payloads beside the HTML,
   * which a crawler could never produce.
   */
  output: 'export',
  /*
   * Pages named page.dev.tsx exist only in development. The hand replay is the
   * one part of this app that is not a static site -- it reads a local SQLite
   * server over HTTP -- so its per-hand route would have nothing to export and
   * no way to answer. The search page itself stays, and says so.
   */
  pageExtensions: developing ? ['dev.tsx', 'dev.ts', 'tsx', 'ts'] : ['tsx', 'ts'],
};

export default nextConfig;
