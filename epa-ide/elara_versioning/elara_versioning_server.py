#!/usr/bin/env python3
"""
Pure-path text server with configurable multi-root overlay storage.

Features
--------
- GET  /some/path           -> serves plain text from first matching root
- PUT  /some/path           -> replaces plain text body at selected write root
- POST /some/path           -> same as PUT, but accepts text/plain or JSON {"text": "..."}
- GET  /_/ui                -> small HTML control panel
- GET  /_/edit?path=/x/y    -> edit form for one path
- POST /_/edit              -> save from HTML form
- POST /_/rpc/get           -> JSON RPC get
- POST /_/rpc/set           -> JSON RPC set
- POST /_/rpc/list          -> JSON RPC list immediate children
- POST /_/rpc/exists        -> JSON RPC batch path existence + lock state
- POST /_/rpc/copy          -> JSON RPC server-side copy
- POST /_/rpc/move          -> JSON RPC server-side move
- POST /_/rpc/set_symlinks  -> JSON RPC create/update logical symlinks in batch
- POST /_/rpc/lock          -> JSON RPC permanently lock one or more paths against modification
- GET  /_/health            -> health check

Storage model
-------------
The server can use either:
- a single --base-dir
- or a JSON config file passed with --config

Config file supports:
- ordered root list, highest priority first
- missing roots are skipped
- pinned path prefixes that always live on a specific root

Example config:
{
  "roots": [
    "/srv/pastebin_local",
    "/media/usbA/pastebin",
    "/media/usbB/pastebin"
  ],
  "pinned_paths": {
    "/mynotes": "/media/usbA/pastebin"
  }
}
"""

from __future__ import annotations

import argparse
import hmac
import html
import json
import logging
import os
import sys
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import parse_qs, quote, urlencode, unquote, urlparse


SPECIAL_SHEBANGS = {"#!binary", "#!media", "#!data"}
VALID_VIEWERS = {"text", "image", "graph-line"}
VALID_ENCODINGS = {"", "base64", "hex", "csv"}


def parse_binday_document(text: str) -> dict[str, object] | None:
    lines = text.splitlines()
    if not lines:
        return None

    shebang = lines[0].strip().lower()
    if shebang not in SPECIAL_SHEBANGS:
        return None

    headers: dict[str, str] = {}
    body_start = 1
    for idx in range(1, len(lines)):
        line = lines[idx]
        stripped = line.strip()
        if stripped == "":
            body_start = idx + 1
            break
        if not stripped.startswith("#"):
            body_start = idx
            break
        header_text = stripped[1:].strip()
        if not header_text:
            body_start = idx + 1
            continue
        if ":" in header_text:
            key, value = header_text.split(":", 1)
        elif any(ch.isspace() for ch in header_text):
            key, value = header_text.split(None, 1)
        else:
            key, value = header_text, ""
        headers[key.strip().lower()] = value.strip()
        body_start = idx + 1

    body = "\n".join(lines[body_start:]).strip()
    viewer = headers.get("viewer", "text").strip().lower() or "text"
    if viewer not in VALID_VIEWERS:
        viewer = "text"

    transfer_encoding = headers.get("encoding", headers.get("content-transfer-encoding", "")).strip().lower()
    if transfer_encoding not in VALID_ENCODINGS:
        transfer_encoding = ""

    return {
        "headers": headers,
        "body": body,
        "mimetype": headers.get("mimetype", "application/octet-stream"),
        "viewer": viewer,
        "filename": headers.get("filename", ""),
        "transfer_encoding": transfer_encoding,
        "shebang": shebang,
    }


def _is_recognized_image_mime(mime_type: str) -> bool:
    if not mime_type.startswith("image/"):
        return False
    allowed = {
        "image/png",
        "image/jpeg",
        "image/gif",
        "image/webp",
        "image/svg+xml",
        "image/bmp",
    }
    return mime_type.lower() in allowed



def _parse_csv_col_labels(value: str) -> list[str]:
    return [part.strip() for part in str(value).split(",") if part.strip()]


def _parse_last_cols(value: str) -> int | None:
    try:
        parsed = int(str(value).strip())
    except (TypeError, ValueError):
        return None
    return max(0, parsed)


def _parse_range_cols_header(value: str) -> tuple[list[str], set[int]]:
    labels: list[str] = []
    indexes: set[int] = set()
    for raw_part in str(value).split(","):
        part = raw_part.strip()
        if not part:
            continue
        labels.append(part)
        if ":" in part:
            start_text, end_text = part.split(":", 1)
            try:
                start = int(start_text.strip())
                end = int(end_text.strip())
            except ValueError:
                continue
            if start <= 0 or end <= 0:
                continue
            if end < start:
                start, end = end, start
            for idx in range(start, end + 1):
                indexes.add(idx)
        else:
            try:
                single = int(part)
            except ValueError:
                continue
            if single > 0:
                indexes.add(single)
    return labels, indexes


def _select_series_indices(
    available_count: int,
    only_cols: list[str],
    ignore_cols: set[str],
    range_col_indexes: set[int],
    last_cols: int | None,
) -> list[int]:
    selected = list(range(1, available_count + 1))

    if range_col_indexes:
        selected = [idx for idx in selected if idx in range_col_indexes]

    if last_cols is not None:
        if last_cols <= 0:
            selected = []
        else:
            start_idx = max(1, available_count - last_cols + 1)
            selected = [idx for idx in selected if idx >= start_idx]

    if only_cols:
        requested_indexes: list[int] = []
        for name in only_cols:
            try:
                index = int(name)
            except ValueError:
                continue
            if 1 <= index <= available_count:
                requested_indexes.append(index)
        if requested_indexes:
            allowed = set(requested_indexes)
            selected = [idx for idx in selected if idx in allowed]
            order_map = {idx: pos for pos, idx in enumerate(requested_indexes)}
            selected.sort(key=lambda idx: order_map.get(idx, len(requested_indexes)))
        else:
            selected = []

    ignored_indexes = set()
    for name in ignore_cols:
        try:
            index = int(name)
        except ValueError:
            continue
        if 1 <= index <= available_count:
            ignored_indexes.add(index)
    if ignored_indexes:
        selected = [idx for idx in selected if idx not in ignored_indexes]

    return selected


def render_default_viewer_html(text: str) -> str:
    binday = parse_binday_document(text)
    if binday is None:
        return (
            '<div class="box"><h2>Viewer</h2><pre>'
            + html.escape(text)
            + '</pre></div>'
        )

    mime_type = str(binday.get("mimetype", "application/octet-stream")).strip().lower()
    viewer = str(binday.get("viewer", "text")).strip().lower() or "text"
    body = str(binday.get("body", ""))
    encoding = str(binday.get("transfer_encoding", "")).strip().lower()
    filename = str(binday.get("filename", ""))
    headers = binday.get("headers", {})
    if not isinstance(headers, dict):
        headers = {}
    ignore_cols = set(_parse_csv_col_labels(headers.get("ignore-cols", "")))
    only_cols = _parse_csv_col_labels(headers.get("only-cols", ""))
    range_cols_labels, range_col_indexes = _parse_range_cols_header(headers.get("range-cols", ""))
    last_cols = _parse_last_cols(headers.get("last-cols", ""))
    rename_cols: dict[str, str] = {}
    for part in str(headers.get("rename-cols", "")).split(","):
        item = part.strip()
        if not item or "=" not in item:
            continue
        src, dst = item.split("=", 1)
        src = src.strip()
        dst = dst.strip()
        if src:
            rename_cols[src] = dst or src
    scale_cols: dict[str, float] = {}
    for part in str(headers.get("scale-cols", "")).split(","):
        item = part.strip()
        if not item or "=" not in item:
            continue
        src, factor_text = item.split("=", 1)
        src = src.strip()
        factor_text = factor_text.strip()
        if not src:
            continue
        try:
            scale_cols[src] = float(factor_text)
        except ValueError:
            continue
    series_colors = [
        part.strip() for part in str(headers.get("series-colors", "")).split(",") if part.strip()
    ]
    nolegend = str(headers.get("nolegend", "")).strip().lower() in {"1", "true", "yes", "on", ""} if "nolegend" in headers else False
    legend_mode = str(headers.get("legend", "")).strip().lower() or ("off" if nolegend else "bottom")
    if nolegend:
        legend_mode = "off"
    valid_legend_modes = {"off", "top", "bottom", "top-left", "top-right", "bottom-left", "bottom-right"}
    if legend_mode not in valid_legend_modes:
        legend_mode = "bottom"
    chart_title = str(headers.get("title", "")).strip()
    x_label = str(headers.get("x-label", "")).strip()
    y_label = str(headers.get("y-label", "")).strip()
    try:
        x_rotate = float(str(headers.get("x-rotate", "0")).strip() or "0")
    except ValueError:
        x_rotate = 0.0
    try:
        y_min_override = float(str(headers.get("y-min", "")).strip()) if str(headers.get("y-min", "")).strip() else None
    except ValueError:
        y_min_override = None
    try:
        y_max_override = float(str(headers.get("y-max", "")).strip()) if str(headers.get("y-max", "")).strip() else None
    except ValueError:
        y_max_override = None
    try:
        width_override = float(str(headers.get("width", "")).strip()) if str(headers.get("width", "")).strip() else None
    except ValueError:
        width_override = None
    try:
        height_override = float(str(headers.get("height", "")).strip()) if str(headers.get("height", "")).strip() else None
    except ValueError:
        height_override = None

    meta = [
        f'<p class="muted"><strong>Viewer:</strong> <code>{html.escape(viewer)}</code></p>',
        f'<p class="muted"><strong>MIME type:</strong> <code>{html.escape(mime_type)}</code></p>',
    ]
    if filename:
        meta.append(f'<p class="muted"><strong>Filename:</strong> <code>{html.escape(filename)}</code></p>')
    if range_cols_labels:
        meta.append(
            f'<p class="muted"><strong>Range columns:</strong> <code>{html.escape(", ".join(range_cols_labels))}</code></p>'
        )
    if last_cols is not None:
        meta.append(
            f'<p class="muted"><strong>Last columns:</strong> <code>{html.escape(str(last_cols))}</code></p>'
        )
    if only_cols:
        meta.append(
            f'<p class="muted"><strong>Only columns:</strong> <code>{html.escape(", ".join(only_cols))}</code></p>'
        )
    if ignore_cols:
        meta.append(
            f'<p class="muted"><strong>Ignored columns:</strong> <code>{html.escape(", ".join(sorted(ignore_cols)))}</code></p>'
        )
    if rename_cols:
        meta.append(
            f'<p class="muted"><strong>Renamed columns:</strong> <code>{html.escape(", ".join(f"{k}={v}" for k, v in rename_cols.items()))}</code></p>'
        )
    if scale_cols:
        meta.append(
            f'<p class="muted"><strong>Scaled columns:</strong> <code>{html.escape(", ".join(f"{k}={v:g}" for k, v in scale_cols.items()))}</code></p>'
        )
    if series_colors:
        meta.append(
            f'<p class="muted"><strong>Series colours:</strong> <code>{html.escape(", ".join(series_colors))}</code></p>'
        )
    if chart_title:
        meta.append(f'<p class="muted"><strong>Title:</strong> <code>{html.escape(chart_title)}</code></p>')
    if x_label:
        meta.append(f'<p class="muted"><strong>X label:</strong> <code>{html.escape(x_label)}</code></p>')
    if y_label:
        meta.append(f'<p class="muted"><strong>Y label:</strong> <code>{html.escape(y_label)}</code></p>')
    if legend_mode != "bottom":
        meta.append(f'<p class="muted"><strong>Legend:</strong> <code>{html.escape(legend_mode)}</code></p>')
    if x_rotate:
        meta.append(f'<p class="muted"><strong>X rotate:</strong> <code>{html.escape(f"{x_rotate:g}")}</code></p>')
    if y_min_override is not None or y_max_override is not None:
        parts = []
        if y_min_override is not None:
            parts.append(f"min={y_min_override:g}")
        if y_max_override is not None:
            parts.append(f"max={y_max_override:g}")
        meta.append(f'<p class="muted"><strong>Y range:</strong> <code>{html.escape(", ".join(parts))}</code></p>')
    if width_override is not None:
        meta.append(f'<p class="muted"><strong>Width:</strong> <code>{html.escape(f"{width_override:g}")}</code></p>')
    if height_override is not None:
        meta.append(f'<p class="muted"><strong>Height:</strong> <code>{html.escape(f"{height_override:g}")}</code></p>')

    if viewer == "image" and encoding == "base64" and _is_recognized_image_mime(mime_type):
        alt_text = html.escape(filename or "image")
        src = f'data:{html.escape(mime_type)};base64,{html.escape(body)}'
        return (
            '<div class="box"><h2>Viewer</h2>'
            + ''.join(meta)
            + f'<img alt="{alt_text}" src="{src}" style="max-width:100%; height:auto; border:1px solid #ddd; border-radius:8px;">'
            + '</div>'
        )

    if viewer == "graph-line" and mime_type == "text/csv":
        escaped_csv = html.escape(body)
        meta_html = ''.join(meta)
        rows = [line.strip() for line in body.splitlines() if line.strip()]
        parsed_series = []
        x_labels = []
        series_names = []

        if rows:
            split_rows = [[p.strip() for p in row.split(',')] for row in rows]
            header = split_rows[0]
            data_rows = split_rows[1:]
            has_header = len(header) >= 2
            if has_header:
                try:
                    float(header[1])
                    has_header = False
                except ValueError:
                    has_header = True

            kept_original_names: list[str] = []
            if has_header:
                keep_indices = [0]
                kept_original_names = []
                selected_header_names = header[1:]

                if range_col_indexes:
                    selected_header_names = [
                        name for idx, name in enumerate(selected_header_names, start=1)
                        if idx in range_col_indexes
                    ]

                if last_cols is not None:
                    if last_cols <= 0:
                        selected_header_names = []
                    else:
                        selected_header_names = selected_header_names[-last_cols:]

                if only_cols:
                    ordered_only = [name for name in only_cols if name in selected_header_names]
                    only_lookup = set(ordered_only)
                    for idx, name in enumerate(header[1:], start=1):
                        if name in only_lookup:
                            keep_indices.append(idx)
                            kept_original_names.append(name)
                else:
                    selected_lookup = set(selected_header_names)
                    for idx, name in enumerate(header[1:], start=1):
                        if name in selected_lookup and name not in ignore_cols:
                            keep_indices.append(idx)
                            kept_original_names.append(name)

                if only_cols and ignore_cols:
                    filtered_keep_indices = [0]
                    filtered_names = []
                    for idx, name in zip(keep_indices[1:], kept_original_names):
                        if name not in ignore_cols:
                            filtered_keep_indices.append(idx)
                            filtered_names.append(name)
                    keep_indices = filtered_keep_indices
                    kept_original_names = filtered_names

                header = [header[idx] for idx in keep_indices]
                data_rows = [
                    [row[idx] if idx < len(row) else "" for idx in keep_indices]
                    for row in data_rows
                ]
                display_names = [rename_cols.get(name, name) or f"series {idx}" for idx, name in enumerate(kept_original_names, start=1)]
                series_names = display_names
            else:
                if len(header) >= 2:
                    data_rows = split_rows
                    keep_data_indexes = _select_series_indices(
                        available_count=max(0, len(header) - 1),
                        only_cols=only_cols,
                        ignore_cols=ignore_cols,
                        range_col_indexes=range_col_indexes,
                        last_cols=last_cols,
                    )
                    if keep_data_indexes:
                        data_rows = [
                            [row[0]] + [row[idx] if idx < len(row) else "" for idx in keep_data_indexes]
                            for row in data_rows
                        ]
                    else:
                        data_rows = [[row[0]] for row in data_rows]
                    kept_original_names = [f"series {idx}" for idx in keep_data_indexes]
                    series_names = [rename_cols.get(name, name) for name in kept_original_names]

            series_count = len(series_names)
            parsed_series = [[] for _ in range(series_count)]

            for row in data_rows:
                if len(row) < 2:
                    continue
                x_labels.append(row[0])
                for idx in range(series_count):
                    y_raw = row[idx + 1] if idx + 1 < len(row) else ""
                    try:
                        y_value = float(y_raw)
                        original_name = kept_original_names[idx] if idx < len(kept_original_names) else series_names[idx]
                        if original_name in scale_cols:
                            y_value *= scale_cols[original_name]
                    except ValueError:
                        y_value = None
                    parsed_series[idx].append(y_value)

        valid_series = []
        for idx, values in enumerate(parsed_series):
            numeric_points = sum(1 for value in values if value is not None)
            if numeric_points >= 2:
                valid_series.append((series_names[idx], values))

        if len(x_labels) >= 2 and valid_series:
            width = max(240.0, width_override if width_override is not None else 800.0)
            height = max(180.0, height_override if height_override is not None else 320.0)
            pad_left = 64.0
            pad_right = 24.0
            pad_top = 20.0
            pad_bottom = 42.0
            if chart_title:
                pad_top += 26.0
            if x_label:
                pad_bottom += 24.0
            if x_rotate:
                pad_bottom += 18.0
            if legend_mode == "bottom":
                pad_bottom += 20.0 * (((len(valid_series) - 1) // 3) + 1)
            elif legend_mode == "top":
                pad_top += 20.0 * (((len(valid_series) - 1) // 3) + 1)
            elif legend_mode in {"top-left", "top-right", "bottom-left", "bottom-right"}:
                pad_right += 140.0
            if y_label:
                pad_left += 22.0

            plot_width = width - pad_left - pad_right
            plot_height = height - pad_top - pad_bottom
            all_ys = [value for _, values in valid_series for value in values if value is not None]
            min_y = min(all_ys)
            max_y = max(all_ys)
            if y_min_override is not None:
                min_y = y_min_override
            if y_max_override is not None:
                max_y = y_max_override
            if min_y == max_y:
                min_y -= 1.0
                max_y += 1.0
            if min_y > max_y:
                min_y, max_y = max_y, min_y

            palette = series_colors or [
                "#0a66c2", "#c2410c", "#15803d", "#7c3aed", "#b91c1c",
                "#0891b2", "#a16207", "#be185d", "#4f46e5", "#047857",
            ]

            axis_y = pad_top + plot_height
            svg_parts = [
                f'<svg viewBox="0 0 {int(width)} {int(height)}" width="{int(width)}" height="{int(height)}" role="img" aria-label="Line chart" style="display:block;">',
            ]
            if chart_title:
                svg_parts.append(f'<text x="{width / 2:.2f}" y="22" font-size="16" text-anchor="middle" font-weight="bold">{html.escape(chart_title)}</text>')
            svg_parts.extend([
                f'<line x1="{pad_left}" y1="{axis_y}" x2="{width - pad_right}" y2="{axis_y}" stroke="#888" />',
                f'<line x1="{pad_left}" y1="{pad_top}" x2="{pad_left}" y2="{axis_y}" stroke="#888" />',
            ])

            for grid_idx in range(5):
                frac = grid_idx / 4.0
                y = pad_top + (frac * plot_height)
                value = max_y - (frac * (max_y - min_y))
                svg_parts.append(
                    f'<line x1="{pad_left}" y1="{y:.2f}" x2="{width - pad_right}" y2="{y:.2f}" stroke="#eee" />'
                )
                svg_parts.append(
                    f'<text x="{pad_left - 8}" y="{y + 4:.2f}" font-size="12" text-anchor="end">{html.escape(f"{value:g}")}</text>'
                )

            point_count = len(x_labels)
            for series_idx, (series_name, values) in enumerate(valid_series):
                color = palette[series_idx % len(palette)]
                poly_points = []
                marker_parts = []
                for idx, y_value in enumerate(values):
                    if y_value is None or point_count <= 1:
                        continue
                    x = pad_left + (idx * plot_width / (point_count - 1))
                    y = axis_y - ((y_value - min_y) * plot_height / (max_y - min_y))
                    poly_points.append(f"{x:.2f},{y:.2f}")
                    marker_parts.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="2.5" fill="{color}" />')
                if len(poly_points) >= 2:
                    svg_parts.append(
                        f'<polyline fill="none" stroke="{color}" stroke-width="2" points="{" ".join(poly_points)}" />'
                    )
                    svg_parts.extend(marker_parts)

            if point_count >= 2:
                first_x = pad_left
                last_x = width - pad_right
                if x_rotate:
                    svg_parts.append(
                        f'<text x="{first_x:.2f}" y="{height - 10:.2f}" font-size="12" text-anchor="start" transform="rotate({-x_rotate:g} {first_x:.2f},{height - 10:.2f})">{html.escape(str(x_labels[0]))}</text>'
                    )
                    svg_parts.append(
                        f'<text x="{last_x:.2f}" y="{height - 10:.2f}" font-size="12" text-anchor="end" transform="rotate({-x_rotate:g} {last_x:.2f},{height - 10:.2f})">{html.escape(str(x_labels[-1]))}</text>'
                    )
                else:
                    svg_parts.append(f'<text x="{first_x:.2f}" y="{height - 8}" font-size="12">{html.escape(str(x_labels[0]))}</text>')
                    svg_parts.append(f'<text x="{last_x:.2f}" y="{height - 8}" font-size="12" text-anchor="end">{html.escape(str(x_labels[-1]))}</text>')

            if x_label:
                svg_parts.append(f'<text x="{width / 2:.2f}" y="{height - 4:.2f}" font-size="12" text-anchor="middle">{html.escape(x_label)}</text>')
            if y_label:
                svg_parts.append(f'<text x="18" y="{pad_top + (plot_height / 2):.2f}" font-size="12" text-anchor="middle" transform="rotate(-90 18,{pad_top + (plot_height / 2):.2f})">{html.escape(y_label)}</text>')

            if legend_mode != "off":
                legend_rows = ((len(valid_series) - 1) // 3) + 1
                if legend_mode == "bottom":
                    base_x = pad_left + 8
                    base_y = height - 12
                    for series_idx, (series_name, _values) in enumerate(valid_series):
                        color = palette[series_idx % len(palette)]
                        legend_x = base_x + ((series_idx % 3) * 160)
                        legend_y = base_y - ((series_idx // 3) * 18)
                        svg_parts.append(f'<line x1="{legend_x}" y1="{legend_y - 4}" x2="{legend_x + 20}" y2="{legend_y - 4}" stroke="{color}" stroke-width="3" />')
                        svg_parts.append(f'<text x="{legend_x + 26}" y="{legend_y}" font-size="12">{html.escape(series_name)}</text>')
                elif legend_mode == "top":
                    base_x = pad_left + 8
                    base_y = pad_top - 8
                    for series_idx, (series_name, _values) in enumerate(valid_series):
                        color = palette[series_idx % len(palette)]
                        legend_x = base_x + ((series_idx % 3) * 160)
                        legend_y = base_y - ((legend_rows - 1 - (series_idx // 3)) * 18)
                        svg_parts.append(f'<line x1="{legend_x}" y1="{legend_y - 4}" x2="{legend_x + 20}" y2="{legend_y - 4}" stroke="{color}" stroke-width="3" />')
                        svg_parts.append(f'<text x="{legend_x + 26}" y="{legend_y}" font-size="12">{html.escape(series_name)}</text>')
                else:
                    box_x = width - pad_right + 8
                    if legend_mode in {"top-left", "bottom-left"}:
                        box_x = 8
                    if legend_mode in {"top-left", "top-right"}:
                        box_y = pad_top + 8
                    else:
                        box_y = axis_y - (18 * len(valid_series))
                    for series_idx, (series_name, _values) in enumerate(valid_series):
                        color = palette[series_idx % len(palette)]
                        legend_y = box_y + (series_idx * 18)
                        svg_parts.append(f'<line x1="{box_x}" y1="{legend_y - 4}" x2="{box_x + 20}" y2="{legend_y - 4}" stroke="{color}" stroke-width="3" />')
                        svg_parts.append(f'<text x="{box_x + 26}" y="{legend_y}" font-size="12">{html.escape(series_name)}</text>')

            svg_parts.append('</svg>')
            svg = ''.join(svg_parts)
            chart_html = (
                f'<div style="width:100%; overflow-x:auto; border:1px solid #ddd; border-radius:8px; '
                f'padding:0.5rem; box-sizing:border-box; min-height:{int(height) + 40}px;">'
                f'<div style="min-width:{int(width)}px; width:{int(width)}px;">{svg}</div>'
                f'</div>'
            )
        else:
            missing_requested = []
            if only_cols and has_header:
                missing_requested = [name for name in only_cols if name not in header and name not in kept_original_names]
            missing_html = ''
            if missing_requested:
                missing_html = '<p class="muted">Requested columns not found: <code>' + html.escape(', '.join(missing_requested)) + '</code></p>'
            empty_height = int(max(180.0, height_override if height_override is not None else 320.0))
            empty_width = int(max(240.0, width_override if width_override is not None else 800.0))
            chart_html = (
                f'<div style="width:100%; overflow-x:auto; border:1px solid #ddd; border-radius:8px; '
                f'padding:0.75rem; box-sizing:border-box; min-height:{empty_height}px;">'
                f'<div style="min-width:{empty_width}px;">{missing_html}<p class="muted">Need at least two CSV rows and one numeric series, for example <code>x,a,b</code>.</p></div>'
                f'</div>'
            )

        return f"""
<div class="box">
  <h2>Viewer</h2>
  {meta_html}
  {chart_html}
  <details style="margin-top:0.75rem;">
    <summary>CSV data</summary>
    <pre>{escaped_csv}</pre>
  </details>
</div>
"""
    fallback_reason = []
    if viewer == "image" and not _is_recognized_image_mime(mime_type):
        fallback_reason.append("image viewer requires a recognised image MIME type")
    if viewer == "graph-line" and mime_type != "text/csv":
        fallback_reason.append("graph-line viewer requires MIME type text/csv")
    if viewer == "image" and encoding != "base64":
        fallback_reason.append("image viewer requires base64 content")

    reason_html = ""
    if fallback_reason:
        reason_html = '<p class="muted">Fell back to text viewer: ' + html.escape('; '.join(fallback_reason)) + '</p>'

    return (
        '<div class="box"><h2>Viewer</h2>'
        + ''.join(meta)
        + reason_html
        + '<pre>' + html.escape(text) + '</pre></div>'
    )


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8080
DEFAULT_LOG_FILE = "pastebin.log"
CONTENT_FILENAME = "content.txt"
ACCESS_FILENAME = ".access"
SYMLINK_FILENAME = ".symlink"
LOCK_FILENAME = ".locked"
MAX_SYMLINK_DEPTH = 32


logger = logging.getLogger("pastebin")




def configure_logging(log_file: Path) -> Path:
    resolved = log_file.expanduser().resolve()
    resolved.parent.mkdir(parents=True, exist_ok=True)

    formatter = logging.Formatter(
        fmt="%(asctime)s %(levelname)s [%(threadName)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    logger.handlers.clear()
    logger.setLevel(logging.INFO)
    logger.propagate = False

    file_handler = logging.FileHandler(resolved, encoding="utf-8")
    file_handler.setFormatter(formatter)
    logger.addHandler(file_handler)

    stderr_handler = logging.StreamHandler(sys.stderr)
    stderr_handler.setFormatter(formatter)
    logger.addHandler(stderr_handler)

    return resolved


def format_headers(headers) -> dict[str, str]:
    return {key: value for key, value in headers.items()}

def json_bytes(obj: dict) -> bytes:
    return json.dumps(obj, ensure_ascii=False, indent=2).encode("utf-8")


def html_page(title: str, body: str) -> bytes:
    doc = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{html.escape(title)}</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {{
      font-family: sans-serif;
      max-width: 1000px;
      margin: 2rem auto;
      padding: 0 1rem;
    }}
    textarea {{
      width: 100%;
      min-height: 20rem;
      font-family: monospace;
    }}
    input[type=text] {{
      width: 100%;
      font-family: monospace;
    }}
    .muted {{ color: #666; }}
    .box {{
      border: 1px solid #ccc;
      padding: 1rem;
      border-radius: 8px;
      margin: 1rem 0;
    }}
    code {{
      background: #f4f4f4;
      padding: 0.15rem 0.35rem;
      border-radius: 4px;
    }}
    button {{
      padding: 0.6rem 1rem;
      cursor: pointer;
    }}
    table {{
      border-collapse: collapse;
      width: 100%;
    }}
    th, td {{
      text-align: left;
      padding: 0.4rem 0.6rem;
      border-bottom: 1px solid #ddd;
      vertical-align: top;
    }}
  </style>
</head>
<body>
{body}
</body>
</html>"""
    return doc.encode("utf-8")


def _first_form_value(params: dict[str, list[str]], key: str) -> str:
    value = params.get(key, [""])[0]
    return value if isinstance(value, str) else str(value)


class PathValidationError(ValueError):
    pass


class AccessDeniedError(PermissionError):
    pass


class StorageConfigError(ValueError):
    pass


class PurePathMapper:
    def __init__(self, roots: list[Path], pinned_paths: dict[str, Path] | None = None):
        if not roots:
            raise StorageConfigError("At least one storage root is required")
        self.roots = [root.resolve() for root in roots]
        self.pinned_paths = {
            self._normalize_prefix(prefix): root.resolve()
            for prefix, root in (pinned_paths or {}).items()
        }

    def validate_url_path(self, raw_path: str) -> str:
        if not raw_path:
            raise PathValidationError("Path is empty")

        parsed = urlparse(raw_path)
        path = parsed.path

        if not path.startswith("/"):
            raise PathValidationError("Path must start with '/'")

        decoded = unquote(path)

        if "\\" in decoded:
            raise PathValidationError("Backslashes are not allowed")

        if any(ord(ch) < 32 for ch in decoded):
            raise PathValidationError("Control characters are not allowed")

        if decoded == "/":
            return "/"

        if "//" in decoded:
            raise PathValidationError("Repeated slashes are not allowed")
        if decoded.endswith("/"):
            raise PathValidationError("Trailing slash is not allowed except for root")

        pure = PurePosixPath(decoded)
        parts = pure.parts
        if not parts or parts[0] != "/":
            raise PathValidationError("Invalid absolute path")

        segments = parts[1:]
        if not segments:
            return "/"

        for seg in segments:
            if seg in ("", ".", ".."):
                raise PathValidationError("Dot segments are not allowed")
            if "/" in seg or "\\" in seg:
                raise PathValidationError("Invalid segment")

        canonical = "/" + "/".join(segments)
        if unquote(quote(canonical, safe="/")) != canonical:
            raise PathValidationError("Path failed round-trip validation")
        return canonical

    def _normalize_prefix(self, prefix: str) -> str:
        normalized = self.validate_url_path(prefix)
        return normalized

    def _root_available(self, root: Path) -> bool:
        return root.is_dir()

    def _safe_dir_for_root(self, root: Path, canonical_path: str) -> Path:
        if canonical_path == "/":
            fs_dir = root.resolve()
        else:
            fs_dir = (root / canonical_path.lstrip("/")).resolve()
        try:
            fs_dir.relative_to(root)
        except ValueError:
            raise PathValidationError("Resolved path escaped storage root")
        return fs_dir

    def _match_pinned_root(self, canonical_path: str) -> tuple[str, Path] | None:
        best_prefix = None
        best_root = None
        for prefix, root in self.pinned_paths.items():
            if canonical_path == prefix or canonical_path.startswith(prefix + "/"):
                if best_prefix is None or len(prefix) > len(best_prefix):
                    best_prefix = prefix
                    best_root = root
        if best_prefix is None or best_root is None:
            return None
        return best_prefix, best_root

    def roots_status(self) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for idx, root in enumerate(self.roots):
            rows.append({
                "priority": idx,
                "root": str(root),
                "available": self._root_available(root),
            })
        return rows

    def resolve_read_content_file(self, raw_path: str) -> tuple[str, Path]:
        canonical = self.validate_url_path(raw_path)

        pinned = self._match_pinned_root(canonical)
        if pinned is not None:
            _, root = pinned
            if self._root_available(root):
                fs_dir = self._safe_dir_for_root(root, canonical)
                content_file = fs_dir / CONTENT_FILENAME
                if content_file.exists():
                    return canonical, content_file
            raise FileNotFoundError(canonical)

        for root in self.roots:
            if not self._root_available(root):
                continue
            fs_dir = self._safe_dir_for_root(root, canonical)
            content_file = fs_dir / CONTENT_FILENAME
            if content_file.exists():
                return canonical, content_file

        raise FileNotFoundError(canonical)

    def resolve_write_content_file(self, raw_path: str) -> tuple[str, Path]:
        canonical = self.validate_url_path(raw_path)

        pinned = self._match_pinned_root(canonical)
        if pinned is not None:
            prefix, root = pinned
            if not self._root_available(root):
                raise FileNotFoundError(f"Pinned root unavailable for {canonical}: {root} (prefix {prefix})")
            fs_dir = self._safe_dir_for_root(root, canonical)
            return canonical, fs_dir / CONTENT_FILENAME

        for root in self.roots:
            if not self._root_available(root):
                continue
            fs_dir = self._safe_dir_for_root(root, canonical)
            return canonical, fs_dir / CONTENT_FILENAME

        raise FileNotFoundError("No available storage roots")

    def resolve_read_dir(self, raw_path: str) -> tuple[str, Path]:
        canonical = self.validate_url_path(raw_path)

        pinned = self._match_pinned_root(canonical)
        if pinned is not None:
            _, root = pinned
            if self._root_available(root):
                fs_dir = self._safe_dir_for_root(root, canonical)
                if fs_dir.exists():
                    return canonical, fs_dir
            raise FileNotFoundError(canonical)

        for root in self.roots:
            if not self._root_available(root):
                continue
            fs_dir = self._safe_dir_for_root(root, canonical)
            if fs_dir.exists():
                return canonical, fs_dir

        raise FileNotFoundError(canonical)

    def resolve_write_dir(self, raw_path: str) -> tuple[str, Path]:
        canonical = self.validate_url_path(raw_path)

        pinned = self._match_pinned_root(canonical)
        if pinned is not None:
            prefix, root = pinned
            if not self._root_available(root):
                raise FileNotFoundError(f"Pinned root unavailable for {canonical}: {root} (prefix {prefix})")
            return canonical, self._safe_dir_for_root(root, canonical)

        for root in self.roots:
            if not self._root_available(root):
                continue
            return canonical, self._safe_dir_for_root(root, canonical)

        raise FileNotFoundError("No available storage roots")

    def list_child_dirs(self, raw_path: str) -> tuple[str, list[str]]:
        canonical = self.validate_url_path(raw_path)
        seen: set[str] = set()
        children: list[str] = []

        pinned = self._match_pinned_root(canonical)
        if pinned is not None:
            roots_to_check = [pinned[1]]
        else:
            roots_to_check = self.roots

        for root in roots_to_check:
            if not self._root_available(root):
                continue
            base_dir = self._safe_dir_for_root(root, canonical)
            if not base_dir.exists() or not base_dir.is_dir():
                continue
            for child in sorted(base_dir.iterdir(), key=lambda p: p.name):
                if not child.is_dir():
                    continue
                child_path = canonical.rstrip("/")
                if child_path == "":
                    child_path = "/"
                full = (child_path if child_path != "/" else "") + "/" + child.name
                if full in seen:
                    continue
                seen.add(full)
                children.append(full)
        return canonical, children


class TextStore:
    def __init__(self, mapper: PurePathMapper):
        self.mapper = mapper

    def _assert_not_access_control_path(self, canonical: str) -> None:
        if ACCESS_FILENAME in PurePosixPath(canonical.lstrip("/")).parts:
            raise AccessDeniedError(f"Direct access to {ACCESS_FILENAME} is not allowed")

    def _access_file_path(self, fs_dir: Path) -> Path:
        return fs_dir / ACCESS_FILENAME

    def _load_access_policy(self, access_file: Path) -> dict[str, str | None]:
        try:
            obj = json.loads(access_file.read_text(encoding="utf-8"))
        except Exception as exc:
            raise ValueError(f"Invalid access policy at {access_file}: {exc}") from exc
        if not isinstance(obj, dict):
            raise ValueError(f"Invalid access policy at {access_file}: expected object")
        read_password = obj.get("read_password")
        write_password = obj.get("write_password")
        if read_password is not None and not isinstance(read_password, str):
            raise ValueError(f"Invalid access policy at {access_file}: read_password must be a string or null")
        if write_password is not None and not isinstance(write_password, str):
            raise ValueError(f"Invalid access policy at {access_file}: write_password must be a string or null")
        return {
            "read_password": read_password,
            "write_password": write_password,
        }

    def _find_effective_access_policy(self, fs_dir: Path) -> tuple[Path, dict[str, str | None]] | None:
        current = fs_dir.resolve()
        stop_roots = {root.resolve() for root in self.mapper.roots}
        while True:
            access_file = self._access_file_path(current)
            if access_file.exists():
                return current, self._load_access_policy(access_file)
            if current in stop_roots:
                return None
            parent = current.parent
            if parent == current:
                return None
            current = parent

    def _check_password(self, provided: str | None, expected: str | None) -> bool:
        if expected is None:
            return True
        if provided is None:
            return False
        return hmac.compare_digest(provided, expected)

    def _enforce_read_access(self, fs_dir: Path, read_password: str | None) -> None:
        match = self._find_effective_access_policy(fs_dir)
        if match is None:
            return
        policy_dir, policy = match
        if self._check_password(read_password, policy.get("read_password")):
            return
        raise AccessDeniedError(f"Read access denied by {policy_dir / ACCESS_FILENAME}")

    def _enforce_write_access(self, fs_dir: Path, write_password: str | None) -> None:
        match = self._find_effective_access_policy(fs_dir)
        if match is None:
            return
        policy_dir, policy = match
        if self._check_password(write_password, policy.get("write_password")):
            return
        raise AccessDeniedError(f"Write access denied by {policy_dir / ACCESS_FILENAME}")

    def _read_symlink_target(self, fs_dir: Path) -> str | None:
        marker = fs_dir / SYMLINK_FILENAME
        if not marker.exists():
            return None
        target = marker.read_text(encoding="utf-8").strip()
        if not target:
            raise ValueError(f"Empty symlink target in {marker}")
        return self.mapper.validate_url_path(target)

    def _resolve_read_content_via_symlinks(self, raw_path: str, read_password: str | None = None) -> tuple[str, Path]:
        current = self.mapper.validate_url_path(raw_path)
        self._assert_not_access_control_path(current)
        visited: set[str] = set()

        for _ in range(MAX_SYMLINK_DEPTH):
            if current in visited:
                raise ValueError(f"Symlink loop detected at {current}")
            visited.add(current)

            canonical, fs_dir = self.mapper.resolve_read_dir(current)
            self._enforce_read_access(fs_dir, read_password)
            marker = fs_dir / SYMLINK_FILENAME
            if marker.exists():
                current = self._read_symlink_target(fs_dir)
                continue

            content_file = fs_dir / CONTENT_FILENAME
            if content_file.exists():
                return canonical, content_file
            raise FileNotFoundError(canonical)

        raise ValueError(f"Symlink resolution exceeded {MAX_SYMLINK_DEPTH} hops")

    def _resolve_read_dir_via_symlinks(self, raw_path: str, read_password: str | None = None) -> tuple[str, Path]:
        current = self.mapper.validate_url_path(raw_path)
        self._assert_not_access_control_path(current)
        visited: set[str] = set()

        for _ in range(MAX_SYMLINK_DEPTH):
            if current in visited:
                raise ValueError(f"Symlink loop detected at {current}")
            visited.add(current)

            canonical, fs_dir = self.mapper.resolve_read_dir(current)
            self._enforce_read_access(fs_dir, read_password)
            marker = fs_dir / SYMLINK_FILENAME
            if marker.exists():
                current = self._read_symlink_target(fs_dir)
                continue
            return canonical, fs_dir

        raise ValueError(f"Symlink resolution exceeded {MAX_SYMLINK_DEPTH} hops")

    def _remove_existing_path_state(self, fs_dir: Path) -> None:
        if fs_dir.is_symlink() or fs_dir.is_file():
            raise ValueError(f"Path storage collision at {fs_dir}")
        content_file = fs_dir / CONTENT_FILENAME
        symlink_file = fs_dir / SYMLINK_FILENAME
        if content_file.exists():
            content_file.unlink()
        if symlink_file.exists():
            symlink_file.unlink()

    def _lock_marker(self, fs_dir: Path) -> Path:
        return fs_dir / LOCK_FILENAME

    def is_locked(self, raw_path: str) -> tuple[str, bool]:
        canonical, fs_dir = self.mapper.resolve_write_dir(raw_path)
        return canonical, self._lock_marker(fs_dir).exists()

    def _ensure_not_locked_path(self, raw_path: str) -> str:
        canonical, locked = self.is_locked(raw_path)
        if locked:
            raise PermissionError(f"Path is permanently locked: {canonical}")
        return canonical

    def lock_path(self, raw_path: str, write_password: str | None = None) -> dict[str, object]:
        canonical, fs_dir = self.mapper.resolve_write_dir(raw_path)
        self._assert_not_access_control_path(canonical)
        self._enforce_write_access(fs_dir, write_password)
        fs_dir.mkdir(parents=True, exist_ok=True)
        marker = self._lock_marker(fs_dir)
        already_locked = marker.exists()
        if not already_locked:
            tmp_file = fs_dir / f".{LOCK_FILENAME}.{os.getpid()}.tmp"
            tmp_file.write_text("locked\n", encoding="utf-8")
            os.replace(tmp_file, marker)
        return {"path": canonical, "locked": True, "already_locked": already_locked}

    def lock_paths(self, raw_paths: list[str], write_password: str | None = None) -> dict[str, object]:
        if not isinstance(raw_paths, list) or not raw_paths:
            raise ValueError("'paths' must be a non-empty array of paths")

        resolved: list[tuple[str, Path]] = []
        seen: set[str] = set()
        for raw_path in raw_paths:
            if not isinstance(raw_path, str):
                raise ValueError("Each entry in 'paths' must be a string")
            canonical, fs_dir = self.mapper.resolve_write_dir(raw_path)
            self._assert_not_access_control_path(canonical)
            self._enforce_write_access(fs_dir, write_password)
            if canonical in seen:
                continue
            seen.add(canonical)
            resolved.append((canonical, fs_dir))

        results: list[dict[str, object]] = []
        for canonical, fs_dir in resolved:
            fs_dir.mkdir(parents=True, exist_ok=True)
            marker = self._lock_marker(fs_dir)
            already_locked = marker.exists()
            if not already_locked:
                tmp_file = fs_dir / f".{LOCK_FILENAME}.{os.getpid()}.tmp"
                tmp_file.write_text("locked\n", encoding="utf-8")
                os.replace(tmp_file, marker)
            results.append({"path": canonical, "locked": True, "already_locked": already_locked})

        return {"paths": results, "count": len(results)}

    def path_status(self, raw_path: str, read_password: str | None = None) -> dict[str, object]:
        canonical = self.mapper.validate_url_path(raw_path)
        self._assert_not_access_control_path(canonical)

        exists = False
        try:
            _resolved_canonical, read_dir = self.mapper.resolve_read_dir(canonical)
            self._enforce_read_access(read_dir, read_password)
        except FileNotFoundError:
            read_dir = None

        if read_dir is not None:
            content_file = read_dir / CONTENT_FILENAME
            symlink_file = read_dir / SYMLINK_FILENAME
            exists = content_file.exists() or symlink_file.exists()

        _write_canonical, write_dir = self.mapper.resolve_write_dir(canonical)
        locked = self._lock_marker(write_dir).exists()
        return {"path": canonical, "exists": exists, "locked": locked}

    def path_statuses(self, raw_paths: list[str], read_password: str | None = None) -> dict[str, object]:
        if not isinstance(raw_paths, list) or not raw_paths:
            raise ValueError("'paths' must be a non-empty array of paths")

        results: list[dict[str, object]] = []
        seen: set[str] = set()
        for raw_path in raw_paths:
            if not isinstance(raw_path, str):
                raise ValueError("Each entry in 'paths' must be a string")
            status = self.path_status(raw_path, read_password=read_password)
            canonical = str(status["path"])
            if canonical in seen:
                continue
            seen.add(canonical)
            results.append(status)

        return {"paths": results, "count": len(results)}

    def read_text(self, raw_path: str, read_password: str | None = None) -> tuple[str, str]:
        canonical, content_file = self._resolve_read_content_via_symlinks(raw_path, read_password=read_password)
        return canonical, content_file.read_text(encoding="utf-8")

    def write_text(self, raw_path: str, text: str, write_password: str | None = None) -> str:
        canonical, content_file = self.mapper.resolve_write_content_file(raw_path)
        self._assert_not_access_control_path(canonical)
        self._enforce_write_access(content_file.parent, write_password)
        if self._lock_marker(content_file.parent).exists():
            raise PermissionError(f"Path is permanently locked: {canonical}")
        content_file.parent.mkdir(parents=True, exist_ok=True)
        symlink_file = content_file.parent / SYMLINK_FILENAME
        if symlink_file.exists():
            symlink_file.unlink()
        tmp_file = content_file.with_suffix(".txt.tmp")
        tmp_file.write_text(text, encoding="utf-8")
        os.replace(tmp_file, content_file)
        return canonical

    def set_symlink(self, raw_path: str, target_path: str, write_password: str | None = None) -> dict[str, object]:
        canonical, fs_dir = self.mapper.resolve_write_dir(raw_path)
        self._assert_not_access_control_path(canonical)
        self._enforce_write_access(fs_dir, write_password)
        if self._lock_marker(fs_dir).exists():
            raise PermissionError(f"Path is permanently locked: {canonical}")
        target_canonical = self.mapper.validate_url_path(target_path)
        self._assert_not_access_control_path(target_canonical)
        if canonical == target_canonical:
            raise ValueError("Symlink target must differ from source path")

        fs_dir.mkdir(parents=True, exist_ok=True)
        self._remove_existing_path_state(fs_dir)
        marker = fs_dir / SYMLINK_FILENAME
        tmp_file = fs_dir / f".{SYMLINK_FILENAME}.{os.getpid()}.tmp"
        tmp_file.write_text(target_canonical + "\n", encoding="utf-8")
        os.replace(tmp_file, marker)

        return {"path": canonical, "target": target_canonical}

    def set_symlinks(self, links: list[dict[str, object]], write_password: str | None = None) -> dict[str, object]:
        results = []
        for item in links:
            if not isinstance(item, dict):
                raise ValueError("Each link must be an object")
            if "path" not in item or "target" not in item:
                raise ValueError("Each link must include 'path' and 'target'")
            path = item["path"]
            target = item["target"]
            if not isinstance(path, str) or not isinstance(target, str):
                raise ValueError("Symlink path and target must be strings")
            results.append(self.set_symlink(path, target, write_password=write_password))
        return {"ok": True, "links": results, "count": len(results)}

    def copy_text(
        self,
        src_path: str,
        dst_path: str,
        *,
        read_password: str | None = None,
        write_password: str | None = None,
    ) -> dict[str, object]:
        src_canonical, src_file = self._resolve_read_content_via_symlinks(src_path, read_password=read_password)
        dst_canonical, dst_file = self.mapper.resolve_write_content_file(dst_path)
        self._assert_not_access_control_path(dst_canonical)
        self._enforce_write_access(dst_file.parent, write_password)
        if self._lock_marker(dst_file.parent).exists():
            raise PermissionError(f"Path is permanently locked: {dst_canonical}")

        dst_file.parent.mkdir(parents=True, exist_ok=True)
        symlink_file = dst_file.parent / SYMLINK_FILENAME
        if symlink_file.exists():
            symlink_file.unlink()
        overwrite = dst_file.exists()
        tmp_file = dst_file.parent / f".{dst_file.name}.{os.getpid()}.copy.tmp"

        with src_file.open("rb") as src_fp, tmp_file.open("wb") as tmp_fp:
            total_bytes = 0
            while True:
                chunk = src_fp.read(1024 * 1024)
                if not chunk:
                    break
                tmp_fp.write(chunk)
                total_bytes += len(chunk)

        os.replace(tmp_file, dst_file)
        return {
            "src_path": src_canonical,
            "dst_path": dst_canonical,
            "bytes": total_bytes,
            "overwrote": overwrite,
        }

    def move_text(
        self,
        src_path: str,
        dst_path: str,
        *,
        read_password: str | None = None,
        write_password: str | None = None,
    ) -> dict[str, object]:
        src_canonical, src_file = self._resolve_read_content_via_symlinks(src_path, read_password=read_password)
        dst_canonical, dst_file = self.mapper.resolve_write_content_file(dst_path)
        self._assert_not_access_control_path(dst_canonical)
        src_dir = src_file.parent
        self._enforce_write_access(src_dir, write_password)
        self._enforce_write_access(dst_file.parent, write_password)
        if self._lock_marker(src_dir).exists():
            raise PermissionError(f"Path is permanently locked: {src_canonical}")
        if self._lock_marker(dst_file.parent).exists():
            raise PermissionError(f"Path is permanently locked: {dst_canonical}")

        dst_file.parent.mkdir(parents=True, exist_ok=True)
        symlink_file = dst_file.parent / SYMLINK_FILENAME
        if symlink_file.exists():
            symlink_file.unlink()
        overwrite = dst_file.exists()
        used_copy_fallback = False

        try:
            total_bytes = src_file.stat().st_size
        except FileNotFoundError:
            raise

        try:
            os.replace(src_file, dst_file)
        except OSError as exc:
            if getattr(exc, "errno", None) != getattr(os, "EXDEV", 18):
                raise
            used_copy_fallback = True
            tmp_file = dst_file.parent / f".{dst_file.name}.{os.getpid()}.move.tmp"
            with src_file.open("rb") as src_fp, tmp_file.open("wb") as tmp_fp:
                total_bytes = 0
                while True:
                    chunk = src_fp.read(1024 * 1024)
                    if not chunk:
                        break
                    tmp_fp.write(chunk)
                    total_bytes += len(chunk)
            os.replace(tmp_file, dst_file)
            src_file.unlink()

        self._cleanup_empty_dirs(src_dir)
        return {
            "src_path": src_canonical,
            "dst_path": dst_canonical,
            "bytes": total_bytes,
            "overwrote": overwrite,
            "used_copy_fallback": used_copy_fallback,
        }

    def _cleanup_empty_dirs(self, start_dir: Path) -> None:
        current = start_dir
        stop_roots = {root.resolve() for root in self.mapper.roots}
        while True:
            resolved = current.resolve()
            if resolved in stop_roots:
                break
            try:
                current.rmdir()
            except OSError:
                break
            parent = current.parent
            if parent == current:
                break
            current = parent

    def list_children(self, raw_path: str, read_password: str | None = None) -> tuple[str, list[str]]:
        canonical, resolved_dir = self._resolve_read_dir_via_symlinks(raw_path, read_password=read_password)
        seen: set[str] = set()
        children: list[str] = []
        for child in sorted(resolved_dir.iterdir(), key=lambda p: p.name):
            if not child.is_dir():
                continue
            child_path = canonical.rstrip("/")
            if child_path == "":
                child_path = "/"
            full = (child_path if child_path != "/" else "") + "/" + child.name
            if full in seen:
                continue
            seen.add(full)
            children.append(full)
        return canonical, children

    def set_access(
        self,
        raw_path: str,
        read_password: str | None,
        write_password_new: str | None,
        write_password_current: str | None = None,
    ) -> dict[str, object]:
        canonical, fs_dir = self.mapper.resolve_write_dir(raw_path)
        self._assert_not_access_control_path(canonical)
        self._enforce_write_access(fs_dir, write_password_current)
        fs_dir.mkdir(parents=True, exist_ok=True)
        access_file = self._access_file_path(fs_dir)
        payload = {
            "read_password": read_password,
            "write_password": write_password_new,
        }
        tmp_file = fs_dir / f".{ACCESS_FILENAME}.{os.getpid()}.tmp"
        tmp_file.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.replace(tmp_file, access_file)
        access_path = canonical.rstrip("/") + "/" + ACCESS_FILENAME if canonical != "/" else "/" + ACCESS_FILENAME
        return {"ok": True, "path": canonical, "access_path": access_path}


class Handler(BaseHTTPRequestHandler):
    server_version = "PurePathTextServer/1.1"

    @property
    def app(self) -> "AppServer":
        return self.server.app  # type: ignore[attr-defined]

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length)

    def _send(self, status: int, body: bytes, content_type: str = "text/plain; charset=utf-8") -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _request_context(self) -> dict[str, object]:
        return {
            "client": self.client_address[0] if self.client_address else None,
            "method": self.command,
            "path": self.path,
            "headers": format_headers(self.headers),
        }

    def _log_exception(self, message: str, exc: Exception) -> None:
        logger.error(
            "%s | context=%s | exc=%s | traceback=\n%s",
            message,
            self._request_context(),
            repr(exc),
            traceback.format_exc(),
        )

    def _send_json(self, status: int, obj: dict) -> None:
        self._send(status, json_bytes(obj), "application/json; charset=utf-8")

    def _send_html(self, status: int, title: str, body: str) -> None:
        self._send(status, html_page(title, body), "text/html; charset=utf-8")

    def _error(self, status: int, message: str) -> None:
        self._send_json(status, {"ok": False, "error": message})

    def _parse_json_body(self) -> dict:
        try:
            return json.loads(self._read_body().decode("utf-8"))
        except Exception as e:
            raise ValueError(f"Invalid JSON body: {e}")

    def _read_password(self) -> str | None:
        value = self.headers.get("X-Read-Password")
        return value if value is not None and value != "" else None

    def _write_password(self) -> str | None:
        value = self.headers.get("X-Write-Password")
        return value if value is not None and value != "" else None

    def _ui_read_password(self, params: dict[str, list[str]] | None = None) -> str | None:
        if params is not None:
            value = _first_form_value(params, "read_password").strip()
            if value:
                return value
        return self._read_password()

    def _ui_write_password(self, params: dict[str, list[str]] | None = None) -> str | None:
        if params is not None:
            value = _first_form_value(params, "write_password").strip()
            if value:
                return value
        return self._write_password()

    def _render_password_prompt(
        self,
        *,
        path: str,
        title: str,
        message: str,
        read_password: str = "",
    ) -> None:
        body = f"""
<h1>{html.escape(title)}</h1>
<div class="box">
  <p>{html.escape(message)}</p>
  <form action="/_/edit" method="get">
    <input type="hidden" name="path" value="{html.escape(path)}">
    <label for="read_password"><strong>Read password</strong></label><br>
    <input id="read_password" name="read_password" type="password" autocomplete="current-password">
    <p><button type="submit">Open editor</button></p>
  </form>
  <p><a href="/_/ui">Back</a></p>
</div>
"""
        self._send_html(403, title, body)

    def _render_edit_form(
        self,
        *,
        canonical: str,
        text: str,
        read_password: str = "",
        error_message: str | None = None,
    ) -> None:
        query = urlencode({"path": canonical, "read_password": read_password}) if read_password else urlencode({"path": canonical})
        error_html = ""
        if error_message:
            error_html = f'<div class="box"><p>{html.escape(error_message)}</p></div>'
        body = f"""
<h1>Edit {html.escape(canonical)}</h1>
{error_html}
<form action="/_/edit" method="get">
  <input type="hidden" name="read_password" value="{html.escape(read_password)}">
  <div class="box">
    <label for="path"><strong>Path</strong></label><br>
    <input id="path" name="path" type="text" value="{html.escape(canonical)}" required>
    <p><button type="submit">Go</button></p>
  </div>
</form>
<form action="/_/edit" method="post">
  <input type="hidden" name="path" value="{html.escape(canonical)}">
  <input type="hidden" name="read_password" value="{html.escape(read_password)}">
  {render_default_viewer_html(text)}
  <div class="box">
    <label for="write_password"><strong>Write password</strong></label><br>
    <input id="write_password" name="write_password" type="password" autocomplete="current-password">
    <p class="muted">Leave blank if the path is not write-protected.</p>
  </div>
  <div class="box">
    <label for="text"><strong>Text</strong></label><br>
    <textarea id="text" name="text">{html.escape(text)}</textarea>
  </div>
  <p><button type="submit">Save</button></p>
</form>
<p><a href="/_/edit?{html.escape(query)}">Reload editor</a></p>
<p><a href="{html.escape(canonical)}" target="_blank">Open raw text</a></p>
<p><a href="/_/ui">Back</a></p>
"""
        self._send_html(200, f"Edit {canonical}", body)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)

        if parsed.path == "/_/health":
            self._send_json(200, {
                "ok": True,
                "status": "healthy",
                "roots": self.app.mapper.roots_status(),
                "pinned_paths": {k: str(v) for k, v in self.app.mapper.pinned_paths.items()},
            })
            return

        if parsed.path == "/_/ui":
            self._render_ui()
            return

        if parsed.path == "/_/edit":
            self._render_edit(parsed.query)
            return

        try:
            canonical, text = self.app.store.read_text(self.path, read_password=self._read_password())
            self._send(200, text.encode("utf-8"), "text/plain; charset=utf-8")
        except FileNotFoundError:
            self._error(404, "No content at that path")
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except Exception as e:
            self._log_exception("Unhandled GET failure", e)
            self._error(500, f"Server error: {e}")

    def do_PUT(self) -> None:
        self._write_direct()

    def do_POST(self) -> None:
        parsed = urlparse(self.path)

        if parsed.path == "/_/edit":
            self._handle_edit_post()
            return
        if parsed.path == "/_/rpc/get":
            self._rpc_get()
            return
        if parsed.path == "/_/rpc/set":
            self._rpc_set()
            return
        if parsed.path == "/_/rpc/set_access":
            self._rpc_set_access()
            return
        if parsed.path == "/_/rpc/list":
            self._rpc_list()
            return
        if parsed.path == "/_/rpc/exists":
            self._rpc_exists()
            return
        if parsed.path == "/_/rpc/copy":
            self._rpc_copy()
            return
        if parsed.path == "/_/rpc/move":
            self._rpc_move()
            return
        if parsed.path == "/_/rpc/set_symlinks":
            self._rpc_set_symlinks()
            return
        if parsed.path == "/_/rpc/lock":
            self._rpc_lock()
            return

        self._write_direct()

    def _write_direct(self) -> None:
        try:
            content_type = self.headers.get("Content-Type", "")
            body = self._read_body()

            if "application/json" in content_type:
                payload = json.loads(body.decode("utf-8"))
                text = payload.get("text", "")
                if not isinstance(text, str):
                    raise ValueError("'text' must be a string")
            else:
                text = body.decode("utf-8")

            canonical = self.app.store.write_text(self.path, text, write_password=self._write_password())
            self._send_json(200, {"ok": True, "path": canonical, "bytes": len(text.encode("utf-8"))})
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except PermissionError as e:
            self._error(423, str(e))
        except FileNotFoundError as e:
            self._error(503, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_get(self) -> None:
        try:
            payload = self._parse_json_body()
            path = payload["path"]
            canonical, text = self.app.store.read_text(path, read_password=self._read_password())
            self._send_json(200, {"ok": True, "path": canonical, "text": text})
        except FileNotFoundError:
            self._error(404, "No content at that path")
        except AccessDeniedError as e:
            self._error(403, str(e))
        except KeyError:
            self._error(400, "Missing 'path'")
        except PathValidationError as e:
            self._error(400, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_set(self) -> None:
        try:
            payload = self._parse_json_body()
            path = payload["path"]
            text = payload["text"]
            if not isinstance(text, str):
                raise ValueError("'text' must be a string")
            canonical = self.app.store.write_text(path, text, write_password=self._write_password())
            self._send_json(200, {"ok": True, "path": canonical})
        except KeyError as e:
            self._error(400, f"Missing field: {e}")
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except PermissionError as e:
            self._error(423, str(e))
        except FileNotFoundError as e:
            self._error(503, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_list(self) -> None:
        try:
            payload = self._parse_json_body()
            path = payload.get("path", "/")
            canonical, children = self.app.store.list_children(path, read_password=self._read_password())
            self._send_json(200, {"ok": True, "path": canonical, "children": children})
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_exists(self) -> None:
        try:
            payload = self._parse_json_body()
            if "paths" in payload:
                result = self.app.store.path_statuses(payload["paths"], read_password=self._read_password())
            elif "path" in payload:
                single = self.app.store.path_status(payload["path"], read_password=self._read_password())
                result = {"paths": [single], "count": 1, **single}
            else:
                raise KeyError("path")
            self._send_json(200, {"ok": True, **result})
        except KeyError:
            self._error(400, "Missing 'path' or 'paths'")
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except FileNotFoundError as e:
            self._error(503, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_set_symlinks(self) -> None:
        try:
            payload = self._parse_json_body()
            links = payload.get("links")
            if not isinstance(links, list):
                raise ValueError("Missing 'links' array")
            result = self.app.store.set_symlinks(links, write_password=self._write_password())
            self._send_json(200, result)
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except PermissionError as e:
            self._error(423, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_lock(self) -> None:
        try:
            payload = self._parse_json_body()
            if "paths" in payload:
                result = self.app.store.lock_paths(payload["paths"], write_password=self._write_password())
            elif "path" in payload:
                single = self.app.store.lock_path(payload["path"], write_password=self._write_password())
                result = {"paths": [single], "count": 1, **single}
            else:
                raise KeyError("path")
            self._send_json(200, {"ok": True, **result})
        except KeyError:
            self._error(400, "Missing 'path' or 'paths'")
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except PermissionError as e:
            self._error(423, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _rpc_set_access(self) -> None:
        try:
            payload = self._parse_json_body()
            path = payload["path"]
            read_password = payload.get("read_password")
            write_password_new = payload.get("write_password_new")
            write_password_current = payload.get("write_password_current", self._write_password())
            if read_password is not None and not isinstance(read_password, str):
                raise ValueError("'read_password' must be a string or null")
            if write_password_new is not None and not isinstance(write_password_new, str):
                raise ValueError("'write_password_new' must be a string or null")
            if write_password_current is not None and not isinstance(write_password_current, str):
                raise ValueError("'write_password_current' must be a string or null")
            result = self.app.store.set_access(
                path,
                read_password=read_password,
                write_password_new=write_password_new,
                write_password_current=write_password_current,
            )
            self._send_json(200, result)
        except KeyError as e:
            self._error(400, f"Missing field: {e}")
        except AccessDeniedError as e:
            self._error(403, str(e))
        except PathValidationError as e:
            self._error(400, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def _render_ui(self) -> None:
        rows = []
        for row in self.app.mapper.roots_status():
            rows.append(
                f"<tr><td>{row['priority']}</td><td><code>{html.escape(str(row['root']))}</code></td>"
                f"<td>{'yes' if row['available'] else 'no'}</td></tr>"
            )
        pinned_rows = []
        for prefix, root in sorted(self.app.mapper.pinned_paths.items()):
            pinned_rows.append(
                f"<tr><td><code>{html.escape(prefix)}</code></td><td><code>{html.escape(str(root))}</code></td></tr>"
            )

        body = f"""
<h1>Elara Versioning Server</h1>
<div class="box">
  <p class="muted">This server maps a URL path to ordered storage roots and stores the text in <code>content.txt</code>.</p>
  <form action="/_/edit" method="get">
    <label for="path"><strong>Path</strong></label><br>
    <input id="path" name="path" type="text" value="/example/note" required>
    <p><button type="submit">Open editor</button></p>
  </form>
</div>

<div class="box">
  <h2>Root priority</h2>
  <table>
    <tr><th>Priority</th><th>Root</th><th>Available</th></tr>
    {''.join(rows) if rows else '<tr><td colspan="3">No roots configured</td></tr>'}
  </table>
</div>

<div class="box">
  <h2>Pinned paths</h2>
  <table>
    <tr><th>Path prefix</th><th>Forced root</th></tr>
    {''.join(pinned_rows) if pinned_rows else '<tr><td colspan="2">None</td></tr>'}
  </table>
</div>

<div class="box">
  <h2>RPC examples</h2>
  <pre>curl -X POST http://127.0.0.1:8080/_/rpc/set \\
  -H 'Content-Type: application/json' \\
  -d '{{"path":"/example/note","text":"hello"}}'

curl -X POST http://127.0.0.1:8080/_/rpc/get \\
  -H 'Content-Type: application/json' \\
  -d '{{"path":"/example/note"}}'

curl -X POST http://127.0.0.1:8080/_/rpc/exists \\
  -H 'Content-Type: application/json' \\
  -d '{{"paths":["/example/note","/example/other"]}}'

curl http://127.0.0.1:8080/example/note

curl -X PUT http://127.0.0.1:8080/example/note \\
  -H 'Content-Type: text/plain; charset=utf-8' \\
  --data-binary 'new text'

curl -X POST http://127.0.0.1:8080/_/rpc/lock \
  -H 'Content-Type: application/json' \
  -d '{{"path":"/example/note"}}'

curl -X POST http://127.0.0.1:8080/_/rpc/lock \
  -H 'Content-Type: application/json' \
  -d '{{"paths":["/commits/a","/commits/b","/commits/c"]}}'</pre>
</div>
"""
        self._send_html(200, "Elara Versioning Server", body)

    def _render_edit(self, query: str) -> None:
        params = parse_qs(query)
        requested_path = _first_form_value(params, "path") or "/"
        read_password = self._ui_read_password(params) or ""

        try:
            canonical = self.app.store.mapper.validate_url_path(requested_path)
            try:
                _, text = self.app.store.read_text(canonical, read_password=read_password or None)
            except FileNotFoundError:
                text = ""
            except AccessDeniedError as e:
                self._render_password_prompt(
                    path=canonical,
                    title="Read Password Required",
                    message=str(e),
                    read_password=read_password,
                )
                return
            self._render_edit_form(canonical=canonical, text=text, read_password=read_password)
        except AccessDeniedError as e:
            self._render_password_prompt(
                path=requested_path,
                title="Read Password Required",
                message=str(e),
                read_password=read_password,
            )
        except PathValidationError as e:
            self._send_html(400, "Invalid path", f"<h1>Invalid path</h1><p>{html.escape(str(e))}</p>")

    def _handle_edit_post(self) -> None:
        try:
            content_type = self.headers.get("Content-Type", "")
            if "application/x-www-form-urlencoded" not in content_type:
                self._error(400, "Expected form submission")
                return

            form = parse_qs(self._read_body().decode("utf-8"), keep_blank_values=True)
            path = _first_form_value(form, "path")
            text = _first_form_value(form, "text")
            read_password = self._ui_read_password(form) or ""
            write_password = self._ui_write_password(form)

            canonical = self.app.store.write_text(path, text, write_password=write_password)
            self.send_response(303)
            location_query = urlencode({
                "path": canonical,
                **({"read_password": read_password} if read_password else {}),
            })
            self.send_header("Location", f"/_/edit?{location_query}")
            self.end_headers()
        except AccessDeniedError as e:
            try:
                canonical = self.app.store.mapper.validate_url_path(path)
            except Exception:
                canonical = path
            self._render_edit_form(
                canonical=canonical,
                text=text,
                read_password=read_password,
                error_message=str(e),
            )
        except PathValidationError as e:
            self._error(400, str(e))
        except PermissionError as e:
            self._error(423, str(e))
        except FileNotFoundError as e:
            self._error(503, str(e))
        except Exception as e:
            self._log_exception("Request handling failure", e)
            self._error(400, str(e))

    def log_message(self, fmt: str, *args) -> None:
        logger.info(
            "access client=%s method=%s path=%s message=%s",
            self.client_address[0] if self.client_address else None,
            getattr(self, "command", None),
            getattr(self, "path", None),
            fmt % args,
        )


class AppServer:
    def __init__(self, mapper: PurePathMapper):
        self.mapper = mapper
        self.store = TextStore(mapper)


def load_storage_config(config_path: Path) -> tuple[list[Path], dict[str, Path]]:
    try:
        obj = json.loads(config_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise StorageConfigError(f"Config file not found: {config_path}")
    except json.JSONDecodeError as e:
        raise StorageConfigError(f"Invalid JSON config {config_path}: {e}") from e

    roots_raw = obj.get("roots")
    if not isinstance(roots_raw, list) or not roots_raw:
        raise StorageConfigError("Config must contain a non-empty 'roots' array")

    roots: list[Path] = []
    for item in roots_raw:
        if not isinstance(item, str) or not item.strip():
            raise StorageConfigError("Each item in 'roots' must be a non-empty string")
        roots.append(Path(item).expanduser())

    pinned_raw = obj.get("pinned_paths", {})
    if pinned_raw is None:
        pinned_raw = {}
    if not isinstance(pinned_raw, dict):
        raise StorageConfigError("'pinned_paths' must be an object mapping URL path prefixes to roots")

    pinned: dict[str, Path] = {}
    for prefix, root in pinned_raw.items():
        if not isinstance(prefix, str) or not isinstance(root, str):
            raise StorageConfigError("Pinned path entries must be string -> string")
        pinned[prefix] = Path(root).expanduser()

    return roots, pinned


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Lightweight elara versioning server")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Bind host")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Bind port")
    parser.add_argument("--base-dir", help="Single base directory for stored paths")
    parser.add_argument("--config", help="JSON config file for ordered roots and pinned paths")
    parser.add_argument("--log-file", default=DEFAULT_LOG_FILE, help="Log file path")
    args = parser.parse_args()

    if bool(args.base_dir) == bool(args.config):
        parser.error("Specify exactly one of --base-dir or --config")
    return args


def main() -> None:
    args = parse_args()
    log_path = configure_logging(Path(args.log_file))

    logger.info("Starting pastebin server | argv=%s", sys.argv)

    if args.config:
        roots, pinned = load_storage_config(Path(args.config).expanduser())
    else:
        single_root = Path(args.base_dir).expanduser()
        single_root.mkdir(parents=True, exist_ok=True)
        roots = [single_root]
        pinned = {}

    mapper = PurePathMapper(roots, pinned)
    app = AppServer(mapper)
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    httpd.app = app  # type: ignore[attr-defined]

    logger.info("Serving on http://%s:%s", args.host, args.port)
    logger.info("Log file: %s", log_path)
    if args.config:
        logger.info("Config file: %s", Path(args.config).expanduser().resolve())
    else:
        logger.info("Single root mode: %s", roots[0].resolve())
    for row in mapper.roots_status():
        logger.info("Root[%s]: %s available=%s", row["priority"], row["root"], row["available"])
    if mapper.pinned_paths:
        logger.info("Pinned paths follow")
        for prefix, root in sorted(mapper.pinned_paths.items()):
            logger.info("Pinned %s -> %s", prefix, root)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        logger.info("Shutdown requested by keyboard interrupt")
    except Exception as exc:
        logger.exception("Fatal server error during serve_forever: %s", exc)
        raise
    finally:
        logger.info("Server shutting down")
        httpd.server_close()


if __name__ == "__main__":
    main()
