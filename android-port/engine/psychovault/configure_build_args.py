#!/usr/bin/env python3
"""Generate the hosted arm64 GN args from the pinned upstream args.

Keep this separate from build-engine.sh so the exact compiler/cache settings can
be tested without fetching or compiling Chromium.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path

REPLACEMENTS = {
    'target_cpu = "arm"': 'target_cpu = "arm64"',
    'symbol_level = 1': 'symbol_level = 0',
    'use_siso = true': 'use_siso = false',
    'generate_linker_map = true': 'generate_linker_map = false',
}

EXTRA_ARGS = """
# Keep the hosted build inside the runner's disk/RAM budget.
chrome_pgo_phase = 0
use_thin_lto = false
blink_symbol_level = 0
v8_symbol_level = 0
enable_resource_allowlist_generation = false
android_static_analysis = "off"
"""

ALLOWED_WRAPPERS = {"", "sccache", "ccache"}


def build_args(upstream: str, wrapper: str = "") -> str:
    if wrapper not in ALLOWED_WRAPPERS:
        raise ValueError(f"Unsupported compiler wrapper: {wrapper}")
    args = upstream
    for old, new in REPLACEMENTS.items():
        if args.count(old) != 1:
            raise ValueError(f"Expected one GN argument: {old}")
        args = args.replace(old, new)
    args += EXTRA_ARGS
    if wrapper:
        args += f'\n# Reuse compiler outputs across hosted runs when the cache has them.\ncc_wrapper = "{wrapper}"\n'
    return args


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--wrapper", default=os.environ.get("PSYCHO_CC_WRAPPER", ""))
    options = parser.parse_args()
    try:
        args = build_args(options.upstream.read_text(encoding="utf-8"), options.wrapper)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(args, encoding="utf-8")


if __name__ == "__main__":
    main()
