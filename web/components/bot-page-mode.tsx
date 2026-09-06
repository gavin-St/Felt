'use client';

import { useState, type ReactNode } from 'react';

import { Switch } from '@/components/ui/switch';

export function BotPageMode({ children }: { children: ReactNode }) {
  const [introMode, setIntroMode] = useState(false);

  return (
    <>
      {/* Just the switch. The label was the only thing on the page that talked
        * about the page rather than about the bot, and it is a control the
        * author uses, not something a reader needs named. */}
      <div
        className="mt-5 flex items-center justify-end"
        title="Video intro mode: hide performance stats and starting hands"
      >
        <Switch
          id="bot-intro-mode"
          checked={introMode}
          onCheckedChange={setIntroMode}
          aria-label="Video intro mode: hide bot performance stats and starting hands"
          className="opacity-40 transition-opacity hover:opacity-100 focus-visible:opacity-100 data-checked:bg-[#231f1b] data-checked:opacity-100"
        />
      </div>
      <div className={introMode ? 'bot-intro-mode' : undefined}>{children}</div>
    </>
  );
}
