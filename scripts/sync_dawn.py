#!/usr/bin/env python3
"""Pin the emdawnwebgpu package from google/dawn GitHub Releases in cmake/dawn-lock.json."""
import argparse
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

API = "https://api.github.com/repos/google/dawn/releases"
LOCK = Path(__file__).resolve().parent.parent / "cmake" / "dawn-lock.json"
ASSET_RE = re.compile(r"^emdawnwebgpu_pkg-.*\.zip$")


def http_get(url):
    request = urllib.request.Request(url, headers={"User-Agent": "sync_dawn", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(request) as response:
        return response.read()


def release_entry(release):
    for asset in release.get("assets", []):
        if ASSET_RE.match(asset["name"]):
            return {"tag": release["tag_name"], "asset": asset["name"], "url": asset["browser_download_url"]}
    return None


def find_release(tag):
    page = 1
    while page <= 10:
        releases = json.loads(http_get(f"{API}?per_page=100&page={page}"))
        if not releases:
            break
        for release in releases:
            if tag is not None and release["tag_name"] != tag:
                continue
            entry = release_entry(release)
            if entry is not None:
                return entry
        page += 1
    raise SystemExit(f"No release with an emdawnwebgpu_pkg-*.zip asset found (tag={tag})")


def with_hash(entry):
    entry["sha256"] = hashlib.sha256(http_get(entry["url"])).hexdigest()
    return entry


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="compare the lock file with the latest release")
    parser.add_argument("--upgrade", action="store_true", help="pin the latest release")
    parser.add_argument("--tag", help="pin this release tag")
    args = parser.parse_args()

    if args.check:
        latest = find_release(None)
        current = json.loads(LOCK.read_text(encoding="utf-8"))
        problems = []
        for key in ("tag", "url"):
            if current.get(key) != latest[key]:
                problems.append(f"{key}: locked {current.get(key)} latest {latest[key]}")
        if not problems:
            latest = with_hash(latest)
            if current.get("sha256") != latest["sha256"]:
                problems.append(f"sha256: locked {current.get('sha256')} latest {latest['sha256']}")
        if problems:
            print("dawn-lock.json is out of date:\n  " + "\n  ".join(problems))
            return 1
        print(f"dawn-lock.json is current ({current['tag']})")
        return 0

    if args.upgrade or args.tag:
        entry = with_hash(find_release(args.tag))
        data = {k: entry[k] for k in ("tag", "asset", "url", "sha256")}
        LOCK.parent.mkdir(parents=True, exist_ok=True)
        LOCK.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"Pinned {data['tag']} sha256={data['sha256']}")
        return 0

    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
