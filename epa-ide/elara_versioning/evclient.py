#!/usr/bin/env python3
"""
pure_path_sync.py

Directory sync client for the pure-path text server with .access support.

Commands
--------
push
    Upload a local directory tree as text files and create/update remote .index

pull
    Download a remote tree using the remote .index file and mirror locally

set-access
    Create or update a remote .access file for a directory

show-index
    Fetch and print a remote .index file

Notes
-----
- Text files must decode as UTF-8
- Symlinks are ignored
- Local .access files are skipped by default
- Remote .access files are never read by the server API
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable
from urllib import error, request


INDEX_NAME = ".index"
ACCESS_NAME = ".access"
USER_AGENT = "elara-versioning/1.1"


@dataclass
class FileEntry:
    path: str
    sha256: str
    bytes: int


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def normalize_remote_root(remote_root: str) -> str:
    if not remote_root.startswith("/"):
        raise ValueError("Remote root must start with '/'")
    if remote_root != "/" and remote_root.endswith("/"):
        remote_root = remote_root[:-1]
    if "//" in remote_root:
        raise ValueError("Remote root must not contain repeated slashes")
    return remote_root


def join_remote_path(remote_root: str, rel_posix: str) -> str:
    remote_root = normalize_remote_root(remote_root)
    rel_posix = rel_posix.strip("/")
    if not rel_posix:
        return remote_root
    if remote_root == "/":
        return "/" + rel_posix
    return remote_root + "/" + rel_posix


def rel_path_posix(root: Path, file_path: Path) -> str:
    rel = file_path.relative_to(root)
    return PurePosixPath(*rel.parts).as_posix()


def read_utf8_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_utf8_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def is_probably_text_file(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as f:
        chunk = f.read(4096)
    return b"\x00" not in chunk


def build_index(entries: list[FileEntry], remote_root: str) -> str:
    payload = {
        "format": "elara-versioning-index",
        "version": 1,
        "remote_root": normalize_remote_root(remote_root),
        "files": [
            {"path": e.path, "sha256": e.sha256, "bytes": e.bytes}
            for e in sorted(entries, key=lambda x: x.path)
        ],
    }
    return json.dumps(payload, ensure_ascii=False, indent=2) + "\n"


def parse_index(text: str) -> dict:
    obj = json.loads(text)
    if obj.get("format") != "elara-versioning-index":
        raise ValueError("Unsupported index format")
    if obj.get("version") != 1:
        raise ValueError(f"Unsupported index version: {obj.get('version')}")
    files = obj.get("files")
    if not isinstance(files, list):
        raise ValueError("Index missing files list")
    return obj


def walk_text_files(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        if not is_probably_text_file(path):
            continue
        yield path


class PurePathClient:
    def __init__(
        self,
        server_base: str,
        rpc: bool = True,
        timeout: int = 30,
        read_password: str | None = None,
        write_password: str | None = None,
    ):
        self.server_base = server_base.rstrip("/")
        self.rpc = rpc
        self.timeout = timeout
        self.read_password = read_password
        self.write_password = write_password

    def _base_headers(self) -> dict[str, str]:
        headers = {"User-Agent": USER_AGENT}
        if self.read_password is not None:
            headers["X-Read-Password"] = self.read_password
        if self.write_password is not None:
            headers["X-Write-Password"] = self.write_password
        return headers

    def _http_json(self, url: str, payload: dict) -> dict:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers = self._base_headers()
        headers["Content-Type"] = "application/json; charset=utf-8"
        req = request.Request(url, data=data, headers=headers, method="POST")
        with request.urlopen(req, timeout=self.timeout) as resp:
            body = resp.read().decode("utf-8")
            return json.loads(body)

    def _http_put_text(self, url: str, text: str) -> dict:
        headers = self._base_headers()
        headers["Content-Type"] = "text/plain; charset=utf-8"
        req = request.Request(url, data=text.encode("utf-8"), headers=headers, method="PUT")
        with request.urlopen(req, timeout=self.timeout) as resp:
            body = resp.read().decode("utf-8")
            return json.loads(body)

    def health(self) -> dict:
        req = request.Request(
            self.server_base + "/_/health",
            headers=self._base_headers(),
            method="GET",
        )
        with request.urlopen(req, timeout=self.timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def get_text(self, remote_path: str, read_password: str | None = None) -> str:
        rp = self.read_password if read_password is None else read_password
        if self.rpc:
            payload = {"path": remote_path}
            if rp is not None:
                payload["read_password"] = rp
            obj = self._http_json(self.server_base + "/_/rpc/get", payload)
            if not obj.get("ok", False):
                raise RuntimeError(obj)
            return obj["text"]

        headers = {"User-Agent": USER_AGENT}
        if rp is not None:
            headers["X-Read-Password"] = rp
        req = request.Request(
            self.server_base + remote_path,
            headers=headers,
            method="GET",
        )
        with request.urlopen(req, timeout=self.timeout) as resp:
            return resp.read().decode("utf-8")

    def set_text(
        self,
        remote_path: str,
        text: str,
        write_password: str | None = None,
    ) -> dict:
        wp = self.write_password if write_password is None else write_password
        if self.rpc:
            payload = {"path": remote_path, "text": text}
            if wp is not None:
                payload["write_password"] = wp
            return self._http_json(self.server_base + "/_/rpc/set", payload)

        headers = {"User-Agent": USER_AGENT, "Content-Type": "text/plain; charset=utf-8"}
        if wp is not None:
            headers["X-Write-Password"] = wp
        req = request.Request(
            self.server_base + remote_path,
            data=text.encode("utf-8"),
            headers=headers,
            method="PUT",
        )
        with request.urlopen(req, timeout=self.timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def set_access(
        self,
        target_path: str,
        read_password: str | None,
        write_password_new: str | None,
        write_password_current: str | None = None,
    ) -> dict:
        wp_current = self.write_password if write_password_current is None else write_password_current
        payload = {
            "path": target_path,
            "read_password": read_password,
            "write_password_new": write_password_new,
        }
        if wp_current is not None:
            payload["write_password_current"] = wp_current
        return self._http_json(self.server_base + "/_/rpc/set_access", payload)

    def get_index(self, remote_root: str, read_password: str | None = None) -> dict:
        text = self.get_text(join_remote_path(remote_root, INDEX_NAME), read_password=read_password)
        return parse_index(text)

    def get_index_text(self, remote_root: str, read_password: str | None = None) -> str:
        return self.get_text(join_remote_path(remote_root, INDEX_NAME), read_password=read_password)

    def stat_paths(self, remote_paths: list[str]) -> dict[str, dict]:
        cleaned = [str(path).strip() for path in remote_paths if str(path).strip()]
        if not cleaned:
            return {}
        obj = self._http_json(self.server_base + "/_/rpc/exists", {"paths": cleaned})
        if not obj.get("ok", False):
            raise RuntimeError(obj)
        statuses: dict[str, dict] = {}
        for item in obj.get("paths", []):
            if not isinstance(item, dict):
                continue
            path = item.get("path")
            if isinstance(path, str) and path:
                statuses[path] = item
        return statuses


def cmd_push(args: argparse.Namespace) -> int:
    local_dir = Path(args.local_dir).resolve()
    if not local_dir.is_dir():
        print(f"error: local directory not found: {local_dir}", file=sys.stderr)
        return 1

    remote_root = normalize_remote_root(args.remote_root)
    client = PurePathClient(
        args.server,
        rpc=not args.no_rpc,
        timeout=args.timeout,
        read_password=args.read_password,
        write_password=args.write_password,
    )

    try:
        health = client.health()
        if not health.get("ok"):
            print("warning: health check returned non-ok", file=sys.stderr)
    except Exception as e:
        print(f"error: server health check failed: {e}", file=sys.stderr)
        return 1

    if args.init_access:
        try:
            client.set_access(
                remote_root,
                read_password=args.new_read_password,
                write_password_new=args.new_write_password,
                write_password_current=args.current_write_password,
            )
            print(f"access updated: {remote_root}/.access")
        except error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")
            print(f"failed to set access: HTTP {e.code}: {body}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"failed to set access: {e}", file=sys.stderr)
            return 1

    entries: list[FileEntry] = []
    uploaded = 0
    skipped = 0

    for path in walk_text_files(local_dir):
        rel = rel_path_posix(local_dir, path)

        if not args.include_local_access and PurePosixPath(rel).name == ACCESS_NAME:
            skipped += 1
            continue

        if PurePosixPath(rel).name == INDEX_NAME:
            skipped += 1
            continue

        try:
            text = read_utf8_text(path)
        except UnicodeDecodeError:
            print(f"skip (not valid UTF-8): {path}", file=sys.stderr)
            skipped += 1
            continue
        except Exception as e:
            print(f"skip ({e}): {path}", file=sys.stderr)
            skipped += 1
            continue

        remote_path = join_remote_path(remote_root, rel)
        try:
            client.set_text(remote_path, text, write_password=args.write_password)
        except error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")
            print(f"upload failed: {path} -> {remote_path}: HTTP {e.code}: {body}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"upload failed: {path} -> {remote_path}: {e}", file=sys.stderr)
            return 1

        entries.append(FileEntry(path=rel, sha256=sha256_text(text), bytes=len(text.encode("utf-8"))))
        uploaded += 1
        print(f"uploaded: {rel}")

    index_text = build_index(entries, remote_root)
    index_remote_path = join_remote_path(remote_root, INDEX_NAME)

    try:
        client.set_text(index_remote_path, index_text, write_password=args.write_password)
    except Exception as e:
        print(f"failed to upload index file: {e}", file=sys.stderr)
        return 1

    print(f"\nindex uploaded: {index_remote_path}")
    print(f"files uploaded: {uploaded}")
    print(f"files skipped:  {skipped}")

    if args.set_access_after:
        try:
            after_client = PurePathClient(
                args.server,
                rpc=not args.no_rpc,
                timeout=args.timeout,
                write_password=args.current_write_password_after,
            )
            after_client.set_access(
                remote_root,
                read_password=args.after_read_password,
                write_password_new=args.after_write_password,
                write_password_current=args.current_write_password_after,
            )
            print(f"access updated after push: {remote_root}/.access")
        except error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")
            print(f"failed to set access after push: HTTP {e.code}: {body}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"failed to set access after push: {e}", file=sys.stderr)
            return 1

    return 0


def cmd_pull(args: argparse.Namespace) -> int:
    local_dir = Path(args.local_dir).resolve()
    local_dir.mkdir(parents=True, exist_ok=True)

    remote_root = normalize_remote_root(args.remote_root)
    client = PurePathClient(
        args.server,
        rpc=not args.no_rpc,
        timeout=args.timeout,
        read_password=args.read_password,
        write_password=args.write_password,
    )

    index_remote_path = join_remote_path(remote_root, INDEX_NAME)

    try:
        index_text = client.get_text(index_remote_path, read_password=args.read_password)
    except error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        print(f"failed to fetch index file: HTTP {e.code}: {body}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"failed to fetch index file: {e}", file=sys.stderr)
        return 1

    try:
        index_obj = parse_index(index_text)
    except Exception as e:
        print(f"invalid index file: {e}", file=sys.stderr)
        return 1

    pulled = 0
    failed = 0

    for item in index_obj["files"]:
        rel = item["path"]
        expected_sha = item["sha256"]

        try:
            rel_pp = PurePosixPath(rel)
            if rel_pp.is_absolute():
                raise ValueError("absolute path in index")
            if ".." in rel_pp.parts or "." in rel_pp.parts:
                raise ValueError("unsafe relative path in index")

            remote_path = join_remote_path(remote_root, rel)
            text = client.get_text(remote_path, read_password=args.read_password)
            actual_sha = sha256_text(text)
            if actual_sha != expected_sha:
                raise ValueError(f"hash mismatch for {rel}: expected {expected_sha}, got {actual_sha}")

            local_path = local_dir.joinpath(*rel_pp.parts)
            write_utf8_text(local_path, text)
            pulled += 1
            print(f"downloaded: {rel}")
        except Exception as e:
            failed += 1
            print(f"failed: {rel}: {e}", file=sys.stderr)

    try:
        write_utf8_text(local_dir / INDEX_NAME, index_text)
    except Exception as e:
        print(f"warning: failed to write local index file: {e}", file=sys.stderr)

    print(f"\nfiles downloaded: {pulled}")
    print(f"files failed:     {failed}")
    return 1 if failed else 0


def cmd_set_access(args: argparse.Namespace) -> int:
    client = PurePathClient(
        args.server,
        rpc=not args.no_rpc,
        timeout=args.timeout,
        write_password=args.current_write_password,
    )

    target_path = normalize_remote_root(args.remote_path)

    try:
        result = client.set_access(
            target_path,
            read_password=args.read_password,
            write_password_new=args.write_password_new,
            write_password_current=args.current_write_password,
        )
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        print(f"failed: HTTP {e.code}: {body}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"failed: {e}", file=sys.stderr)
        return 1


def cmd_show_index(args: argparse.Namespace) -> int:
    client = PurePathClient(
        args.server,
        rpc=not args.no_rpc,
        timeout=args.timeout,
        read_password=args.read_password,
        write_password=args.write_password,
    )
    try:
        text = client.get_index_text(args.remote_root, read_password=args.read_password)
        print(text, end="")
        return 0
    except error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        print(f"failed: HTTP {e.code}: {body}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"failed: {e}", file=sys.stderr)
        return 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Push/pull directory trees to/from a pure-path text server")
    p.add_argument("--server", required=True, help="Server base URL, e.g. http://127.0.0.1:8080")
    p.add_argument("--timeout", type=int, default=30, help="HTTP timeout in seconds")
    p.add_argument("--no-rpc", action="store_true", help="Use direct GET/PUT where possible")
    p.add_argument("--read-password", help="Read password for protected subtree")
    p.add_argument("--write-password", help="Write password for protected subtree")

    sub = p.add_subparsers(dest="cmd", required=True)

    push = sub.add_parser("push", help="Upload a directory tree and create index")
    push.add_argument("local_dir", help="Local source directory")
    push.add_argument("remote_root", help="Remote root path, e.g. /projectA")
    push.add_argument(
        "--include-local-access",
        action="store_true",
        help="Include local files named .access during push",
    )
    push.add_argument(
        "--init-access",
        action="store_true",
        help="Create/update remote .access before upload",
    )
    push.add_argument(
        "--new-read-password",
        help="Read password to set when using --init-access",
    )
    push.add_argument(
        "--new-write-password",
        help="Write password to set when using --init-access",
    )
    push.add_argument(
        "--current-write-password",
        help="Current write password needed to modify existing .access before upload",
    )
    push.add_argument(
        "--set-access-after",
        action="store_true",
        help="Create/update remote .access after upload",
    )
    push.add_argument(
        "--after-read-password",
        help="Read password to set after upload",
    )
    push.add_argument(
        "--after-write-password",
        help="Write password to set after upload",
    )
    push.add_argument(
        "--current-write-password-after",
        help="Current write password needed to modify .access after upload",
    )
    push.set_defaults(func=cmd_push)

    pull = sub.add_parser("pull", help="Download a directory tree using index")
    pull.add_argument("remote_root", help="Remote root path, e.g. /projectA")
    pull.add_argument("local_dir", help="Local destination directory")
    pull.set_defaults(func=cmd_pull)

    access = sub.add_parser("set-access", help="Create or update a remote .access file")
    access.add_argument("remote_path", help="Target directory path, e.g. /projectA")
    access.add_argument("--read-password", dest="read_password", help="New read password to set")
    access.add_argument("--write-password-new", help="New write password to set")
    access.add_argument("--current-write-password", help="Current write password if the subtree is already protected")
    access.set_defaults(func=cmd_set_access)

    show_index = sub.add_parser("show-index", help="Fetch and print remote .index")
    show_index.add_argument("remote_root", help="Remote root path, e.g. /projectA")
    show_index.set_defaults(func=cmd_show_index)

    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
