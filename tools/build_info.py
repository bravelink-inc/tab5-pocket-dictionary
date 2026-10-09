#!/usr/bin/env python3
"""Record the source, locked components and hashes of a locally built release."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parent.parent
    web = Path(sys.argv[1]).resolve()
    manifest = json.loads((web / "manifest.json").read_text())
    description = json.loads((root / "firmware/build/project_description.json").read_text())
    versions = {}
    component = None
    for line in (root / "firmware/dependencies.lock").read_text().splitlines():
        match = re.fullmatch(r"  ([\w/]+):", line)
        if match:
            component = match[1]
        match = re.fullmatch(r"    version: (.+)", line)
        if match and component:
            versions[component] = match[1].strip("'\"")
    parts = []
    for item in manifest["builds"][0]["parts"]:
        path = web / item["path"]
        parts.append({**item, "size": path.stat().st_size, "sha256": sha256(path)})
    remote = git(root, "remote", "get-url", "origin").removesuffix(".git")
    remote = re.sub(r"^git@github.com:", "https://github.com/", remote)
    data = {
        "version": manifest["version"],
        "source_repository": remote,
        "source_commit": git(root, "rev-parse", "HEAD"),
        "source_dirty": bool(git(root, "status", "--porcelain")),
        "project_version": description["project_version"],
        "sdk": "ESP-IDF " + versions["idf"],
        "component_versions": versions,
        "dependencies_lock_sha256": sha256(root / "firmware/dependencies.lock"),
        "sdkconfig_sha256": sha256(root / "firmware/sdkconfig"),
        "ejdict_sources": [
            {"path": str(p.relative_to(root)), "sha256": sha256(p)}
            for p in sorted((root / "third_party/ejdict/src").glob("*.txt"))
        ],
        "parts": parts,
        "recovery_resets_nvs": True,
        "device_tested": False,
    }
    recovery = web / f"tab5-pocket-dictionary-{manifest['version']}-firmware.bin"
    data["recovery"] = {"path": recovery.name, "size": recovery.stat().st_size,
                        "sha256": sha256(recovery)}
    (web / "build-info.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
