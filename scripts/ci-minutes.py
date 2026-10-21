#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""CI minutes used, by platform (docs/ASSUMPTIONS.md, "CI cost policy").

Sums the GitHub Actions jobs of this repository: each job's time from start
to end, rounded up to whole minutes as GitHub bills it, grouped by platform,
with the list price and the included minutes it consumes. Reads the runs
through the GitHub CLI (`gh`), which must be logged in.

    scripts/ci-minutes.py                       # every run, by branch
    scripts/ci-minutes.py --branch milestone-4b # one branch (a milestone)
    scripts/ci-minutes.py --since 2026-10-21    # runs created on or after a date

The billing API of the account is the authority; this is the estimate the
milestone reports use (cancelled jobs count for the time they ran).
"""

import argparse
import collections
import datetime
import json
import math
import subprocess
import sys

# Per-minute list prices of GitHub-hosted standard runners for private
# repositories (since 2026-01-01), and the multiplier with which a minute
# consumes the included minutes. Recorded in docs/ASSUMPTIONS.md.
PLATFORMS = {
    "linux-x64": (0.006, 1),
    "linux-arm64": (0.005, 1),
    "windows-x64": (0.010, 2),
    "windows-arm64": (0.010, 2),
    "macos": (0.062, 10),
}


def gh(path, key):
    """The items under `key` of every page of a GitHub API list. Pages are
    fetched by number: the links `gh --paginate` follows use numeric
    repository paths, which some proxies refuse."""
    items, page = [], 1
    while True:
        separator = "&" if "?" in path else "?"
        out = subprocess.run(["gh", "api", f"{path}{separator}page={page}"], check=True, capture_output=True,
                             text=True).stdout
        batch = json.loads(out)[key]
        items += batch
        if len(batch) < 100:
            return items
        page += 1


def repository():
    out = subprocess.run(["gh", "repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"],
                         capture_output=True, text=True)
    if out.returncode == 0 and out.stdout.strip():
        return out.stdout.strip()
    url = subprocess.run(["git", "remote", "get-url", "origin"], check=True, capture_output=True, text=True).stdout
    return url.strip().removesuffix(".git").split("github.com")[-1].lstrip(":/").split("/git/")[-1]


def platform(job):
    labels = " ".join(job.get("labels") or []) + " " + (job.get("runner_name") or "")
    if "macos" in labels:
        return "macos"
    if "windows" in labels:
        return "windows-arm64" if "arm" in labels else "windows-x64"
    return "linux-arm64" if "arm" in labels else "linux-x64"


def parse_time(text):
    return datetime.datetime.fromisoformat(text.replace("Z", "+00:00"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--branch", help="only runs of this branch")
    parser.add_argument("--since", help="only runs created on or after this date (YYYY-MM-DD)")
    parser.add_argument("--repo", help="owner/name (default: this checkout's)")
    args = parser.parse_args()

    repo = args.repo or repository()
    query = f"repos/{repo}/actions/runs?per_page=100"
    if args.branch:
        query += f"&branch={args.branch}"
    if args.since:
        query += f"&created=>={args.since}"
    runs = gh(query, "workflow_runs")

    # (branch, platform) -> [jobs, minutes]
    totals = collections.defaultdict(lambda: [0, 0])
    for run in runs:
        if run["status"] != "completed":
            continue
        for job in gh(f"repos/{repo}/actions/runs/{run['id']}/jobs?per_page=100&filter=all", "jobs"):
            if job.get("conclusion") == "skipped" or not job.get("started_at") or not job.get("completed_at"):
                continue
            seconds = (parse_time(job["completed_at"]) - parse_time(job["started_at"])).total_seconds()
            if seconds <= 0:
                continue
            entry = totals[(run["head_branch"] or "?", platform(job))]
            entry[0] += 1
            entry[1] += math.ceil(seconds / 60)

    branches = sorted({branch for branch, _ in totals})
    print(f"{len(runs)} runs of {repo}" + (f" on {args.branch}" if args.branch else "")
          + (f" since {args.since}" if args.since else ""))
    print(f"{'branch':<24} {'platform':<14} {'jobs':>5} {'minutes':>8} {'included':>9} {'list $':>8}")
    grand = [0, 0, 0.0]
    for branch in branches:
        for name, (price, multiplier) in PLATFORMS.items():
            if (branch, name) not in totals:
                continue
            jobs, minutes = totals[(branch, name)]
            print(f"{branch:<24} {name:<14} {jobs:>5} {minutes:>8} {minutes * multiplier:>9} {minutes * price:>8.2f}")
            grand[0] += minutes
            grand[1] += minutes * multiplier
            grand[2] += minutes * price
    print(f"{'total':<24} {'':<14} {'':>5} {grand[0]:>8} {grand[1]:>9} {grand[2]:>8.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
