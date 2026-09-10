import type { RouteObject } from 'react-router';

import BotPage, { botLoader } from '@/pages/bot';
import HandsPage from '@/pages/hands';
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
 * The hand replay is registered everywhere, including on the published site.
 * It reads a local SQLite server holding all 4.62 million hands, so away from
 * that machine it has nothing to show -- but that is the same situation as a
 * local page with the server not started, and it is better answered by the
 * search with a note where the results go than by a route that does not
 * exist.
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
  { path: '/hands', element: <HandsPage /> },
  { path: '/hands/:matchId/:handIndex', element: <HandReplayPage /> },
  { path: '*', element: <NotFound /> },
];
