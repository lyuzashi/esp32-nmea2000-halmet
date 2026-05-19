# Pre-build script to bundle ESM dependencies using esbuild
# Requires Node.js/npx - bundles are cached in lib/generated/ (gitignored)
import os
import gzip
import shutil
import subprocess
import inspect
import json
import math
from typing import Dict, Any, List


def scriptPath():
    return inspect.getfile(lambda: None)

def basePath():
    return os.path.dirname(os.path.dirname(os.path.dirname(inspect.getfile(lambda: None))))

def outPath():
    return os.path.join(basePath(), "lib", "generated")

def halmettaskPath():
    return os.path.join(basePath(), "lib", "halmettask")

CANBOAT_PART_COUNT = 8

# Dependencies to bundle - each becomes a separate embedded JS file
# Format: output_name -> { npm packages to install, entry code, esbuild options }
BUNDLES = {
    'halmet_preact': {
        'packages': ['preact@10', 'htm@3'],
        'entry': '''
import { h, render, Component, Fragment } from 'preact';
import { useState, useEffect, useRef, useMemo, useCallback } from 'preact/hooks';
import htm from 'htm';
const html = htm.bind(h);
export { h, render, Component, Fragment, html, useState, useEffect, useRef, useMemo, useCallback };
'''
    },
    'halmet_canboat': {
        'packages': [
            '@canboat/canboatjs@3',
            'events',
            'buffer',
            'stream-browserify',
            'util'
        ],  # browser polyfills for canboatjs/node deps
        'entry': '''
// Buffer polyfill for browser (canboatjs uses Node's Buffer)
import { Buffer } from 'buffer';
globalThis.Buffer = Buffer;

import { FromPgn, pgnToActisenseSerialFormat } from '@canboat/canboatjs/dist/browser.js';
export { FromPgn, pgnToActisenseSerialFormat };
''',
        # Explicit options to encourage maximum dead-code elimination.
        'esbuild': {
            'tree_shaking': True,
            'minify': True,
            'target': 'es2020',
            'format': 'esm',
            'platform': 'browser',
            'main_fields': 'browser,module,main',
            'conditions': 'browser,module',
            'aliases': {
                'stream': 'stream-browserify',
                'util': 'util',
            },
            # Split into byte parts after bundling to reduce single-transfer pressure.
            'byte_split_parts': CANBOAT_PART_COUNT,
            'analyze': True,
        }
    },
}


def _print_metafile_summary(name: str, metafile_path: str):
    """Print top retained modules from esbuild metafile to debug tree-shaking limits."""
    try:
        with open(metafile_path, 'r') as f:
            meta = json.load(f)
    except Exception as e:
        print(f"#WARNING {name}: unable to read metafile: {e}")
        return

    outputs: Dict[str, Any] = meta.get('outputs', {})
    if not outputs:
        print(f"#WARNING {name}: metafile has no outputs")
        return

    out_key = next(iter(outputs.keys()))
    out_info = outputs.get(out_key, {})
    inputs = out_info.get('inputs', {})
    if not inputs:
        print(f"#INFO {name}: no input contribution data in metafile")
        return

    ranked = sorted(inputs.items(), key=lambda kv: kv[1].get('bytesInOutput', 0), reverse=True)
    top = ranked[:8]
    print(f"#analyze {name}: top retained modules by bytesInOutput")
    for path, info in top:
        kept = info.get('bytesInOutput', 0)
        print(f"#  {kept:8d}  {path}")

def check_npx():
    """Check if npx is available"""
    try:
        result = subprocess.run(['npx', '--version'], capture_output=True, text=True, timeout=10)
        return result.returncode == 0
    except:
        return False


def _outputs_are_current(paths: List[str]) -> bool:
    """All output files exist and are newer than this build script."""
    if not paths:
        return False
    try:
        script_mtime = os.path.getmtime(scriptPath())
        for path in paths:
            if not os.path.exists(path):
                return False
            if os.path.getmtime(path) < script_mtime:
                return False
        return True
    except OSError:
        return False


def _part_gz_path(out_dir: str, name: str, index: int) -> str:
    return os.path.join(out_dir, f"{name}_part_{index:02d}.js.gz")


def _split_to_gz_parts(js_path: str, out_dir: str, name: str, part_count: int):
    with open(js_path, 'rb') as f:
        data = f.read()
    if part_count <= 0:
        raise ValueError("part_count must be > 0")

    chunk_size = int(math.ceil(len(data) / float(part_count))) if len(data) > 0 else 1
    total_gz = 0.0
    for i in range(part_count):
        start = i * chunk_size
        end = min(start + chunk_size, len(data))
        chunk = data[start:end]
        # Keep all parts present for deterministic embedding.
        if len(chunk) == 0:
            chunk = b"\n"
        gz_path = _part_gz_path(out_dir, name, i + 1)
        with gzip.open(gz_path, 'wb') as f_out:
            f_out.write(chunk)
        chunk_kb = len(chunk) / 1024.0
        gz_kb = os.path.getsize(gz_path) / 1024.0
        total_gz += gz_kb
        print(f"#bundled {name}_part_{i+1:02d}: {chunk_kb:.1f}KB -> {gz_kb:.1f}KB gzipped")
    return total_gz

def bundle_dep(name, config, out_dir, work_dir):
    """Bundle a dependency using esbuild"""
    es_cfg = config.get('esbuild', {})
    part_count = int(es_cfg.get('byte_split_parts', 0) or 0)

    if part_count > 0:
        expected = [_part_gz_path(out_dir, name, i + 1) for i in range(part_count)]
    else:
        expected = [os.path.join(out_dir, f"{name}.js.gz")]

    if _outputs_are_current(expected):
        print(f"#cached {name}")
        return True
    
    print(f"#bundling {name}...")
    
    # Create temp working directory
    bundle_dir = os.path.join(work_dir, name)
    os.makedirs(bundle_dir, exist_ok=True)
    
    try:
        # Create package.json
        pkg = {"name": name, "type": "module", "dependencies": {}}
        for pkg_spec in config['packages']:
            pkg_name = pkg_spec.rsplit('@', 1)[0]
            pkg_version = pkg_spec.rsplit('@', 1)[1] if '@' in pkg_spec else 'latest'
            # Handle scoped packages
            if pkg_spec.startswith('@'):
                parts = pkg_spec.split('@')
                pkg_name = '@' + parts[1]
                pkg_version = parts[2] if len(parts) > 2 else 'latest'
            pkg['dependencies'][pkg_name] = pkg_version
        
        pkg_json_path = os.path.join(bundle_dir, 'package.json')
        with open(pkg_json_path, 'w') as f:
            json.dump(pkg, f)
        
        # Write entry file
        entry_path = os.path.join(bundle_dir, 'entry.js')
        with open(entry_path, 'w') as f:
            f.write(config['entry'].strip())
        
        # Install packages
        print(f"#  npm install...")
        result = subprocess.run(
            ['npm', 'install', '--silent'],
            cwd=bundle_dir,
            capture_output=True,
            text=True,
            timeout=120
        )
        if result.returncode != 0:
            print(f"#ERROR npm install failed: {result.stderr}")
            return False
        
        # Bundle with esbuild (-y auto-accepts install prompt)
        print(f"#  esbuild bundle...")
        bundled_js_path = os.path.join(bundle_dir, f"{name}.js")
        metafile_path = os.path.join(bundle_dir, 'meta.json')
        esbuild_args = [
            'npx', '-y', 'esbuild', 'entry.js',
            '--bundle',
            f"--format={es_cfg.get('format', 'esm')}",
            f"--platform={es_cfg.get('platform', 'browser')}",
            f"--target={es_cfg.get('target', 'es2020')}",
            f"--main-fields={es_cfg.get('main_fields', 'module,main')}",
            f"--conditions={es_cfg.get('conditions', 'browser,module')}",
            f"--tree-shaking={'true' if es_cfg.get('tree_shaking', True) else 'false'}",
            f'--outfile={bundled_js_path}'
        ]
        if es_cfg.get('minify', True):
            esbuild_args.append('--minify')
        if es_cfg.get('analyze', False):
            esbuild_args.append(f'--metafile={metafile_path}')
        for alias_from, alias_to in es_cfg.get('aliases', {}).items():
            esbuild_args.append(f'--alias:{alias_from}={alias_to}')

        result = subprocess.run(
            esbuild_args,
            cwd=bundle_dir,
            capture_output=True,
            text=True,
            timeout=120
        )
        if result.returncode != 0:
            print(f"#ERROR esbuild failed: {result.stderr}")
            return False

        size_kb = os.path.getsize(bundled_js_path) / 1024.0
        if part_count > 0:
            total_gz_kb = _split_to_gz_parts(bundled_js_path, out_dir, name, part_count)
            print(f"#bundled {name} total: {size_kb:.1f}KB -> {total_gz_kb:.1f}KB gzipped across {part_count} parts")
            # Cleanup obsolete artifacts from previous chunking approaches.
            for stale in [
                os.path.join(out_dir, f"{name}.js.gz"),
                os.path.join(out_dir, f"{name}_chunk_browser.js.gz"),
                os.path.join(out_dir, f"{name}_chunk_chunk.js.gz"),
                os.path.join(out_dir, f"{name}_format.js.gz"),
                os.path.join(out_dir, f"{name}_decode.js.gz"),
            ]:
                if os.path.exists(stale):
                    os.remove(stale)
        else:
            gz_path = os.path.join(out_dir, f"{name}.js.gz")
            with open(bundled_js_path, 'rb') as f_in:
                with gzip.open(gz_path, 'wb') as f_out:
                    f_out.writelines(f_in)
            gz_size_kb = os.path.getsize(gz_path) / 1024.0
            print(f"#bundled {name}: {size_kb:.1f}KB -> {gz_size_kb:.1f}KB gzipped")

        if es_cfg.get('analyze', False) and os.path.exists(metafile_path):
            _print_metafile_summary(name, metafile_path)
        return True
        
    except subprocess.TimeoutExpired:
        print(f"#ERROR timeout bundling {name}")
        return False
    except Exception as e:
        print(f"#ERROR bundling {name}: {e}")
        return False
    finally:
        # Cleanup work directory
        shutil.rmtree(bundle_dir, ignore_errors=True)

def main():
    out_dir = outPath()
    os.makedirs(out_dir, exist_ok=True)
    
    # Check for npx
    if not check_npx():
        print("#WARNING: npx not found - Node.js required for bundling")
        print("#Dashboard dependencies will not be available offline")
        return
    
    # Create temp work directory
    work_dir = os.path.join(basePath(), '.pio', 'esm_bundle_tmp')
    os.makedirs(work_dir, exist_ok=True)
    
    success = True
    for name, config in BUNDLES.items():
        if not bundle_dep(name, config, out_dir, work_dir):
            success = False
    
    # Cleanup work directory
    shutil.rmtree(work_dir, ignore_errors=True)
    
    if not success:
        print("#WARNING: Some ESM bundles failed")
        print("#Dashboard may not work offline")

# Run on import (PlatformIO pre-script)
main()
