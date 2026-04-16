# Pre-build script to bundle ESM dependencies using esbuild
# Requires Node.js/npx - bundles are cached in lib/generated/ (gitignored)
import os
import gzip
import shutil
import subprocess
import inspect
import json

def basePath():
    return os.path.dirname(os.path.dirname(os.path.dirname(inspect.getfile(lambda: None))))

def outPath():
    return os.path.join(basePath(), "lib", "generated")

def halmettaskPath():
    return os.path.join(basePath(), "lib", "halmettask")

# Dependencies to bundle - each becomes a separate embedded JS file
# Format: output_name -> { npm packages to install, entry code }
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
        'packages': ['@canboat/canboatjs@3', 'events', 'buffer'],  # events + buffer = browser polyfills
        'entry': '''
// Buffer polyfill for browser (canboatjs uses Node's Buffer)
import { Buffer } from 'buffer';
globalThis.Buffer = Buffer;

import { FromPgn, pgnToActisenseSerialFormat } from '@canboat/canboatjs';
export { FromPgn, pgnToActisenseSerialFormat };
'''
    },
}

def check_npx():
    """Check if npx is available"""
    try:
        result = subprocess.run(['npx', '--version'], capture_output=True, text=True, timeout=10)
        return result.returncode == 0
    except:
        return False

def bundle_dep(name, config, out_dir, work_dir):
    """Bundle a dependency using esbuild"""
    js_path = os.path.join(out_dir, f"{name}.js")
    gz_path = os.path.join(out_dir, f"{name}.js.gz")
    
    # Skip if already bundled
    if os.path.exists(js_path):
        # Ensure gz exists
        if not os.path.exists(gz_path) or os.path.getmtime(js_path) > os.path.getmtime(gz_path):
            with open(js_path, 'rb') as f_in:
                with gzip.open(gz_path, 'wb') as f_out:
                    f_out.writelines(f_in)
            print(f"#gzipped {name}.js")
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
        result = subprocess.run(
            ['npx', '-y', 'esbuild', 'entry.js',
             '--bundle',
             '--format=esm',
             '--platform=browser',
             '--minify',
             f'--outfile={js_path}'],
            cwd=bundle_dir,
            capture_output=True,
            text=True,
            timeout=120
        )
        if result.returncode != 0:
            print(f"#ERROR esbuild failed: {result.stderr}")
            return False
        
        # Gzip for embedding
        with open(js_path, 'rb') as f_in:
            with gzip.open(gz_path, 'wb') as f_out:
                f_out.writelines(f_in)
        
        size_kb = os.path.getsize(js_path) / 1024
        gz_size_kb = os.path.getsize(gz_path) / 1024
        print(f"#bundled {name}: {size_kb:.1f}KB -> {gz_size_kb:.1f}KB gzipped")
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
