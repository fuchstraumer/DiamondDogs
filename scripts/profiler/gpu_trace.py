"""Read the text export of an Nsight Graphics GPU Trace.

The export folder holds a BASE subfolder of tab-separated .xls files. This module reads them and
returns plain Python values. It does not write to the database. `db_store.ingest_gpu_trace` stores them.
"""

from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

REPRO_INFO_FILE = "REPRO_INFO.xls"
FRAME_FILE = "FRAME.xls"
EVENTS_FILE = "D3DPERF_EVENTS.xls"
REGIMES_FILE = "GPUTRACE_REGIMES.xls"
FRAME_METRICS_FILE = "GPUTRACE_FRAME.xls"

FRAME_TIME_KEY = "GPU frame time"
EVENTS_HEADER = "event_text"
REGIMES_HEADER = "flattened_event_name"

# Nsight Graphics indents each nested debug label by this many spaces.
INDENT_WIDTH = 8


def read_table_rows(path: Path) -> List[List[str]]:
    """Read a tab-separated file. Keep the leading spaces of each cell.

    Return the non-empty lines as lists of cells.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    return [line.split("\t") for line in lines if line]


def read_key_values(path: Path) -> Dict[str, str]:
    """Read a file of key, TAB, value lines. Strip the keys and the values.

    Return a dictionary. A line without a tab is skipped.
    """
    values: Dict[str, str] = {}
    for cells in read_table_rows(path):
        if len(cells) >= 2:
            values[cells[0].strip()] = cells[1].strip()
    return values


def parse_number(text: str) -> Optional[float]:
    """Convert a cell to a float. Return None when the cell is not a number."""
    try:
        return float(text.strip())
    except ValueError:
        return None


def normalize_metric_name(raw_name: str) -> str:
    """Drop the leading unit instance and metric set tokens of a raw metric name.

    The normalized name starts at the first dotted token that contains a double underscore.
    A name without a double underscore is returned unchanged.
    """
    tokens = raw_name.split(".")
    for index, token in enumerate(tokens):
        if "__" in token:
            return ".".join(tokens[index:])
    return raw_name


def make_metric(raw_name: str, value: float) -> Dict[str, Any]:
    """Build a metric record with the raw name, the normalized name and the value.

    Return the record dictionary.
    """
    return {
        "raw_name": raw_name,
        "metric_name": normalize_metric_name(raw_name),
        "value": value,
    }


def parse_scope_events(rows: List[List[str]]) -> Tuple[List[Dict[str, Any]], int]:
    """Build the scope paths from the event rows of D3DPERF_EVENTS.

    Each nesting level adds INDENT_WIDTH leading spaces to the event text. The path joins the
    names of the enclosing scopes with "/". Return the scopes in file order and the skipped row count.
    """
    if not rows or rows[0][0].strip() != EVENTS_HEADER:
        raise ValueError(f"Event file has no {EVENTS_HEADER} header.")

    scopes: List[Dict[str, Any]] = []
    stack: List[str] = []
    skipped = 0
    for cells in rows[1:]:
        if len(cells) < 2:
            skipped += 1
            continue

        text = cells[0]
        name = text.strip()
        depth = (len(text) - len(text.lstrip(" "))) // INDENT_WIDTH
        time_ms = parse_number(cells[1])
        if not name or time_ms is None or depth > len(stack):
            skipped += 1
            continue

        stack = stack[:depth] + [name]
        scopes.append({"path": "/".join(stack), "depth": depth, "time_ms": time_ms})

    return scopes, skipped


def parse_regimes(rows: List[List[str]]) -> Tuple[Dict[str, List[Dict[str, Any]]], int]:
    """Read the per-scope metric table of GPUTRACE_REGIMES.

    The first column holds the scope path. Each other column is one metric. Return a dictionary
    from scope path to its metric records, and the count of cells that are not numbers.
    """
    if not rows or rows[0][0].strip() != REGIMES_HEADER:
        raise ValueError(f"Regimes file has no {REGIMES_HEADER} header.")

    raw_names = rows[0][1:]
    skipped = 0
    by_path: Dict[str, List[Dict[str, Any]]] = {}
    for cells in rows[1:]:
        path = cells[0]
        metrics: List[Dict[str, Any]] = []
        for index, raw_name in enumerate(raw_names):
            cell = cells[index + 1] if index + 1 < len(cells) else ""
            value = parse_number(cell)
            if value is None:
                skipped += 1
                continue

            metrics.append(make_metric(raw_name, value))

        by_path[path] = metrics

    return by_path, skipped


def parse_frame_metrics(rows: List[List[str]]) -> Tuple[List[Dict[str, Any]], int]:
    """Read the whole-frame metrics of GPUTRACE_FRAME, one metric per line.

    Return the metric records and the count of cells that are not numbers.
    """
    metrics: List[Dict[str, Any]] = []
    skipped = 0
    for cells in rows:
        value = parse_number(cells[1]) if len(cells) >= 2 else None
        if value is None:
            skipped += 1
            continue

        metrics.append(make_metric(cells[0].strip(), value))

    return metrics, skipped


def parse_gpu_trace_export(base_dir: Path) -> Dict[str, Any]:
    """Read the BASE folder of a GPU Trace export.

    Return a dictionary with these keys:
    - repro_info: the key and value pairs of REPRO_INFO.
    - frame_time_ms: the GPU frame time, or None.
    - scopes: the scope records in file order, each with path, depth and time_ms.
    - scope_metrics: the metric records of each scope path, or an empty list.
    - unmatched_scopes: the scope paths that have no row in GPUTRACE_REGIMES.
    - frame_metrics: the whole-frame metric records.
    - skipped_cells: the count of rows and cells that did not parse.
    """
    repro_info = read_key_values(base_dir / REPRO_INFO_FILE)
    frame_time_ms = parse_number(read_key_values(base_dir / FRAME_FILE).get(FRAME_TIME_KEY, ""))

    scopes, skipped_events = parse_scope_events(read_table_rows(base_dir / EVENTS_FILE))
    regime_metrics, skipped_regimes = parse_regimes(read_table_rows(base_dir / REGIMES_FILE))
    frame_metrics, skipped_frame = parse_frame_metrics(read_table_rows(base_dir / FRAME_METRICS_FILE))

    scope_metrics = {scope["path"]: regime_metrics.get(scope["path"], []) for scope in scopes}
    unmatched_scopes = [scope["path"] for scope in scopes if scope["path"] not in regime_metrics]

    return {
        "repro_info": repro_info,
        "frame_time_ms": frame_time_ms,
        "scopes": scopes,
        "scope_metrics": scope_metrics,
        "unmatched_scopes": unmatched_scopes,
        "frame_metrics": frame_metrics,
        "skipped_cells": skipped_events + skipped_regimes + skipped_frame,
    }
