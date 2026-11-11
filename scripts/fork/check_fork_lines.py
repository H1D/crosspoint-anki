#!/usr/bin/env python3
"""List fork-added lines that a merged tree no longer contains.

usage: check_fork_lines.py <merge-base> <fork-ref> <merged-ref> [--allow FILE]

Every line the fork added since <merge-base> (git diff <merge-base> <fork-ref>)
must still appear somewhere in the same file at <merged-ref>, whitespace
ignored. Lines listed in the allow file ("path<TAB>line<TAB>reason") are
expected losses. Exit status 1 when an unexplained line is missing.
"""
import subprocess
import sys
from collections import Counter

TRIVIAL = {"", "{", "}", "};", "});", ")", ");", "else {", "} else {", "break;", "return;", "return false;",
           "return true;", "#endif", "default:", "*/", "/*", "//", "#", "---", "```", "-", "<br>", "</div>"}


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def norm(line):
    return "".join(line.split())


def added_lines(base, fork):
    out = {}
    path = None
    for line in git("diff", "-U0", "--no-renames", base, fork).splitlines():
        if line.startswith("+++ "):
            path = None if line == "+++ /dev/null" else line[6:]
        elif path and line.startswith("+") and not line.startswith("+++"):
            text = line[1:]
            if text.strip() not in TRIVIAL:
                out.setdefault(path, []).append(text)
    return out


def file_at(ref, path):
    try:
        return git("show", f"{ref}:{path}")
    except subprocess.CalledProcessError:
        pass
    # A submodule has no blob; its diff line is "Subproject commit <sha>".
    entry = git("ls-tree", ref, "--", path).split()
    if len(entry) >= 3 and entry[0] == "160000":
        return f"Subproject commit {entry[2]}\n"
    return None


def main():
    args = sys.argv[1:]
    allow = set()
    if "--allow" in args:
        i = args.index("--allow")
        for row in open(args[i + 1], encoding="utf-8"):
            if row.strip() and not row.startswith("#"):
                p, l, *_ = row.rstrip("\n").split("\t")
                allow.add((p, norm(l)))
        del args[i:i + 2]
    base, fork, merged = args
    missing_total = 0
    allowed_total = 0
    checked = 0
    for path, lines in sorted(added_lines(base, fork).items()):
        content = file_at(merged, path)
        if content is None:
            checked += len(lines)
            print(f"MISSING FILE {path} ({len(lines)} fork lines)")
            missing_total += len(lines)
            continue
        have = Counter(norm(l) for l in content.splitlines())
        want = Counter(norm(l) for l in lines)
        for key, n in want.items():
            checked += n
            short = n - have.get(key, 0)
            if short <= 0:
                continue
            if (path, key) in allow:
                allowed_total += short
                continue
            missing_total += short
            sample = next(l for l in lines if norm(l) == key)
            print(f"{path}\t{sample.strip()}" + (f"\t(x{short})" if short > 1 else ""))
    print(f"# checked {checked} fork lines: {missing_total} missing, {allowed_total} allowed", file=sys.stderr)
    return 1 if missing_total else 0


if __name__ == "__main__":
    sys.exit(main())
