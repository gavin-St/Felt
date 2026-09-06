'use client';

import { useState, type ReactNode } from 'react';

import { Switch } from '@/components/ui/switch';

export function BotPageMode({ children }: { children: ReactNode }) {
  const [introMode, setIntroMode] = useState(false);

  return (
    <>
      <div className="mt-5 flex items-center justify-end gap-3 font-mono text-xs text-[#756b60]">
        <label htmlFor="bot-intro-mode" className="cursor-pointer">
          Video intro mode
        </label>
        <Switch
          id="bot-intro-mode"
          checked={introMode}
          onCheckedChange={setIntroMode}
          aria-label="Hide bot performance stats and starting hands"
          className="data-checked:bg-[#231f1b]"
        />
      </div>
      <div className={introMode ? 'bot-intro-mode' : undefined}>{children}</div>
    </>
  );
}
