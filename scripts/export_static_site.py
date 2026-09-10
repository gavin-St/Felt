#!/usr/bin/env python3
"""Crawl the built site into a folder GitHub Pages can serve.

The web app renders on a server, so there is no static build to point Pages
at. There does not need to be: every page it can show is already decided by
web/data/dashboard.json, so the whole site is a finite list of URLs. Run the
app, ask it for each one, and write the answer to disk.

The hand replay is not in the list. It reads a local SQLite server that will
not exist on Pages, and the app already disables it in a production build.

    npm --prefix web run build
    npm --prefix web start &          # serves on 127.0.0.1:8787
    python3 scripts/export_static_site.py --out site
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import urllib.error
import urllib.request
from pathlib import Path

ASSET_PREFIXES = ("/_next/", "/assets/", "/favicon", "/static/")


def routes(dashboard: dict) -> list[str]:
    paths = ["/", "/preflop"]
    for bot in dashboard["ratings"]:
        paths.append(f"/bot/{bot['bot_id']}")
    for result in dashboard["matrix"]:
        paths.append(f"/matchup/{result['match_id']}/{result['bot_id']}")
    # The one page no listing links to.
    paths.append("/bot/secret")
    seen, unique = set(), []
    for path in paths:
        if path not in seen:
            seen.add(path)
            unique.append(path)
    return unique


def destination(out: Path, path: str) -> Path:
    if path == "/":
        return out / "index.html"
    return out / path.strip("/") / "index.html"


# Written by the Workers build for Cloudflare to read; Pages has no use for
# either, and _headers would be one more underscore folder to explain.
WORKER_ONLY = {"_headers", ".assetsignore", ".vite"}


def copy_assets(source: Path, out: Path, skip: str | None = None) -> None:
    for entry in source.iterdir():
        if entry.name in WORKER_ONLY or entry.name == skip:
            continue
        target = out / entry.name
        if entry.is_dir():
            shutil.copytree(entry, target, dirs_exist_ok=True)
        else:
            shutil.copy2(entry, target)


def fetch(base: str, path: str) -> bytes:
    with urllib.request.urlopen(f"{base}{path}", timeout=30) as response:
        if response.status != 200:
            raise RuntimeError(f"{path} returned {response.status}")
        return response.read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="http://127.0.0.1:8787")
    parser.add_argument("--base-path", default="",
                        help="the prefix the built site is served under, if "
                             "any: /Felt for a project repo on Pages. Pages "
                             "are fetched with it and written without, since "
                             "the output folder is what lives at that prefix.")
    parser.add_argument("--out", type=Path, default=Path("site"))
    parser.add_argument("--dashboard", type=Path,
                        default=Path("web/data/dashboard.json"))
    parser.add_argument("--assets", type=Path, default=Path("web/dist/client"),
                        help="built client assets, copied in as they are")
    arguments = parser.parse_args()

    dashboard = json.loads(arguments.dashboard.read_text())
    paths = routes(dashboard)
    out = arguments.out
    out.mkdir(parents=True, exist_ok=True)

    if arguments.assets.is_dir():
        prefix = arguments.base_path.strip("/")
        nested = arguments.assets / prefix if prefix else None
        # The build already writes the client assets under the base path, so
        # dist/client holds Felt/_next/... while the pages ask for
        # /Felt/_next/... The output folder IS what gets served at /Felt, so
        # that level has to come off here; copying it as-is puts every asset
        # at /Felt/Felt/_next/..., which is a 404 for the stylesheet and every
        # script, and the site comes up as unstyled HTML.
        copy_assets(arguments.assets, out, skip=prefix or None)
        if nested is not None and nested.is_dir():
            copy_assets(nested, out)
        print(f"copied assets from {arguments.assets}")

    failures = []
    for index, path in enumerate(paths, 1):
        try:
            body = fetch(arguments.base, f"{arguments.base_path}{path}")
        except (urllib.error.URLError, RuntimeError) as error:
            failures.append((path, str(error)))
            continue
        target = destination(out, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
        if index % 100 == 0 or index == len(paths):
            print(f"  {index}/{len(paths)} pages", flush=True)

    # Pages runs Jekyll by default, and Jekyll hides folders that start with
    # an underscore -- which is where the framework puts every asset.
    (out / ".nojekyll").write_text("")

    print(f"wrote {len(paths) - len(failures)} pages to {out}")
    for path, error in failures:
        print(f"  FAILED {path}: {error}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
