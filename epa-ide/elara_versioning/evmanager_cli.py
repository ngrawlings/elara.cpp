#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path, PurePosixPath
from typing import Any

from evmanager_core import (
    APP_NAME,
    APP_VERSION,
    DEFAULT_IGNORE_PATTERNS,
    PROJECT_DIR_NAME,
    PROJECT_META_NAME,
    ProjectConfig,
    ProjectRepo,
    apply_workspace_bundle,
    build_workspace_bundle,
    decrypt_blob_with_password,
    encrypt_blob_with_password,
    load_global_projects,
    normalize_ignore_pattern_lines,
    normalize_remote_root,
    read_utf8_text,
    register_global_project,
    safe_json_dump,
    save_global_preferences,
    save_global_projects,
    touch_global_project,
)
from evclient import PurePathClient

LARGE_COMMIT_FILE_THRESHOLD = 100 * 1024 * 1024


def print_json(obj: Any) -> None:
    print(json.dumps(obj, ensure_ascii=False, indent=2))


def eprint(*args: Any) -> None:
    print(*args, file=sys.stderr)


def format_file_size(size: int) -> str:
    units = ["B", "KiB", "MiB", "GiB", "TiB"]
    value = float(size)
    unit = units[0]
    for candidate in units:
        unit = candidate
        if value < 1024 or candidate == units[-1]:
            break
        value /= 1024
    if unit == "B":
        return f"{int(value)} {unit} ({size:,} bytes)"
    return f"{value:.1f} {unit} ({size:,} bytes)"


def confirm_large_commit_files(repo: ProjectRepo, status: dict[str, Any]) -> list[str]:
    excluded_paths: list[str] = []
    for rel_path in [*status["added"], *status["modified"]]:
        path = repo.project_root.joinpath(*PurePosixPath(rel_path).parts)
        try:
            size = path.stat().st_size
        except OSError:
            continue
        if size <= LARGE_COMMIT_FILE_THRESHOLD:
            continue
        if not sys.stdin.isatty():
            raise RuntimeError(
                "Large file confirmation is required, but stdin is not interactive: "
                f"{rel_path} ({format_file_size(size)})"
            )
        while True:
            eprint(f"Large file detected: {rel_path}")
            eprint(f"Size: {format_file_size(size)}")
            answer = input("Include this file in the commit? [y/N]: ").strip().lower()
            if answer in ("y", "yes"):
                break
            if answer in ("", "n", "no"):
                excluded_paths.append(rel_path)
                break
            eprint("Please answer y or n.")
    return excluded_paths


def discover_project_root(start: Path) -> Path | None:
    current = start.resolve()
    if current.is_file():
        current = current.parent
    for candidate in [current, *current.parents]:
        if (candidate / PROJECT_DIR_NAME / PROJECT_META_NAME).exists():
            return candidate
    return None


def load_repo_from_args(args: argparse.Namespace) -> ProjectRepo:
    project = getattr(args, "project", None)
    if project:
        return ProjectRepo(Path(project))
    discovered = discover_project_root(Path.cwd())
    if discovered is None:
        raise RuntimeError("No project specified and no .project/project.json found in the current directory tree")
    return ProjectRepo(discovered)


def status_payload(repo: ProjectRepo, include_text: bool = False) -> dict[str, Any]:
    status = repo.status()
    payload: dict[str, Any] = {
        "project": repo.config.to_dict(),
        "baseline": status["baseline"],
        "counts": {k: len(status[k]) for k in ("added", "modified", "deleted", "unchanged")},
        "added": status["added"],
        "modified": status["modified"],
        "deleted": status["deleted"],
        "unchanged": status["unchanged"],
    }
    if include_text:
        lines = []
        for key in ("added", "modified", "deleted", "unchanged"):
            lines.append(f"[{key}]")
            values = status[key]
            if values:
                lines.extend(values)
            else:
                lines.append("(none)")
            lines.append("")
        payload["text"] = "\n".join(lines).rstrip() + "\n"
    return payload


def transport_text_for_rel(repo: ProjectRepo, rel_path: str) -> str:
    path = repo.project_root.joinpath(*PurePosixPath(rel_path).parts)
    return repo.working_snapshot() and __import__('evmanager_core').read_project_file_for_transport(path)  # type: ignore


def cmd_init(args: argparse.Namespace) -> int:
    project_root = Path(args.local_dir).expanduser().resolve()
    ignore_patterns = normalize_ignore_pattern_lines("\n".join(args.ignore or DEFAULT_IGNORE_PATTERNS))
    repo = ProjectRepo.init_project(
        project_root=project_root,
        name=args.name or project_root.name,
        server=args.server.rstrip("/"),
        remote_root=normalize_remote_root(args.remote_root),
        branch=args.branch,
        rpc=not args.no_rpc,
        timeout=args.timeout,
        read_password=args.read_password,
        write_password=args.write_password,
        include_local_access=args.include_local_access,
        ignore_patterns=ignore_patterns,
    )
    touch_global_project(repo.config.local_dir)
    print_json({"ok": True, "project_root": str(project_root), "config": repo.config.to_dict()})
    return 0


def cmd_projects_list(_args: argparse.Namespace) -> int:
    print_json({"projects": load_global_projects()})
    return 0


def cmd_config_show(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json({
        "config": repo.config.to_dict(),
        "ignore_patterns": repo.read_ignore_patterns(),
        "ignore_file": str(repo.ignore_file_path()),
    })
    return 0


def cmd_config_set(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    cfg = ProjectConfig.from_dict(repo.config.to_dict())

    if args.name is not None:
        cfg.name = args.name
    if args.server is not None:
        cfg.server = args.server.rstrip("/")
    if args.remote_root is not None:
        cfg.remote_root = normalize_remote_root(args.remote_root)
    if args.branch is not None:
        cfg.current_branch = args.branch
    if args.timeout is not None:
        cfg.timeout = args.timeout
    if args.rpc is not None:
        cfg.rpc = args.rpc
    if args.read_password is not None:
        cfg.read_password = args.read_password or None
    if args.write_password is not None:
        cfg.write_password = args.write_password or None
    if args.include_local_access is not None:
        cfg.include_local_access = args.include_local_access

    repo.config = cfg
    repo.save_config()
    repo.ensure_branch(cfg.current_branch)

    if args.ignore_reset_defaults:
        repo.write_ignore_patterns(list(DEFAULT_IGNORE_PATTERNS))
    if args.ignore_add:
        patterns = repo.read_ignore_patterns()
        patterns.extend(args.ignore_add)
        repo.write_ignore_patterns(normalize_ignore_pattern_lines("\n".join(patterns)))
    if args.ignore_replace is not None:
        repo.write_ignore_patterns(normalize_ignore_pattern_lines("\n".join(args.ignore_replace)))

    print_json({
        "ok": True,
        "config": repo.config.to_dict(),
        "ignore_patterns": repo.read_ignore_patterns(),
    })
    return 0


def cmd_branch_list(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json({"current_branch": repo.current_branch(), "branches": repo.list_branches()})
    return 0


def cmd_branch_create(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    repo.ensure_branch(args.name)
    if args.switch:
        repo.switch_branch(args.name)
    print_json({"ok": True, "current_branch": repo.current_branch(), "branches": repo.list_branches()})
    return 0


def cmd_branch_switch(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    repo.switch_branch(args.name)
    touch_global_project(repo.config.local_dir)
    print_json({"ok": True, "current_branch": repo.current_branch()})
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    payload = status_payload(repo, include_text=not args.json)
    if args.diff:
        import difflib
        rel_path = args.diff.replace("\\", "/").lstrip("/")
        baseline_source, baseline_snapshot = repo.baseline_snapshot()
        current_path = repo.project_root.joinpath(*PurePosixPath(rel_path).parts)
        current_text = ""
        if current_path.exists() and current_path.is_file() and not current_path.is_symlink():
            from evmanager_core import read_project_file_for_transport
            current_text = read_project_file_for_transport(current_path)
        baseline_text = ""
        baseline_item = baseline_snapshot.get(rel_path)
        if baseline_item:
            remote_root = repo.load_last_sync_state().get("remote_files_root") if repo.load_last_sync_state() else repo.remote_branch_root()
            if baseline_source == "last_sync" and remote_root:
                try:
                    client = repo.make_client()
                    from evclient import join_remote_path
                    baseline_text = client.get_text(join_remote_path(remote_root, rel_path))
                except Exception:
                    baseline_text = ""
            elif baseline_source == "head":
                # Head only stores hashes, not content.
                baseline_text = ""
        diff_lines = list(difflib.unified_diff(
            baseline_text.splitlines(),
            current_text.splitlines(),
            fromfile=f"baseline/{rel_path}",
            tofile=f"working/{rel_path}",
            lineterm="",
        ))
        payload["diff_path"] = rel_path
        payload["diff"] = "\n".join(diff_lines) + ("\n" if diff_lines else "")
    if args.json:
        print_json(payload)
    else:
        print(payload["text"], end="")
        if args.diff:
            print(payload["diff"], end="")
    return 0


def cmd_commit(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    status = repo.status()
    excluded_paths = confirm_large_commit_files(repo, status)
    remaining_added = [path for path in status["added"] if path not in excluded_paths]
    remaining_modified = [path for path in status["modified"] if path not in excluded_paths]
    if not (remaining_added or remaining_modified or status["deleted"]):
        eprint("No working tree changes remain after excluding large files; creating an empty snapshot commit.")
    author = args.author or os.environ.get("USER") or os.environ.get("USERNAME") or "user"
    meta = repo.commit(message=args.message, author=author, bug_ids=args.bug or [], excluded_paths=excluded_paths)
    print_json(meta)
    return 0


def cmd_commits_list(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    commits = repo.list_commits(args.branch)
    print_json({"branch": args.branch or repo.current_branch(), "commits": commits})
    return 0


def cmd_commits_show(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    meta = repo.local_commit_metadata(args.commit_id, args.branch)
    if meta is None:
        remote_summary = repo.load_local_remote_commit_summary(args.branch)
        if remote_summary:
            for item in remote_summary.get("commits", []):
                if str(item.get("commit_id")) == args.commit_id:
                    print_json(item)
                    return 0
        raise RuntimeError(f"Commit not found: {args.commit_id}")
    print_json(meta)
    return 0


def cmd_push(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json(repo.push_current_tree())
    return 0


def cmd_pull(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json(repo.pull_current_tree())
    return 0


def cmd_bugs_list(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json({"bugs": repo.list_bugs(), "pending_sync": repo.load_pending_bug_sync().get("pending_bug_ids", [])})
    return 0


def cmd_bugs_show(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json(repo.load_bug(args.bug_id))
    return 0


def cmd_bugs_create(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    author = args.author or os.environ.get("USER") or os.environ.get("USERNAME") or "user"
    print_json(repo.create_bug(args.title, description=args.description or "", severity=args.severity, status=args.status, author=author))
    return 0


def cmd_bugs_comment(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    author = args.author or os.environ.get("USER") or os.environ.get("USERNAME") or "user"
    print_json(repo.add_bug_comment(args.bug_id, args.text, author=author))
    return 0


def cmd_bugs_update(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json(repo.update_bug(args.bug_id, title=args.title, description=args.description, severity=args.severity, status=args.status))
    return 0


def cmd_bugs_sync(args: argparse.Namespace) -> int:
    repo = load_repo_from_args(args)
    print_json(repo.sync_pending_bugs())
    return 0


def cmd_workspace_export(args: argparse.Namespace) -> int:
    bundle = build_workspace_bundle()
    encrypted_blob = encrypt_blob_with_password(safe_json_dump(bundle).encode("utf-8"), args.bundle_password)
    client = PurePathClient(args.server.rstrip("/"), rpc=not args.no_rpc, timeout=args.timeout, write_password=args.write_password)
    client.set_text(args.remote_path, encrypted_blob.decode("utf-8"))
    save_global_preferences({
        "export": {
            "server": args.server.rstrip("/"),
            "remote_path": args.remote_path,
            "write_password": args.write_password or "",
            "rpc": not args.no_rpc,
            "timeout": args.timeout,
        }
    })
    print_json({"ok": True, "server": args.server.rstrip("/"), "remote_path": args.remote_path, "files": len(bundle.get("files", {}))})
    return 0


def cmd_workspace_import(args: argparse.Namespace) -> int:
    client = PurePathClient(args.server.rstrip("/"), rpc=not args.no_rpc, timeout=args.timeout, read_password=args.read_password)
    encrypted_text = client.get_text(args.remote_path)
    bundle_text = decrypt_blob_with_password(encrypted_text.encode("utf-8"), args.bundle_password).decode("utf-8")
    bundle = json.loads(bundle_text)

    mappings: dict[str, str] = {}
    for item in args.map or []:
        if "=" not in item:
            raise RuntimeError(f"Invalid mapping: {item}")
        old, new = item.split("=", 1)
        mappings[old.strip()] = str(Path(new.strip()).expanduser().resolve())

    def resolver(item: dict[str, Any], _prompt: str) -> str | None:
        current_dir = str(item.get("local_dir", "")).strip()
        if current_dir in mappings:
            return mappings[current_dir]
        if args.default_project_root:
            return str((Path(args.default_project_root).expanduser().resolve() / Path(current_dir).name).resolve())
        if args.non_interactive:
            raise RuntimeError(f"No mapping provided for project path: {current_dir}")
        response = input(f"Map project '{item.get('name', Path(current_dir).name)}' from '{current_dir}' to local directory: ").strip()
        return response or None

    result = apply_workspace_bundle(bundle, project_dir_resolver=resolver)
    print_json(result)
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=f"{APP_NAME} CLI {APP_VERSION}")
    p.add_argument("--project", help="Project root. Defaults to the nearest parent containing .project/project.json")
    sub = p.add_subparsers(dest="command", required=True)

    init = sub.add_parser("init", help="Create a new managed project")
    init.add_argument("local_dir")
    init.add_argument("--name")
    init.add_argument("--server", required=True)
    init.add_argument("--remote-root", required=True)
    init.add_argument("--branch", default="main")
    init.add_argument("--timeout", type=int, default=30)
    init.add_argument("--no-rpc", action="store_true")
    init.add_argument("--read-password")
    init.add_argument("--write-password")
    init.add_argument("--include-local-access", action="store_true")
    init.add_argument("--ignore", action="append", help="Add ignore pattern. Can be repeated")
    init.set_defaults(func=cmd_init)

    projects = sub.add_parser("projects", help="Global project registry")
    projects_sub = projects.add_subparsers(dest="projects_cmd", required=True)
    projects_list = projects_sub.add_parser("list", help="List saved projects")
    projects_list.set_defaults(func=cmd_projects_list)

    config = sub.add_parser("config", help="Show or update project config")
    config_sub = config.add_subparsers(dest="config_cmd", required=True)
    config_show = config_sub.add_parser("show", help="Show config")
    config_show.set_defaults(func=cmd_config_show)
    config_set = config_sub.add_parser("set", help="Update config")
    config_set.add_argument("--name")
    config_set.add_argument("--server")
    config_set.add_argument("--remote-root")
    config_set.add_argument("--branch")
    config_set.add_argument("--timeout", type=int)
    config_set.add_argument("--rpc", dest="rpc", action="store_true")
    config_set.add_argument("--no-rpc", dest="rpc", action="store_false")
    config_set.add_argument("--read-password")
    config_set.add_argument("--write-password")
    config_set.add_argument("--include-local-access", dest="include_local_access", action="store_true")
    config_set.add_argument("--exclude-local-access", dest="include_local_access", action="store_false")
    config_set.add_argument("--ignore-add", action="append")
    config_set.add_argument("--ignore-replace", action="append")
    config_set.add_argument("--ignore-reset-defaults", action="store_true")
    config_set.set_defaults(func=cmd_config_set, rpc=None, include_local_access=None)

    branch = sub.add_parser("branch", help="Branch operations")
    branch_sub = branch.add_subparsers(dest="branch_cmd", required=True)
    branch_list = branch_sub.add_parser("list")
    branch_list.set_defaults(func=cmd_branch_list)
    branch_create = branch_sub.add_parser("create")
    branch_create.add_argument("name")
    branch_create.add_argument("--switch", action="store_true")
    branch_create.set_defaults(func=cmd_branch_create)
    branch_switch = branch_sub.add_parser("switch")
    branch_switch.add_argument("name")
    branch_switch.set_defaults(func=cmd_branch_switch)

    status = sub.add_parser("status", help="Working tree status")
    status.add_argument("--json", action="store_true")
    status.add_argument("--diff", help="Show unified diff for one relative path")
    status.set_defaults(func=cmd_status)

    commit = sub.add_parser("commit", help="Create a commit")
    commit.add_argument("-m", "--message", required=True)
    commit.add_argument("--author")
    commit.add_argument("--bug", action="append", help="Link bug id. Can be repeated")
    commit.set_defaults(func=cmd_commit)

    commits = sub.add_parser("commits", help="List or show commits")
    commits_sub = commits.add_subparsers(dest="commits_cmd", required=True)
    commits_list = commits_sub.add_parser("list")
    commits_list.add_argument("--branch")
    commits_list.set_defaults(func=cmd_commits_list)
    commits_show = commits_sub.add_parser("show")
    commits_show.add_argument("commit_id")
    commits_show.add_argument("--branch")
    commits_show.set_defaults(func=cmd_commits_show)

    push = sub.add_parser("push", help="Push current tree")
    push.set_defaults(func=cmd_push)

    pull = sub.add_parser("pull", help="Pull current branch")
    pull.set_defaults(func=cmd_pull)

    bugs = sub.add_parser("bugs", help="Bug tracker")
    bugs_sub = bugs.add_subparsers(dest="bugs_cmd", required=True)
    bugs_list = bugs_sub.add_parser("list")
    bugs_list.set_defaults(func=cmd_bugs_list)
    bugs_show = bugs_sub.add_parser("show")
    bugs_show.add_argument("bug_id")
    bugs_show.set_defaults(func=cmd_bugs_show)
    bugs_create = bugs_sub.add_parser("create")
    bugs_create.add_argument("title")
    bugs_create.add_argument("--description")
    bugs_create.add_argument("--severity", default="medium")
    bugs_create.add_argument("--status", default="open")
    bugs_create.add_argument("--author")
    bugs_create.set_defaults(func=cmd_bugs_create)
    bugs_comment = bugs_sub.add_parser("comment")
    bugs_comment.add_argument("bug_id")
    bugs_comment.add_argument("text")
    bugs_comment.add_argument("--author")
    bugs_comment.set_defaults(func=cmd_bugs_comment)
    bugs_update = bugs_sub.add_parser("update")
    bugs_update.add_argument("bug_id")
    bugs_update.add_argument("--title")
    bugs_update.add_argument("--description")
    bugs_update.add_argument("--severity")
    bugs_update.add_argument("--status")
    bugs_update.set_defaults(func=cmd_bugs_update)
    bugs_sync = bugs_sub.add_parser("sync-pending")
    bugs_sync.set_defaults(func=cmd_bugs_sync)

    workspace = sub.add_parser("workspace", help="Workspace config backup")
    workspace_sub = workspace.add_subparsers(dest="workspace_cmd", required=True)
    workspace_export = workspace_sub.add_parser("export")
    workspace_export.add_argument("--server", required=True)
    workspace_export.add_argument("--remote-path", required=True)
    workspace_export.add_argument("--bundle-password", required=True)
    workspace_export.add_argument("--write-password")
    workspace_export.add_argument("--timeout", type=int, default=30)
    workspace_export.add_argument("--no-rpc", action="store_true")
    workspace_export.set_defaults(func=cmd_workspace_export)
    workspace_import = workspace_sub.add_parser("import")
    workspace_import.add_argument("--server", required=True)
    workspace_import.add_argument("--remote-path", required=True)
    workspace_import.add_argument("--bundle-password", required=True)
    workspace_import.add_argument("--read-password")
    workspace_import.add_argument("--timeout", type=int, default=30)
    workspace_import.add_argument("--no-rpc", action="store_true")
    workspace_import.add_argument("--map", action="append", help="Map old project path to new path as old=new")
    workspace_import.add_argument("--default-project-root", help="Base directory for imported projects when no explicit mapping is given")
    workspace_import.add_argument("--non-interactive", action="store_true")
    workspace_import.set_defaults(func=cmd_workspace_import)

    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args) or 0)
    except Exception as exc:
        eprint(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
