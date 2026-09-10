/*
 * The deployed site is a folder of static HTML, and vinext's client router
 * cannot navigate in it. Its link chunk destructures
 * getPrefetchInterceptionContext and a dozen siblings out of the app-router
 * chunk, which in a production build exports none of those names -- so every
 * <Link> logs "RSC prefetch setup error: f is not a function" the moment it
 * scrolls into view, and a click calls preventDefault() and then dies before
 * it reaches the fetch. No request, no navigation, nothing in the console but
 * the prefetch error. It is not a hosting problem: the failure happens before
 * anything is sent, so it would do the same behind any server.
 *
 * So this build does not use the router. The script below watches clicks on
 * window, after everything else has seen them, and navigates to the anchor's
 * href itself. The hrefs in the markup are already right -- Link renders the
 * base path into them correctly, it is only the interception that is broken --
 * so the browser lands on the page the router was trying to reach, and every
 * page of this site is a file the host can serve.
 *
 * It runs late, in the bubble phase, on purpose. Claiming the click earlier
 * would be tidier but would stop React's own handler from firing, and some
 * links do real work there: StepLink counts each press toward unlocking the
 * secret bot. So the click is left to run its course, and the navigation the
 * router failed to perform happens afterwards. That means ignoring
 * defaultPrevented, which the router sets on its way down -- the trade is
 * that an anchor which cancels its own navigation on purpose would navigate
 * anyway, and this site has none.
 *
 * Modified and new-tab clicks are left entirely alone, since those belong to
 * the browser. Nothing here runs in development, where the router works.
 */
export const STATIC_NAVIGATION = `
window.addEventListener('click', function (event) {
  if (event.button !== 0) return;
  if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
  var node = event.target;
  var anchor = node && node.closest ? node.closest('a[href]') : null;
  if (!anchor || (anchor.target && anchor.target !== '_self')) return;
  if (anchor.hasAttribute('download')) return;
  var url;
  try { url = new URL(anchor.href, location.href); } catch (error) { return; }
  if (url.origin !== location.origin) return;
  if (url.href === location.href) return;
  window.location.assign(url.href);
}, false);
`.trim();
