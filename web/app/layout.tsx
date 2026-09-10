import type { Metadata } from 'next';

import { STATIC_NAVIGATION } from '@/lib/static-navigation';
import './globals.css';

export const metadata: Metadata = {
  title: 'Felt — Bot Matchups',
  description: 'Head-to-head results for Felt poker bots.',
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <head>
        {/* Why the client router is bypassed: see lib/static-navigation.ts. */}
        {process.env.NODE_ENV === 'production' && (
          <script dangerouslySetInnerHTML={{ __html: STATIC_NAVIGATION }} />
        )}
      </head>
      <body>{children}</body>
    </html>
  );
}
