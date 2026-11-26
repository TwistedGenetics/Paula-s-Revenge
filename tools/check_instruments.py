#!/usr/bin/env python3
"""Have the instruments copied in from Jamie's other repos been updated?

    python3 tools/check_instruments.py              report (Markdown) on every instrument in third_party/instruments.json
    python3 tools/check_instruments.py --update X   copy instrument X's newest code in (X = choppage, helix, crafty)

The report says which repos have new commits that touch what Paula copied (or the AU/VST plug-in build, which is
what Paula should match), and lists them. CI runs it every day and keeps a GitHub issue up to date
(.github/workflows/instrument-updates.yml).

--update clones the repo, copies the newest files over Paula's copies (renaming and rewriting as the manifest says)
and moves the commit forward. Files Paula changed on purpose ('patched') are kept as they are; if the repo changed
one of them, its new version is written beside Paula's as <file>.upstream to merge by hand, and it says so.
Build, run the tests and look at the diff before committing.
"""
import json, os, re, shutil, subprocess, sys, tempfile, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "third_party", "instruments.json")


def api(path):
    req = urllib.request.Request("https://api.github.com/" + path, headers={"Accept": "application/vnd.github+json"})
    tok = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if tok:
        req.add_header("Authorization", "Bearer " + tok)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.load(r)
    except Exception:
        if shutil.which("gh") is None:
            raise
        r = subprocess.run(["gh", "api", path], capture_output=True, text=True)   # (gh knows the way through a proxy)
        if r.returncode != 0:
            raise RuntimeError(r.stderr.strip()[:200])
        return json.loads(r.stdout)


def watched(inst):
    w = [src for src, _ in inst["copy"]] + inst.get("watch", [])
    if inst.get("plugin"):
        w.append(inst["plugin"])
    return [x.rstrip("/") + "/" if not os.path.splitext(x)[1] else x for x in w]


def touches(f, inst):
    return any(f == w or f.startswith(w) for w in watched(inst))


def check(key, inst):
    repo = inst["repo"]
    info = api(f"repos/{repo}")
    head = api(f"repos/{repo}/commits/{info['default_branch']}")["sha"]
    if head == inst["commit"]:
        return None
    cmp = api(f"repos/{repo}/compare/{inst['commit']}...{head}")
    files = [f["filename"] for f in cmp.get("files", [])]
    hit = [f for f in files if touches(f, inst)]
    commits = [(c["sha"][:7], c["commit"]["message"].splitlines()[0]) for c in cmp.get("commits", [])]
    return {"head": head, "files": hit, "plugin": [f for f in hit if inst.get("plugin") and f.startswith(inst["plugin"] + "/")],
            "patched": [f for f in hit if f in inst.get("patched", [])], "commits": commits, "ahead": cmp.get("ahead_by", len(commits))}


def report():
    insts = {k: v for k, v in json.load(open(MANIFEST)).items() if not k.startswith("_")}
    out, any_update, failed = [], False, False
    for key, inst in insts.items():
        try:
            r = check(key, inst)
        except Exception as e:  # network, rate limit...
            out.append(f"- **{inst['name']}** ({inst['repo']}): could not check ({e})")
            failed = True
            continue
        if r is None or not r["files"]:
            out.append(f"- **{inst['name']}** {inst['version']}: up to date" + (" (its newer commits don't touch what Paula uses)" if r else ""))
            continue
        any_update = True
        out.append(f"- **{inst['name']}** {inst['version']} → **{r['ahead']} new commit(s)** in {inst['repo']} that change what Paula uses "
                   f"({len(r['files'])} file(s){', the AU/VST plug-in build too' if r['plugin'] else ''}). "
                   f"Update with `python3 tools/check_instruments.py --update {key}`.")
        for sha, msg in r["commits"][:20]:
            out.append(f"  - `{sha}` {msg}")
        if r["patched"]:
            out.append(f"  - Changes files Paula patched on purpose, merge by hand: {', '.join(r['patched'])}")
    head = "## Built-in instruments from your other repos\n\n" + ("Updates are waiting:\n\n" if any_update else "Some could not be checked:\n\n" if failed else "Everything is up to date.\n\n")
    print(head + "\n".join(out))
    return any_update


def update(key):
    data = json.load(open(MANIFEST))
    inst = data[key]
    tmp = tempfile.mkdtemp(prefix="prv-inst-")
    src = os.path.join(tmp, "repo")
    subprocess.run(["git", "clone", "-q", f"https://github.com/{inst['repo']}.git", src], check=True)
    head = subprocess.run(["git", "-C", src, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    old = os.path.join(tmp, "old")
    subprocess.run(["git", "-C", src, "worktree", "add", "-q", "--detach", old, inst["commit"]], check=True)
    renames, rewrites, patched = inst.get("rename", []), inst.get("rewrite", []), set(inst.get("patched", []))
    notes = []

    def dest_name(rel):
        for a, b in renames:
            rel = rel.replace(a, b)
        return rel

    for s, d in inst["copy"]:
        sdir, ddir = os.path.join(src, s), os.path.join(ROOT, d)
        new_files = set()
        for base, _, names in os.walk(sdir):
            for n in names:
                full = os.path.join(base, n)
                rel = os.path.relpath(full, sdir)
                repo_rel = s + "/" + rel
                target = os.path.join(ddir, dest_name(rel))
                new_files.add(os.path.normpath(target))
                os.makedirs(os.path.dirname(target), exist_ok=True)
                if repo_rel in patched:
                    o = os.path.join(old, repo_rel)
                    same = os.path.exists(o) and open(o, "rb").read() == open(full, "rb").read()
                    if not same:
                        shutil.copyfile(full, target + ".upstream")
                        notes.append(f"{dest_name(repo_rel)}: the repo changed a file Paula patched; its new version is in {os.path.relpath(target, ROOT)}.upstream")
                    continue
                data_b = open(full, "rb").read()
                if rewrites and n.endswith((".h", ".cpp", ".c", ".hpp")):
                    t = data_b.decode("utf-8", "surrogateescape")
                    for a, b in rewrites:
                        t = t.replace(a, b)
                    data_b = t.encode("utf-8", "surrogateescape")
                open(target, "wb").write(data_b)
        # files the repo deleted
        for base, _, names in os.walk(ddir):
            for n in names:
                p = os.path.normpath(os.path.join(base, n))
                if p not in new_files and not n.endswith((".upstream", ".md")):
                    oldrel = os.path.relpath(p, ddir)
                    if os.path.exists(os.path.join(old, s, oldrel)) or any(os.path.exists(os.path.join(old, s, oldrel.replace(b, a))) for a, b in renames):
                        os.remove(p)
                        notes.append(f"removed {os.path.relpath(p, ROOT)} (gone from the repo)")

    for step in inst.get("generate", []):
        *args, outname = step
        os.makedirs(os.path.join(ROOT, inst["generated_into"]), exist_ok=True)
        subprocess.run([sys.executable, *args, os.path.join(ROOT, inst["generated_into"], outname)], cwd=src, check=True)

    vf, pat = inst["version_from"]
    m = re.search(pat, open(os.path.join(src, vf)).read())
    inst["commit"], inst["version"] = head, (m.group(1) if m else inst["version"])
    open(MANIFEST, "w").write(json.dumps(data, indent=2) + "\n")
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"{inst['name']} is now {inst['version']} ({head[:7]}).")
    for n in notes:
        print("  " + n)
    print("Now build, run the engine tests and check the diff (git diff --stat).")


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--update":
        update(sys.argv[2])
    else:
        found = report()
        if "--exit-code" in sys.argv:
            sys.exit(1 if found else 0)
