import type { RouteObject } from 'react-router';

import { HAND_REPLAY_ENABLED } from '@/lib/hands';
import BotPage, { botLoader } from '@/pages/bot';
import HandsPage, { Unavailable } from '@/pages/hands';
import HandReplayPage from '@/pages/hand-replay';
import MatchupPage, { matchupLoader } from '@/pages/matchup';
import MatrixPage from '@/pages/matrix';
import { NotFound } from '@/pages/not-found';
import PreflopPage from '@/pages/preflop';

/*
 * The whole site. Seven routes replaced a file-based router, an RSC runtime
 * and a Worker build, which is roughly the ratio this app was running at: it
 * used two things from the framework, next/link and notFound, and paid for a
 * server it never had anything for.
 *
 * The hand replay is registered only in development. It reads a local SQLite
 * server holding all 4.62 million hands, so on the published site there is
 * nothing behind it -- but the matchup pages link to the search, so that one
 * address answers either way, with the search or with an explanation.
 */
export const routes: RouteObject[] = [
  { path: '/', element: <MatrixPage /> },
  { path: '/preflop', element: <PreflopPage /> },
  { path: '/bot/:botId', element: <BotPage />, loader: botLoader },
  {
    path: '/matchup/:matchId/:botId',
    element: <MatchupPage />,
    loader: matchupLoader,
  },
  {
    path: '/hands',
    element: HAND_REPLAY_ENABLED ? <HandsPage /> : <Unavailable />,
  },
  ...(HAND_REPLAY_ENABLED
    ? [{ path: '/hands/:matchId/:handIndex', element: <HandReplayPage /> }]
    : []),
  { path: '*', element: <NotFound /> },
];
