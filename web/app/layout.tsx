import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'Felt — Bot Matchups',
  description: 'Head-to-head results for Felt poker bots.',
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
