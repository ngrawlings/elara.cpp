#!/usr/bin/env python3
"""
pbmanager_core.py

A lightweight project/branch/commit manager for the pure-path text server.

It builds on top of pure_path_sync.py and gives you a very small, Git-like GUI:
- register projects by local path + remote root + server
- create/switch branches
- view working tree status
- create commits (metadata snapshots)
- push current working tree to remote
- pull current branch from remote
- browse commit history

Project layout
--------------
Inside each managed project, this app stores state under:

    .project/
        project.json
        branches/
            <branch>/
                head
                commits/
                    <commit_id>/
                        metadata.json

This is intentionally metadata-first. It does not implement object storage,
diff packs, merges, or rebases. A commit is a snapshot of the working tree
hashes plus a small amount of metadata. Push/pull use pure_path_sync.py.

Requirements
------------
- Python 3.10+
- tkinter
- pure_path_sync.py in the same directory, or importable on PYTHONPATH

Usage
-----
python3 pure_path_project_manager_tk.py
"""

from __future__ import annotations

import base64
import difflib
import hashlib
import json
import fnmatch
import mimetypes
import os
import secrets
import socket
import sys
import traceback
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

# ---- dependency from user's existing client ----
try:
    from evclient import (
        ACCESS_NAME,
        INDEX_NAME,
        PurePathClient,
        normalize_remote_root,
        join_remote_path,
        read_utf8_text,
        write_utf8_text,
        is_probably_text_file,
        build_index,
        parse_index,
        sha256_text,
        FileEntry,
    )
except Exception as exc:
    raise SystemExit(
        "Failed to import pure_path_sync.py.\n"
        "Put pure_path_sync.py in the same directory as this app.\n\n"
        f"Import error: {exc}"
    )


APP_NAME = "Elara Versioning"
APP_VERSION = "0.1"
CONFIG_APP_DIR = Path.home() / ".config" / "elara-versioning"
GLOBAL_PROJECTS_FILE = CONFIG_APP_DIR / "projects.json"
GLOBAL_PREFERENCES_FILE = CONFIG_APP_DIR / "preferences.json"
PROJECT_DIR_NAME = ".project"
PROJECT_META_NAME = "project.json"
METADATA_NAME = "metadata.json"
BUGS_DIR_NAME = "bugs"
BUG_ID_COUNTER_NAME = "next_bug_id"
BUG_PENDING_SYNC_NAME = "pending_sync.json"
LAST_SYNC_STATE_NAME = "last_sync_state.json"
REMOTE_COMMIT_SUMMARY_NAME = "commit_summary.json"



def local_now_iso() -> str:
    return datetime.now().replace(microsecond=0).isoformat()


def ensure_global_config_dir() -> None:
    CONFIG_APP_DIR.mkdir(parents=True, exist_ok=True)


def load_global_projects() -> list[dict[str, Any]]:
    ensure_global_config_dir()
    if not GLOBAL_PROJECTS_FILE.exists():
        return []
    try:
        obj = json.loads(GLOBAL_PROJECTS_FILE.read_text(encoding="utf-8"))
    except Exception:
        return []
    projects = obj.get("projects", [])
    if not isinstance(projects, list):
        return []
    cleaned: list[dict[str, Any]] = []
    for item in projects:
        if not isinstance(item, dict):
            continue
        local_dir = str(item.get("local_dir", "")).strip()
        if not local_dir:
            continue
        cleaned.append({
            "name": str(item.get("name", Path(local_dir).name)),
            "local_dir": local_dir,
            "server": str(item.get("server", "")).strip(),
            "remote_root": str(item.get("remote_root", "")).strip(),
            "last_accessed": str(item.get("last_accessed", "")),
        })
    cleaned.sort(key=lambda x: x.get("last_accessed", ""), reverse=True)
    return cleaned


def save_global_projects(projects: list[dict[str, Any]]) -> None:
    ensure_global_config_dir()
    dedup: dict[str, dict[str, Any]] = {}
    for item in projects:
        local_dir = str(item.get("local_dir", "")).strip()
        if not local_dir:
            continue
        dedup[local_dir] = {
            "name": str(item.get("name", Path(local_dir).name)),
            "local_dir": local_dir,
            "server": str(item.get("server", "")).strip(),
            "remote_root": str(item.get("remote_root", "")).strip(),
            "last_accessed": str(item.get("last_accessed", "")),
        }
    ordered = sorted(dedup.values(), key=lambda x: x.get("last_accessed", ""), reverse=True)
    GLOBAL_PROJECTS_FILE.write_text(
        json.dumps({"format": "elara-versioning-registry", "version": 1, "projects": ordered}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def load_global_preferences() -> dict[str, Any]:
    ensure_global_config_dir()
    if not GLOBAL_PREFERENCES_FILE.exists():
        return {}
    try:
        obj = json.loads(GLOBAL_PREFERENCES_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {}
    return obj if isinstance(obj, dict) else {}


def save_global_preferences(prefs: dict[str, Any]) -> None:
    ensure_global_config_dir()
    GLOBAL_PREFERENCES_FILE.write_text(
        json.dumps(prefs, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def pbkdf2_key(password: str, salt: bytes, iterations: int = 200_000) -> bytes:
    password = password or ""
    return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, iterations, dklen=32)


def xor_stream_encrypt(data: bytes, key: bytes, nonce: bytes) -> bytes:
    out = bytearray()
    counter = 0
    offset = 0
    while offset < len(data):
        block = hashlib.sha256(key + nonce + counter.to_bytes(8, "big")).digest()
        chunk = data[offset: offset + len(block)]
        out.extend(a ^ b for a, b in zip(chunk, block))
        offset += len(chunk)
        counter += 1
    return bytes(out)


def encrypt_blob_with_password(data: bytes, password: str) -> bytes:
    salt = secrets.token_bytes(16)
    nonce = secrets.token_bytes(16)
    key = pbkdf2_key(password, salt)
    ciphertext = xor_stream_encrypt(data, key, nonce)
    mac = hashlib.pbkdf2_hmac("sha256", ciphertext + nonce, key, 1, dklen=32)
    payload = {
        "format": "evmanager-encrypted-bundle",
        "version": 1,
        "kdf": "pbkdf2-hmac-sha256",
        "iterations": 200000,
        "cipher": "sha256-xor-stream",
        "salt_b64": base64.b64encode(salt).decode("ascii"),
        "nonce_b64": base64.b64encode(nonce).decode("ascii"),
        "mac_b64": base64.b64encode(mac).decode("ascii"),
        "ciphertext_b64": base64.b64encode(ciphertext).decode("ascii"),
    }
    return safe_json_dump(payload).encode("utf-8")


def decrypt_blob_with_password(blob: bytes, password: str) -> bytes:
    obj = json.loads(blob.decode("utf-8"))
    if obj.get("format") != "evmanager-encrypted-bundle":
        raise ValueError("Unsupported bundle format")
    salt = base64.b64decode(obj["salt_b64"])
    nonce = base64.b64decode(obj["nonce_b64"])
    ciphertext = base64.b64decode(obj["ciphertext_b64"])
    mac = base64.b64decode(obj["mac_b64"])
    key = pbkdf2_key(password, salt, int(obj.get("iterations", 200000)))
    expected_mac = hashlib.pbkdf2_hmac("sha256", ciphertext + nonce, key, 1, dklen=32)
    if not secrets.compare_digest(mac, expected_mac):
        raise ValueError("Invalid password or corrupted bundle")
    return xor_stream_encrypt(ciphertext, key, nonce)


def build_workspace_bundle() -> dict[str, Any]:
    ensure_global_config_dir()
    files: dict[str, str] = {}
    for path in sorted(CONFIG_APP_DIR.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        rel = path.relative_to(CONFIG_APP_DIR).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except Exception:
            continue
        files[rel] = text
    return {
        "format": "evmanager-workspace-bundle",
        "version": 1,
        "created_local": local_now_iso(),
        "config_dir": str(CONFIG_APP_DIR),
        "files": files,
    }


def apply_workspace_bundle(
    bundle: dict[str, Any],
    project_dir_resolver: Any | None = None,
) -> dict[str, Any]:
    if bundle.get("format") != "evmanager-workspace-bundle":
        raise ValueError("Unsupported workspace bundle")
    files = bundle.get("files", {})
    if not isinstance(files, dict):
        raise ValueError("Bundle files payload invalid")

    resolved_files: dict[str, str] = {}
    for rel, content in files.items():
        rel = str(rel).replace("\\", "/").lstrip("/")
        if not rel or rel.startswith("../") or "/../" in rel or rel == "..":
            continue
        resolved_files[rel] = str(content)

    imported_project_configs: list[dict[str, Any]] = []
    if "projects.json" in resolved_files:
        try:
            projects_obj = json.loads(resolved_files["projects.json"])
        except Exception as exc:
            raise ValueError(f"projects.json inside bundle is invalid: {exc}") from exc
        projects = projects_obj.get("projects", []) if isinstance(projects_obj, dict) else []
        updated_projects = []
        for item in projects:
            if not isinstance(item, dict):
                continue
            current_dir = str(item.get("local_dir", "")).strip()
            if project_dir_resolver is None:
                raise RuntimeError("Project directory resolver is required to import workspace bundles that contain projects")
            prompt = f"Select local directory for project '{item.get('name', Path(current_dir).name)}'"
            selected = project_dir_resolver(dict(item), prompt)
            if not selected:
                raise RuntimeError("Import cancelled while selecting local directories")
            new_item = dict(item)
            new_item["local_dir"] = str(Path(selected).resolve())
            updated_projects.append(new_item)
        projects_obj["projects"] = updated_projects
        imported_project_configs = [dict(item) for item in updated_projects if isinstance(item, dict)]
        resolved_files["projects.json"] = json.dumps(projects_obj, ensure_ascii=False, indent=2) + "\n"

    ensure_global_config_dir()
    written = 0
    for rel, content in resolved_files.items():
        dest = CONFIG_APP_DIR / Path(rel)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content, encoding="utf-8")
        written += 1

    scaffolded_projects = 0
    for config_obj in imported_project_configs:
        ensure_project_scaffold_from_config(config_obj)
        scaffolded_projects += 1

    return {
        "ok": True,
        "written_files": written,
        "scaffolded_projects": scaffolded_projects,
    }




def ensure_project_scaffold_from_config(config_obj: dict[str, Any]) -> None:
    local_dir = str(config_obj.get("local_dir", "")).strip()
    if not local_dir:
        return

    project_root = Path(local_dir).resolve()
    project_root.mkdir(parents=True, exist_ok=True)

    repo_dir = project_root / PROJECT_DIR_NAME
    repo_dir.mkdir(parents=True, exist_ok=True)

    project_meta = repo_dir / PROJECT_META_NAME
    project_meta.write_text(
        json.dumps(config_obj, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    branch = str(config_obj.get("current_branch", "main") or "main").strip() or "main"
    branch_dir = repo_dir / "branches" / branch
    commits_dir = branch_dir / "commits"
    commits_dir.mkdir(parents=True, exist_ok=True)

    head_path = branch_dir / "head"
    if not head_path.exists():
        head_path.write_text("", encoding="utf-8")

    bugs_dir = repo_dir / BUGS_DIR_NAME
    bugs_dir.mkdir(parents=True, exist_ok=True)
    bug_counter_path = bugs_dir / BUG_ID_COUNTER_NAME
    if not bug_counter_path.exists():
        bug_counter_path.write_text("1\n", encoding="utf-8")

    ignore_name = str(config_obj.get("ignore_file") or EVIGNORE_NAME).strip() or EVIGNORE_NAME
    ignore_path = project_root / ignore_name
    if not ignore_path.exists():
        ignore_path.write_text(ignore_text_from_patterns(DEFAULT_IGNORE_PATTERNS), encoding="utf-8")

def register_global_project(config: "ProjectConfig") -> None:
    projects = load_global_projects()
    local_dir = str(Path(config.local_dir).resolve())
    entry = {
        "name": config.name,
        "local_dir": local_dir,
        "server": config.server,
        "remote_root": config.remote_root,
        "last_accessed": local_now_iso(),
    }
    remaining = [p for p in projects if str(Path(p.get("local_dir", "")).resolve()) != local_dir]
    save_global_projects([entry] + remaining)


def touch_global_project(local_dir: str) -> None:
    local_dir = str(Path(local_dir).resolve())
    projects = load_global_projects()
    updated = False
    for item in projects:
        if str(Path(item.get("local_dir", "")).resolve()) == local_dir:
            item["last_accessed"] = local_now_iso()
            updated = True
            break
    if updated:
        save_global_projects(projects)


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def host_name() -> str:
    try:
        return socket.gethostname()
    except Exception:
        return "unknown-host"


def safe_json_dump(obj: Any) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=2) + "\n"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalize_rel_posix(path: Path, root: Path) -> str:
    rel = path.relative_to(root)
    return PurePosixPath(*rel.parts).as_posix()


def detect_mime_type(path: Path) -> str:
    mime_type, _encoding = mimetypes.guess_type(path.name)
    return mime_type or "application/octet-stream"


def render_binday_text(path: Path) -> str:
    raw = path.read_bytes()
    rel_name = path.name
    headers = [
        "#!binary",
        f"#mimetype: {detect_mime_type(path)}",
        f"#filename: {rel_name}",
        f"#filesize: {len(raw)}",
        "#content-transfer-encoding: base64",
        "",
        base64.b64encode(raw).decode("ascii"),
        "",
    ]
    return "\n".join(headers)


def read_project_file_for_transport(path: Path) -> str:
    if is_probably_text_file(path):
        return read_utf8_text(path)
    return render_binday_text(path)


def transport_file_entry(path: Path, project_root: Path) -> FileEntry | None:
    rel = normalize_rel_posix(path, project_root)
    name = PurePosixPath(rel).name
    if name == INDEX_NAME:
        return None
    text = read_project_file_for_transport(path)
    return FileEntry(
        path=rel,
        sha256=sha256_text(text),
        bytes=len(text.encode("utf-8")),
    )


DEFAULT_IGNORE_PATTERNS = [
    ".git/",
    "__pycache__/",
    "*.pyc",
    "*.pyo",
    ".venv/",
    "venv/",
    "build/",
    "dist/",
]
EVIGNORE_NAME = ".evignore"


def normalize_ignore_pattern_lines(text: str) -> list[str]:
    lines: list[str] = []
    for raw in text.splitlines():
        line = raw.rstrip()
        if not line.strip():
            continue
        lines.append(line)
    return lines


def ignore_text_from_patterns(patterns: list[str]) -> str:
    cleaned = normalize_ignore_pattern_lines("\n".join(patterns))
    if not cleaned:
        return ""
    return "\n".join(cleaned) + "\n"


def matches_ignore_pattern(rel_path: str, pattern: str) -> bool:
    rel_path = rel_path.replace("\\", "/").lstrip("/")
    pattern = pattern.replace("\\", "/")
    name = PurePosixPath(rel_path).name

    anchored = pattern.startswith("/")
    if anchored:
        pattern = pattern[1:]

    if not pattern:
        return False

    if pattern.endswith("/"):
        prefix = pattern.rstrip("/")
        if not prefix:
            return False
        if anchored:
            return rel_path == prefix or rel_path.startswith(prefix + "/")
        parts = PurePosixPath(rel_path).parts
        prefix_parts = tuple(PurePosixPath(prefix).parts)
        for idx in range(0, len(parts) - len(prefix_parts) + 1):
            if tuple(parts[idx: idx + len(prefix_parts)]) == prefix_parts:
                return True
        return False

    if anchored:
        return fnmatch.fnmatch(rel_path, pattern)

    return fnmatch.fnmatch(rel_path, pattern) or fnmatch.fnmatch(name, pattern)


def should_ignore_rel_path(rel_path: str, ignore_patterns: list[str]) -> bool:
    rel_path = rel_path.replace("\\", "/").lstrip("/")
    ignored = False
    for raw in ignore_patterns:
        pattern = raw.strip()
        if not pattern or pattern.startswith("#"):
            continue
        negate = pattern.startswith("!")
        if negate:
            pattern = pattern[1:].strip()
            if not pattern:
                continue
        if matches_ignore_pattern(rel_path, pattern):
            ignored = not negate
    return ignored


def is_ignored_project_path(project_root: Path, path: Path, ignore_patterns: list[str] | None = None) -> bool:
    try:
        rel = path.relative_to(project_root)
    except ValueError:
        return True
    parts = rel.parts
    if not parts:
        return False
    if parts[0] == PROJECT_DIR_NAME:
        return True
    rel_posix = PurePosixPath(*parts).as_posix()
    return should_ignore_rel_path(rel_posix, ignore_patterns or [])


def text_file_entries(
    project_root: Path,
    include_local_access: bool = False,
    ignore_patterns: list[str] | None = None,
) -> list[FileEntry]:
    entries: list[FileEntry] = []
    patterns = list(ignore_patterns or [])
    for path in sorted(project_root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        if is_ignored_project_path(project_root, path, patterns):
            continue
        rel = normalize_rel_posix(path, project_root)
        name = PurePosixPath(rel).name
        if not include_local_access and name == ACCESS_NAME:
            continue
        try:
            entry = transport_file_entry(path, project_root)
        except Exception:
            continue
        if entry is not None:
            entries.append(entry)
    return entries


def build_snapshot_map(
    project_root: Path,
    include_local_access: bool = False,
    ignore_patterns: list[str] | None = None,
) -> dict[str, dict[str, Any]]:
    snapshot: dict[str, dict[str, Any]] = {}
    for entry in text_file_entries(
        project_root,
        include_local_access=include_local_access,
        ignore_patterns=ignore_patterns,
    ):
        snapshot[entry.path] = {
            "sha256": entry.sha256,
            "bytes": entry.bytes,
        }
    return snapshot


@dataclass
class ProjectConfig:
    name: str
    local_dir: str
    server: str
    remote_root: str
    current_branch: str = "main"
    rpc: bool = True
    timeout: int = 30
    read_password: str | None = None
    write_password: str | None = None
    include_local_access: bool = False
    ignore_file: str = EVIGNORE_NAME

    @classmethod
    def from_dict(cls, obj: dict[str, Any]) -> "ProjectConfig":
        return cls(
            name=obj["name"],
            local_dir=obj["local_dir"],
            server=obj["server"],
            remote_root=obj["remote_root"],
            current_branch=obj.get("current_branch", "main"),
            rpc=bool(obj.get("rpc", True)),
            timeout=int(obj.get("timeout", 30)),
            read_password=obj.get("read_password"),
            write_password=obj.get("write_password"),
            include_local_access=bool(obj.get("include_local_access", False)),
            ignore_file=str(obj.get("ignore_file") or EVIGNORE_NAME),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "local_dir": self.local_dir,
            "server": self.server,
            "remote_root": self.remote_root,
            "current_branch": self.current_branch,
            "rpc": self.rpc,
            "timeout": self.timeout,
            "read_password": self.read_password,
            "write_password": self.write_password,
            "include_local_access": self.include_local_access,
            "ignore_file": self.ignore_file,
            "app": APP_NAME,
            "version": APP_VERSION,
        }


class ProjectRepo:
    def __init__(self, project_root: Path):
        self.project_root = project_root.resolve()
        self.repo_dir = self.project_root / PROJECT_DIR_NAME
        self.project_meta_path = self.repo_dir / PROJECT_META_NAME
        self.config = self.load_config()

    @staticmethod
    def init_project(
        project_root: Path,
        name: str,
        server: str,
        remote_root: str,
        branch: str = "main",
        rpc: bool = True,
        timeout: int = 30,
        read_password: str | None = None,
        write_password: str | None = None,
        include_local_access: bool = False,
        ignore_patterns: list[str] | None = None,
    ) -> "ProjectRepo":
        project_root = project_root.resolve()
        project_root.mkdir(parents=True, exist_ok=True)
        repo_dir = project_root / PROJECT_DIR_NAME
        repo_dir.mkdir(parents=True, exist_ok=True)

        config = ProjectConfig(
            name=name,
            local_dir=str(project_root),
            server=server.rstrip("/"),
            remote_root=normalize_remote_root(remote_root),
            current_branch=branch,
            rpc=rpc,
            timeout=timeout,
            read_password=read_password,
            write_password=write_password,
            include_local_access=include_local_access,
        )

        write_utf8_text(repo_dir / PROJECT_META_NAME, safe_json_dump(config.to_dict()))
        if ignore_patterns is not None:
            ignore_path = project_root / config.ignore_file
            write_utf8_text(ignore_path, ignore_text_from_patterns(ignore_patterns))
        repo = ProjectRepo(project_root)
        repo.ensure_branch(branch)
        register_global_project(repo.config)
        return repo

    def load_config(self) -> ProjectConfig:
        if not self.project_meta_path.exists():
            raise FileNotFoundError(f"Missing project metadata: {self.project_meta_path}")
        obj = json.loads(read_utf8_text(self.project_meta_path))
        return ProjectConfig.from_dict(obj)

    def save_config(self) -> None:
        write_utf8_text(self.project_meta_path, safe_json_dump(self.config.to_dict()))
        register_global_project(self.config)

    def ignore_file_path(self) -> Path:
        ignore_name = (self.config.ignore_file or EVIGNORE_NAME).strip() or EVIGNORE_NAME
        return self.project_root / ignore_name

    def read_ignore_patterns(self) -> list[str]:
        path = self.ignore_file_path()
        if not path.exists():
            return list(DEFAULT_IGNORE_PATTERNS)
        try:
            return normalize_ignore_pattern_lines(read_utf8_text(path))
        except Exception:
            return list(DEFAULT_IGNORE_PATTERNS)

    def write_ignore_patterns(self, patterns: list[str]) -> None:
        write_utf8_text(self.ignore_file_path(), ignore_text_from_patterns(patterns))

    def should_ignore_rel_path(self, rel_path: str) -> bool:
        return should_ignore_rel_path(rel_path, self.read_ignore_patterns())

    def branches_dir(self) -> Path:
        return self.repo_dir / "branches"

    def branch_dir(self, branch: str | None = None) -> Path:
        return self.branches_dir() / (branch or self.config.current_branch)

    def commits_dir(self, branch: str | None = None) -> Path:
        return self.branch_dir(branch) / "commits"

    def head_path(self, branch: str | None = None) -> Path:
        return self.branch_dir(branch) / "head"

    def ensure_branch(self, branch: str) -> None:
        if not branch.strip():
            raise ValueError("Branch name cannot be empty")
        self.commits_dir(branch).mkdir(parents=True, exist_ok=True)
        head = self.head_path(branch)
        if not head.exists():
            write_utf8_text(head, "")

    def list_branches(self) -> list[str]:
        root = self.branches_dir()
        if not root.exists():
            return []
        branches = [p.name for p in root.iterdir() if p.is_dir()]
        return sorted(branches)

    def switch_branch(self, branch: str) -> None:
        self.ensure_branch(branch)
        self.config.current_branch = branch
        self.save_config()

    def current_branch(self) -> str:
        return self.config.current_branch

    def current_head(self) -> str | None:
        head_path = self.head_path()
        if not head_path.exists():
            return None
        text = read_utf8_text(head_path).strip()
        return text or None

    def head_metadata_path(self) -> Path | None:
        head = self.current_head()
        if not head:
            return None
        return self.commits_dir() / head / METADATA_NAME

    def load_head_metadata(self) -> dict[str, Any] | None:
        path = self.head_metadata_path()
        if not path or not path.exists():
            return None
        return json.loads(read_utf8_text(path))

    def last_sync_state_path(self) -> Path:
        return self.repo_dir / LAST_SYNC_STATE_NAME

    def load_last_sync_state(self) -> dict[str, Any] | None:
        path = self.last_sync_state_path()
        if not path.exists():
            return None
        try:
            obj = json.loads(read_utf8_text(path))
        except Exception:
            return None
        if obj.get("format") != "elara-versioning-last-sync":
            return None
        snapshot = obj.get("snapshot")
        if not isinstance(snapshot, dict):
            return None
        return obj

    def save_last_sync_state(
        self,
        snapshot: dict[str, dict[str, Any]],
        *,
        source: str,
        branch: str | None = None,
        remote_files_root: str | None = None,
    ) -> dict[str, Any]:
        state = {
            "format": "elara-versioning-last-sync",
            "version": 1,
            "project_name": self.config.name,
            "branch": branch or self.current_branch(),
            "remote_root": self.config.remote_root,
            "remote_files_root": remote_files_root,
            "updated_utc": utc_now_iso(),
            "source": source,
            "snapshot": snapshot,
        }
        write_utf8_text(self.last_sync_state_path(), safe_json_dump(state))
        return state

    def baseline_snapshot(self) -> tuple[str, dict[str, dict[str, Any]]]:
        last_sync = self.load_last_sync_state()
        current_branch = self.current_branch()
        if last_sync and last_sync.get("branch") == current_branch:
            snapshot = last_sync.get("snapshot", {})
            if isinstance(snapshot, dict):
                return "last_sync", snapshot
        head = self.load_head_metadata()
        if head:
            snapshot = head.get("snapshot", {})
            if isinstance(snapshot, dict):
                return "head", snapshot
        return "empty", {}

    def list_commits(self, branch: str | None = None) -> list[dict[str, Any]]:
        items: list[dict[str, Any]] = []
        commits_root = self.commits_dir(branch)
        if not commits_root.exists():
            return items
        for commit_dir in sorted(commits_root.iterdir(), reverse=True):
            meta = commit_dir / METADATA_NAME
            if meta.exists():
                try:
                    items.append(json.loads(read_utf8_text(meta)))
                except Exception:
                    pass
        return items

    def working_snapshot(self) -> dict[str, dict[str, Any]]:
        return build_snapshot_map(
            self.project_root,
            include_local_access=self.config.include_local_access,
            ignore_patterns=self.read_ignore_patterns(),
        )

    def status(self) -> dict[str, Any]:
        current = self.working_snapshot()
        baseline_source, prev = self.baseline_snapshot()

        cur_paths = set(current.keys())
        prev_paths = set(prev.keys())
        added = sorted(cur_paths - prev_paths)
        deleted = sorted(prev_paths - cur_paths)
        modified = sorted(
            p for p in (cur_paths & prev_paths)
            if current[p].get("sha256") != prev[p].get("sha256")
        )
        unchanged = sorted(
            p for p in (cur_paths & prev_paths)
            if current[p].get("sha256") == prev[p].get("sha256")
        )
        return {
            "baseline": baseline_source,
            "added": added,
            "modified": modified,
            "deleted": deleted,
            "unchanged": unchanged,
        }

    def next_commit_id(self) -> str:
        ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        entropy = os.urandom(3).hex()
        return f"{ts}-{entropy}"

    def commit(
        self,
        message: str,
        author: str = "user",
        bug_ids: list[str] | None = None,
        excluded_paths: list[str] | None = None,
    ) -> dict[str, Any]:
        message = (message or "").strip()
        if not message:
            raise ValueError("Commit message is required")

        branch = self.current_branch()
        self.ensure_branch(branch)
        parent = self.current_head()
        snapshot = self.working_snapshot()
        _baseline_source, baseline_snapshot = self.baseline_snapshot()
        excluded: set[str] = set()
        for raw_path in excluded_paths or []:
            rel_path = str(PurePosixPath(str(raw_path).replace("\\", "/").lstrip("/")))
            if rel_path and rel_path != ".":
                excluded.add(rel_path)
        for rel_path in excluded:
            baseline_item = baseline_snapshot.get(rel_path)
            if baseline_item is not None:
                snapshot[rel_path] = dict(baseline_item)
            else:
                snapshot.pop(rel_path, None)
        commit_id = self.next_commit_id()
        current_paths = set(snapshot.keys())
        previous_paths = set(baseline_snapshot.keys())
        added = current_paths - previous_paths
        deleted = previous_paths - current_paths
        modified = {
            path for path in (current_paths & previous_paths)
            if snapshot[path].get("sha256") != baseline_snapshot[path].get("sha256")
        }

        metadata = {
            "format": "elara-versioning-commit",
            "version": 1,
            "commit_id": commit_id,
            "parent": parent,
            "branch": branch,
            "project_name": self.config.name,
            "local_dir": str(self.project_root),
            "server": self.config.server,
            "remote_root": self.config.remote_root,
            "author": author,
            "host": host_name(),
            "created_utc": utc_now_iso(),
            "message": message,
            "bug_ids": list(bug_ids or []),
            "snapshot": snapshot,
            "stats": {
                "files": len(snapshot),
                "added": len(added),
                "modified": len(modified),
                "deleted": len(deleted),
            },
        }

        commit_dir = self.commits_dir(branch) / commit_id
        commit_dir.mkdir(parents=True, exist_ok=True)
        write_utf8_text(commit_dir / METADATA_NAME, safe_json_dump(metadata))
        write_utf8_text(self.head_path(branch), commit_id + "\n")
        self.link_commit_to_bugs(commit_id, list(bug_ids or []))
        return metadata

    def make_client(self) -> PurePathClient:
        return PurePathClient(
            self.config.server,
            rpc=self.config.rpc,
            timeout=self.config.timeout,
            read_password=self.config.read_password,
            write_password=self.config.write_password,
        )

    def remote_branch_root(self, branch: str | None = None) -> str:
        branch = branch or self.current_branch()
        return join_remote_path(self.config.remote_root, f"branches/{branch}/files")

    def remote_meta_root(self, branch: str | None = None) -> str:
        branch = branch or self.current_branch()
        return join_remote_path(self.config.remote_root, f"branches/{branch}/meta")

    def remote_object_root(self, branch: str | None = None) -> str:
        return join_remote_path(self.remote_meta_root(branch), "objects")

    def remote_object_path(self, sha256: str, branch: str | None = None) -> str:
        sha256 = str(sha256).strip().lower()
        if not sha256:
            raise ValueError("sha256 is required")
        return join_remote_path(self.remote_object_root(branch), sha256)

    def remote_head_path(self, branch: str | None = None) -> str:
        return join_remote_path(self.remote_meta_root(branch), "HEAD.json")

    def set_remote_symlinks(self, client: PurePathClient, links: list[dict[str, str]]) -> dict[str, Any]:
        if not links:
            return {"ok": True, "links": [], "count": 0}
        if hasattr(client, "set_symlinks"):
            return client.set_symlinks(links)
        return client._http_json(client.server_base + "/_/rpc/set_symlinks", {"links": links})

    def lock_remote_paths(self, client: PurePathClient, paths: list[str]) -> dict[str, Any]:
        unique_paths = sorted({str(p).strip() for p in paths if str(p).strip()})
        if not unique_paths:
            return {"ok": True, "paths": [], "count": 0}
        return client._http_json(client.server_base + "/_/rpc/lock", {"paths": unique_paths})

    def stat_remote_paths(self, client: PurePathClient, paths: list[str]) -> dict[str, dict[str, Any]]:
        unique_paths = sorted({str(p).strip() for p in paths if str(p).strip()})
        if not unique_paths:
            return {}
        if hasattr(client, "stat_paths"):
            return client.stat_paths(unique_paths)
        result = client._http_json(client.server_base + "/_/rpc/exists", {"paths": unique_paths})
        if not result.get("ok", False):
            raise RuntimeError(result)
        statuses: dict[str, dict[str, Any]] = {}
        for item in result.get("paths", []):
            if not isinstance(item, dict):
                continue
            canonical = str(item.get("path", "")).strip()
            if canonical:
                statuses[canonical] = item
        return statuses

    def fetch_remote_head(self, branch: str | None = None) -> dict[str, Any] | None:
        client = self.make_client()
        try:
            text = client.get_text(self.remote_head_path(branch))
        except Exception:
            return None
        try:
            obj = json.loads(text)
        except Exception:
            return None
        if obj.get("format") != "elara-versioning-head":
            return None
        return obj

    def local_commit_metadata(self, commit_id: str, branch: str | None = None) -> dict[str, Any] | None:
        if not commit_id:
            return None
        meta_path = self.commits_dir(branch) / commit_id / METADATA_NAME
        if not meta_path.exists():
            return None
        try:
            return json.loads(read_utf8_text(meta_path))
        except Exception:
            return None

    def is_local_descendant_of(self, commit_id: str | None, ancestor_commit_id: str | None, branch: str | None = None) -> bool:
        if not ancestor_commit_id:
            return True
        seen: set[str] = set()
        current = commit_id
        while current and current not in seen:
            if current == ancestor_commit_id:
                return True
            seen.add(current)
            meta = self.local_commit_metadata(current, branch)
            if not meta:
                return False
            current = meta.get("parent")
        return False

    def default_fork_branch_name(self, base_branch: str, username: str | None = None) -> str:
        user = (username or os.environ.get("USER") or os.environ.get("USERNAME") or "user").strip().lower()
        cleaned = "".join(ch if ch.isalnum() or ch in "-_" else "-" for ch in user).strip("-") or "user"
        return f"{base_branch}-{cleaned}"

    def choose_push_branch(self, target_branch: str, local_head_commit: str | None) -> tuple[str, dict[str, Any] | None, bool]:
        remote_head = self.fetch_remote_head(target_branch)
        remote_head_commit = remote_head.get("commit_id") if remote_head else None
        if remote_head_commit is None or self.is_local_descendant_of(local_head_commit, remote_head_commit, target_branch):
            return target_branch, remote_head, False

        fork_branch = self.default_fork_branch_name(target_branch)
        fork_remote_head = self.fetch_remote_head(fork_branch)
        fork_remote_head_commit = fork_remote_head.get("commit_id") if fork_remote_head else None
        if fork_remote_head_commit is None or self.is_local_descendant_of(local_head_commit, fork_remote_head_commit, target_branch):
            return fork_branch, fork_remote_head, True

        suffix = 2
        while True:
            candidate = f"{fork_branch}-{suffix}"
            candidate_remote_head = self.fetch_remote_head(candidate)
            candidate_head_commit = candidate_remote_head.get("commit_id") if candidate_remote_head else None
            if candidate_head_commit is None or self.is_local_descendant_of(local_head_commit, candidate_head_commit, target_branch):
                return candidate, candidate_remote_head, True
            suffix += 1

    def _push_tree_to_branch(self, push_branch: str, remote_head: dict[str, Any] | None, auto_forked: bool) -> dict[str, Any]:
        client = self.make_client()
        files_root = self.remote_branch_root(push_branch)
        meta_root = self.remote_meta_root(push_branch)
        baseline_source, baseline_snapshot = self.baseline_snapshot()

        file_uploads: list[dict[str, Any]] = []
        entries: list[FileEntry] = []
        uploaded = 0
        symlinked = 0
        object_uploads = 0
        symlink_links: list[dict[str, str]] = []
        immutable_lock_paths: set[str] = set()
        remote_status_paths: set[str] = set()
        known_remote_objects: set[str] = set()

        ignore_patterns = self.read_ignore_patterns()
        for path in sorted(self.project_root.rglob("*")):
            if path.is_symlink() or not path.is_file():
                continue
            if is_ignored_project_path(self.project_root, path, ignore_patterns):
                continue

            rel = normalize_rel_posix(path, self.project_root)
            name = PurePosixPath(rel).name
            if name == INDEX_NAME:
                continue
            if not self.config.include_local_access and name == ACCESS_NAME:
                continue

            try:
                text = read_project_file_for_transport(path)
            except Exception:
                continue

            file_sha256 = sha256_text(text)
            file_bytes = len(text.encode("utf-8"))
            remote_path = join_remote_path(files_root, rel)
            object_path = self.remote_object_path(file_sha256, push_branch)

            baseline_item = baseline_snapshot.get(rel, {}) if isinstance(baseline_snapshot, dict) else {}
            unchanged = baseline_item.get("sha256") == file_sha256

            entry = FileEntry(path=rel, sha256=file_sha256, bytes=file_bytes)
            entries.append(entry)
            file_uploads.append({
                "entry": entry,
                "text": text,
                "remote_path": remote_path,
                "object_path": object_path,
                "unchanged": unchanged,
            })
            remote_status_paths.add(object_path)

        head_meta = self.load_head_metadata()
        head_commit = head_meta["commit_id"] if head_meta else None
        remote_commit_meta_path = join_remote_path(meta_root, f"commits/{head_commit}.json") if head_commit else None
        if remote_commit_meta_path:
            remote_status_paths.add(remote_commit_meta_path)

        remote_path_status = self.stat_remote_paths(client, sorted(remote_status_paths))

        def get_remote_status(remote_path: str) -> dict[str, Any]:
            status = remote_path_status.get(remote_path)
            if isinstance(status, dict):
                return status
            return {"path": remote_path, "exists": False, "locked": False}

        def ensure_remote_object(object_path: str, text: str) -> bool:
            if object_path in known_remote_objects:
                return False

            status = get_remote_status(object_path)
            if bool(status.get("locked", False)) and not bool(status.get("exists", False)):
                raise RuntimeError(f"Remote object path is locked but missing content: {object_path}")
            if bool(status.get("exists", False)):
                known_remote_objects.add(object_path)
                if not bool(status.get("locked", False)):
                    immutable_lock_paths.add(object_path)
                return False

            print(f"Setting: {object_path}")
            client.set_text(object_path, text)
            known_remote_objects.add(object_path)
            immutable_lock_paths.add(object_path)
            remote_path_status[object_path] = {"path": object_path, "exists": True, "locked": False}
            return True

        for item in file_uploads:
            object_path = str(item["object_path"])
            if ensure_remote_object(object_path, str(item["text"])):
                object_uploads += 1

            remote_path = str(item["remote_path"])
            if bool(item["unchanged"]):
                symlink_links.append({"path": remote_path, "target": object_path})
                symlinked += 1
                continue

            print(f"Setting: {remote_path}")
            client.set_text(remote_path, str(item["text"]))
            uploaded += 1

        if symlink_links:
            self.set_remote_symlinks(client, symlink_links)

        index_text = build_index(entries, files_root)
        print(f"Setting: {join_remote_path(files_root, INDEX_NAME)}")
        client.set_text(join_remote_path(files_root, INDEX_NAME), index_text)

        current_snapshot = {entry.path: {"sha256": entry.sha256, "bytes": entry.bytes} for entry in entries}

        remote_head_commit = remote_head.get("commit_id") if remote_head else None
        head_updated = remote_head_commit is None or self.is_local_descendant_of(head_commit, remote_head_commit)

        if head_meta and remote_commit_meta_path:
            commit_meta_status = get_remote_status(remote_commit_meta_path)
            if bool(commit_meta_status.get("locked", False)) and not bool(commit_meta_status.get("exists", False)):
                raise RuntimeError(f"Remote commit metadata path is locked but missing content: {remote_commit_meta_path}")
            if not bool(commit_meta_status.get("exists", False)):
                print(f"setting {remote_commit_meta_path}")
                client.set_text(
                    remote_commit_meta_path,
                    safe_json_dump(head_meta),
                )
                remote_path_status[remote_commit_meta_path] = {
                    "path": remote_commit_meta_path,
                    "exists": True,
                    "locked": False,
                }
            if not bool(commit_meta_status.get("locked", False)):
                immutable_lock_paths.add(remote_commit_meta_path)
            if head_updated:
                head_info = {
                    "format": "elara-versioning-head",
                    "version": 1,
                    "branch": push_branch,
                    "commit_id": head_commit,
                    "updated_utc": utc_now_iso(),
                    "source_branch": self.current_branch(),
                    "auto_forked": auto_forked,
                }
                print(f"setting {join_remote_path(meta_root, "HEAD.json")}")
                client.set_text(join_remote_path(meta_root, "HEAD.json"), safe_json_dump(head_info))

        commit_summary = self.build_commit_summary(self.current_branch())
        commit_summary["branch"] = push_branch
        commit_summary["source_branch"] = self.current_branch()
        commit_summary["auto_forked"] = auto_forked
        print(f"setting {self.remote_commit_summary_path(push_branch)}")
        client.set_text(self.remote_commit_summary_path(push_branch), safe_json_dump(commit_summary))
        self.save_local_remote_commit_summary(commit_summary, push_branch)

        lock_result = self.lock_remote_paths(client, sorted(immutable_lock_paths))

        self.save_last_sync_state(
            current_snapshot,
            source="push",
            branch=push_branch,
            remote_files_root=files_root,
        )

        return {
            "ok": True,
            "uploaded_files": uploaded,
            "symlinked_files": symlinked,
            "object_uploads": object_uploads,
            "baseline_source": baseline_source,
            "branch": push_branch,
            "source_branch": self.current_branch(),
            "remote_files_root": files_root,
            "remote_meta_root": meta_root,
            "remote_object_root": self.remote_object_root(push_branch),
            "head_commit": head_commit,
            "head_updated": head_updated,
            "remote_head_before": remote_head_commit,
            "commit_summary_path": self.remote_commit_summary_path(push_branch),
            "commit_summary_count": len(commit_summary.get("commits", [])),
            "locked_paths": lock_result.get("paths", []),
            "locked_count": int(lock_result.get("count", 0) or 0),
            "auto_forked": auto_forked,
        }

    def push_current_tree(self) -> dict[str, Any]:
        """
        Before pushing, download the relevant remote HEAD so push decisions are
        based on the current server state.

        Fast-forward rule:
        - if the current remote head is in the ancestry of the local head, push to
          the selected branch and advance that branch head
        - otherwise push to an automatically created user fork branch
        """
        client = self.make_client()

        try:
            health = client.health()
            if not health.get("ok", False):
                raise RuntimeError(f"Health returned non-ok: {health}")
        except Exception as exc:
            raise RuntimeError(f"Server health check failed: {exc}") from exc

        local_branch = self.current_branch()
        local_head_commit = self.current_head()
        push_branch, remote_head, auto_forked = self.choose_push_branch(local_branch, local_head_commit)
        result = self._push_tree_to_branch(push_branch, remote_head, auto_forked)

        if auto_forked and push_branch != local_branch:
            self.switch_branch(push_branch)
            result["local_branch_switched"] = True
        else:
            result["local_branch_switched"] = False

        return result

    def pull_current_tree(self) -> dict[str, Any]:
        client = self.make_client()
        branch = self.current_branch()
        files_root = self.remote_branch_root(branch)
        index_remote_path = join_remote_path(files_root, INDEX_NAME)

        index_text = client.get_text(index_remote_path)
        index_obj = parse_index(index_text)

        pulled = 0
        failed = 0

        for item in index_obj["files"]:
            rel = item["path"]
            expected_sha = item["sha256"]
            rel_pp = PurePosixPath(rel)
            if rel_pp.is_absolute():
                failed += 1
                continue
            if ".." in rel_pp.parts or "." in rel_pp.parts:
                failed += 1
                continue

            remote_path = join_remote_path(files_root, rel)
            text = client.get_text(remote_path)
            actual_sha = sha256_text(text)
            if actual_sha != expected_sha:
                failed += 1
                continue

            local_path = self.project_root.joinpath(*rel_pp.parts)
            write_utf8_text(local_path, text)
            pulled += 1

        write_utf8_text(self.project_root / INDEX_NAME, index_text)
        remote_commit_summary = self.fetch_remote_commit_summary(branch)
        if remote_commit_summary is not None:
            self.save_local_remote_commit_summary(remote_commit_summary, branch)
        if failed == 0:
            remote_snapshot = {
                str(item["path"]): {"sha256": item["sha256"], "bytes": item.get("bytes", 0)}
                for item in index_obj.get("files", [])
                if isinstance(item, dict) and "path" in item and "sha256" in item
            }
            self.save_last_sync_state(
                remote_snapshot,
                source="pull",
                branch=branch,
                remote_files_root=files_root,
            )
        return {
            "ok": failed == 0,
            "pulled_files": pulled,
            "failed_files": failed,
            "branch": branch,
            "remote_files_root": files_root,
            "remote_commit_summary": remote_commit_summary is not None,
            "remote_commit_count": len(remote_commit_summary.get("commits", [])) if remote_commit_summary else 0,
        }


    def remote_commit_summary_path(self, branch: str | None = None) -> str:
        return join_remote_path(self.remote_meta_root(branch), REMOTE_COMMIT_SUMMARY_NAME)

    def local_remote_commit_summary_path(self, branch: str | None = None) -> Path:
        return self.branch_dir(branch) / REMOTE_COMMIT_SUMMARY_NAME

    def build_commit_summary(self, branch: str | None = None) -> dict[str, Any]:
        branch_name = branch or self.current_branch()
        commits = self.list_commits(branch_name)
        summary_items: list[dict[str, Any]] = []
        for commit in commits:
            summary_items.append({
                "commit_id": commit.get("commit_id"),
                "parent": commit.get("parent"),
                "branch": commit.get("branch"),
                "project_name": commit.get("project_name"),
                "author": commit.get("author"),
                "host": commit.get("host"),
                "created_utc": commit.get("created_utc"),
                "message": commit.get("message"),
                "bug_ids": list(commit.get("bug_ids", [])),
                "stats": dict(commit.get("stats", {})),
            })
        return {
            "format": "elara-versioning-commit-summary",
            "version": 1,
            "project_name": self.config.name,
            "branch": branch_name,
            "remote_root": self.config.remote_root,
            "generated_utc": utc_now_iso(),
            "head_commit": self.current_head(),
            "commits": summary_items,
        }

    def save_local_remote_commit_summary(self, summary_obj: dict[str, Any], branch: str | None = None) -> None:
        path = self.local_remote_commit_summary_path(branch)
        path.parent.mkdir(parents=True, exist_ok=True)
        write_utf8_text(path, safe_json_dump(summary_obj))

    def load_local_remote_commit_summary(self, branch: str | None = None) -> dict[str, Any] | None:
        path = self.local_remote_commit_summary_path(branch)
        if not path.exists():
            return None
        try:
            obj = json.loads(read_utf8_text(path))
        except Exception:
            return None
        if obj.get("format") != "elara-versioning-commit-summary":
            return None
        commits = obj.get("commits")
        if not isinstance(commits, list):
            return None
        return obj

    def fetch_remote_commit_summary(self, branch: str | None = None) -> dict[str, Any] | None:
        client = self.make_client()
        try:
            text = client.get_text(self.remote_commit_summary_path(branch))
        except Exception:
            return None
        try:
            obj = json.loads(text)
        except Exception:
            return None
        if obj.get("format") != "elara-versioning-commit-summary":
            return None
        commits = obj.get("commits")
        if not isinstance(commits, list):
            return None
        return obj

    def bugs_dir(self) -> Path:
        return self.repo_dir / BUGS_DIR_NAME

    def bug_counter_path(self) -> Path:
        return self.bugs_dir() / BUG_ID_COUNTER_NAME

    def ensure_bug_storage(self) -> None:
        self.bugs_dir().mkdir(parents=True, exist_ok=True)
        if not self.bug_counter_path().exists():
            write_utf8_text(self.bug_counter_path(), "1\n")

    def next_bug_id(self) -> str:
        self.ensure_bug_storage()
        counter_path = self.bug_counter_path()
        try:
            n = int(read_utf8_text(counter_path).strip() or "1")
        except Exception:
            n = 1
        write_utf8_text(counter_path, f"{n + 1}\n")
        return f"BUG-{n:05d}"

    def bug_path(self, bug_id: str) -> Path:
        return self.bugs_dir() / f"{bug_id}.json"

    def create_bug(
        self,
        title: str,
        description: str = "",
        severity: str = "medium",
        status: str = "open",
        author: str = "user",
    ) -> dict[str, Any]:
        title = (title or "").strip()
        if not title:
            raise ValueError("Bug title is required")
        self.ensure_bug_storage()
        bug_id = self.next_bug_id()
        obj = {
            "format": "elara-versioning-bug",
            "version": 1,
            "bug_id": bug_id,
            "title": title,
            "description": description or "",
            "severity": severity,
            "status": status,
            "author": author,
            "host": host_name(),
            "created_utc": utc_now_iso(),
            "updated_utc": utc_now_iso(),
            "linked_commits": [],
            "comments": [],
        }
        write_utf8_text(self.bug_path(bug_id), safe_json_dump(obj))
        try:
            self.sync_bug_to_remote(bug_id)
            obj["remote_sync"] = "ok"
        except Exception as exc:
            obj["remote_sync"] = f"pending: {exc}"
        return obj

    def list_bugs(self) -> list[dict[str, Any]]:
        self.ensure_bug_storage()
        items: list[dict[str, Any]] = []
        for path in sorted(self.bugs_dir().glob("BUG-*.json")):
            try:
                items.append(json.loads(read_utf8_text(path)))
            except Exception:
                pass
        items.sort(key=lambda x: x.get("bug_id", ""), reverse=True)
        return items

    def load_bug(self, bug_id: str) -> dict[str, Any]:
        path = self.bug_path(bug_id)
        if not path.exists():
            raise FileNotFoundError(f"Bug not found: {bug_id}")
        return json.loads(read_utf8_text(path))

    def save_bug(self, bug: dict[str, Any]) -> None:
        bug["updated_utc"] = utc_now_iso()
        write_utf8_text(self.bug_path(bug["bug_id"]), safe_json_dump(bug))


    def add_bug_comment(self, bug_id: str, text: str, author: str = "user") -> dict[str, Any]:
        text = (text or "").strip()
        if not text:
            raise ValueError("Comment text required")
        bug = self.load_bug(bug_id)
        comments = list(bug.get("comments", []))
        comments.append({
            "author": author,
            "created_utc": utc_now_iso(),
            "text": text,
        })
        bug["comments"] = comments
        self.save_bug(bug)
        try:
            self.sync_bug_to_remote(bug_id)
            bug["remote_sync"] = "ok"
        except Exception as exc:
            bug["remote_sync"] = f"pending: {exc}"
        return bug

    def update_bug(
        self,
        bug_id: str,
        *,
        title: str | None = None,
        description: str | None = None,
        severity: str | None = None,
        status: str | None = None,
    ) -> dict[str, Any]:
        bug = self.load_bug(bug_id)
        if title is not None:
            bug["title"] = title
        if description is not None:
            bug["description"] = description
        if severity is not None:
            bug["severity"] = severity
        if status is not None:
            bug["status"] = status
        self.save_bug(bug)
        try:
            self.sync_bug_to_remote(bug_id)
            bug["remote_sync"] = "ok"
        except Exception as exc:
            bug["remote_sync"] = f"pending: {exc}"
        return bug

    def link_commit_to_bugs(self, commit_id: str, bug_ids: list[str]) -> None:
        seen = set()
        for bug_id in bug_ids:
            bug_id = bug_id.strip()
            if not bug_id or bug_id in seen:
                continue
            seen.add(bug_id)
            try:
                bug = self.load_bug(bug_id)
            except FileNotFoundError:
                continue
            linked = list(bug.get("linked_commits", []))
            if commit_id not in linked:
                linked.append(commit_id)
            bug["linked_commits"] = linked
            self.save_bug(bug)


    def bug_pending_sync_path(self) -> Path:
        return self.bugs_dir() / BUG_PENDING_SYNC_NAME

    def load_pending_bug_sync(self) -> dict[str, Any]:
        self.ensure_bug_storage()
        path = self.bug_pending_sync_path()
        if not path.exists():
            return {"pending_bug_ids": []}
        try:
            obj = json.loads(read_utf8_text(path))
            ids = obj.get("pending_bug_ids", [])
            if not isinstance(ids, list):
                ids = []
            return {"pending_bug_ids": [str(x) for x in ids]}
        except Exception:
            return {"pending_bug_ids": []}

    def save_pending_bug_sync(self, obj: dict[str, Any]) -> None:
        self.ensure_bug_storage()
        ids = obj.get("pending_bug_ids", [])
        deduped = []
        seen = set()
        for item in ids:
            item = str(item).strip()
            if item and item not in seen:
                seen.add(item)
                deduped.append(item)
        write_utf8_text(self.bug_pending_sync_path(), safe_json_dump({"pending_bug_ids": deduped}))

    def mark_bug_pending_sync(self, bug_id: str) -> None:
        obj = self.load_pending_bug_sync()
        ids = obj.get("pending_bug_ids", [])
        if bug_id not in ids:
            ids.append(bug_id)
        self.save_pending_bug_sync({"pending_bug_ids": ids})

    def clear_bug_pending_sync(self, bug_id: str) -> None:
        obj = self.load_pending_bug_sync()
        ids = [x for x in obj.get("pending_bug_ids", []) if x != bug_id]
        self.save_pending_bug_sync({"pending_bug_ids": ids})

    def remote_bugs_root(self) -> str:
        return join_remote_path(self.config.remote_root, "bugs")

    def push_bug_index(self) -> dict[str, Any]:
        client = self.make_client()
        bugs_root = self.remote_bugs_root()
        entries: list[FileEntry] = []

        for path in sorted(self.bugs_dir().glob("BUG-*.json")):
            try:
                text = read_utf8_text(path)
            except Exception:
                continue
            entries.append(
                FileEntry(
                    path=path.name,
                    sha256=sha256_text(text),
                    bytes=len(text.encode("utf-8")),
                )
            )

        index_text = build_index(entries, bugs_root)

        print(f"setting {join_remote_path(bugs_root, INDEX_NAME)}")
        client.set_text(join_remote_path(bugs_root, INDEX_NAME), index_text)
        return {
            "ok": True,
            "remote_bugs_root": bugs_root,
            "bug_files_indexed": len(entries),
        }

    def sync_bug_to_remote(self, bug_id: str) -> dict[str, Any]:
        bug = self.load_bug(bug_id)
        client = self.make_client()
        bugs_root = self.remote_bugs_root()
        bug_text = safe_json_dump(bug)

        try:
            client.health()
        except Exception as exc:
            self.mark_bug_pending_sync(bug_id)
            raise RuntimeError(f"Server health check failed: {exc}") from exc

        try:
            print(f"setting {join_remote_path(bugs_root, f"{bug_id}.json")}")
            client.set_text(join_remote_path(bugs_root, f"{bug_id}.json"), bug_text)
            idx_result = self.push_bug_index()
            self.clear_bug_pending_sync(bug_id)
            return {
                "ok": True,
                "bug_id": bug_id,
                "remote_bugs_root": bugs_root,
                "index_result": idx_result,
            }
        except Exception as exc:
            self.mark_bug_pending_sync(bug_id)
            raise

    def sync_pending_bugs(self) -> dict[str, Any]:
        pending = self.load_pending_bug_sync().get("pending_bug_ids", [])
        synced: list[str] = []
        failed: list[str] = []

        if not pending:
            return {"ok": True, "synced": [], "failed": []}

        for bug_id in list(pending):
            try:
                self.sync_bug_to_remote(bug_id)
                synced.append(bug_id)
            except Exception:
                failed.append(bug_id)

        return {
            "ok": len(failed) == 0,
            "synced": synced,
            "failed": failed,
        }
