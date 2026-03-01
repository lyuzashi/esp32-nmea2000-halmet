#!/usr/bin/env python3
"""
Feature stripper for ESP32-NMEA2000 project
Uses PlatformIO build middleware to modify source in-memory without touching original files.
Stripped code is compiled from a temp file, original sources remain unchanged.
"""
Import("env")
import os
import re
import hashlib

# Get stripping options from platformio.ini
STRIP_MDNS = env.GetProjectOption("custom_strip_mdns", "false").lower() == "true"
STRIP_XDR = env.GetProjectOption("custom_strip_xdr", "false").lower() == "true"
STRIP_NMEA0183 = env.GetProjectOption("custom_strip_nmea0183", "false").lower() == "true"

print(f"[strip_features] MDNS={STRIP_MDNS}, XDR={STRIP_XDR}, NMEA0183={STRIP_NMEA0183}")

# Files to process - paths relative to project root
FILES_TO_STRIP = [
    "lib/gwwebserver/GwWebServer.cpp",
    "lib/socketserver/GwSocketServer.cpp",
    "src/main.cpp",
]

# Temp directory for modified sources
STRIP_TEMP_DIR = os.path.join(env.get("BUILD_DIR", ".pio/build"), "_stripped_src")


def should_process_file(filepath):
    """Check if this file should be processed for stripping"""
    if not (STRIP_MDNS or STRIP_XDR or STRIP_NMEA0183):
        return False
    
    project_dir = env.get("PROJECT_DIR", "")
    rel_path = os.path.relpath(filepath, project_dir)
    # Normalize path separators
    rel_path = rel_path.replace("\\", "/")
    
    return rel_path in FILES_TO_STRIP


def strip_mdns(content):
    """Remove MDNS includes and calls"""
    if not STRIP_MDNS:
        return content
    
    # Comment out MDNS includes
    content = re.sub(
        r'^(\s*#include\s*[<"]ESPmDNS\.h[>"])',
        r'// [STRIPPED] \1',
        content,
        flags=re.MULTILINE
    )
    
    # Remove MDNS.begin() calls (handles multi-line)
    content = re.sub(
        r'(\s*)(MDNS\.begin\s*\([^)]*\)\s*;)',
        r'\1/* [STRIPPED] \2 */',
        content
    )
    
    # Remove MDNS.addService() calls
    content = re.sub(
        r'(\s*)(MDNS\.addService\s*\([^)]*\)\s*;)',
        r'\1/* [STRIPPED] \2 */',
        content
    )
    
    # Remove mdns_query_a() calls (used in GwSocketServer)
    content = re.sub(
        r'(\s*)(mdns_query_a\s*\([^)]*\)\s*;)',
        r'\1/* [STRIPPED] \2 */',
        content
    )
    
    # Handle if blocks that check MDNS results
    # e.g., if (MDNS.begin(...)) { ... }
    content = re.sub(
        r'if\s*\(\s*MDNS\.begin\s*\([^)]*\)\s*\)',
        r'if (true /* [STRIPPED MDNS.begin] */)',
        content
    )
    
    return content


def strip_xdr(content):
    """Remove XDR transducer mapping code"""
    if not STRIP_XDR:
        return content
    
    # Comment out XDR includes
    content = re.sub(
        r'^(\s*#include\s*[<"].*[Xx]dr.*\.h[>"])',
        r'// [STRIPPED] \1',
        content,
        flags=re.MULTILINE
    )
    
    # Comment out xdr function calls (common patterns)
    content = re.sub(
        r'(\s*)(xdr[A-Z]\w*\s*\([^)]*\)\s*;)',
        r'\1/* [STRIPPED] \2 */',
        content
    )
    
    return content


def strip_nmea0183(content):
    """Remove NMEA0183 converter code"""
    if not STRIP_NMEA0183:
        return content
    
    # Comment out NMEA0183 converter includes
    content = re.sub(
        r'^(\s*#include\s*[<"].*[Nn]mea0183.*\.h[>"])',
        r'// [STRIPPED] \1',
        content,
        flags=re.MULTILINE
    )
    
    return content


def process_source(content, filepath):
    """Apply all enabled stripping operations"""
    original_len = len(content)
    
    content = strip_mdns(content)
    content = strip_xdr(content)
    content = strip_nmea0183(content)
    
    if len(content) != original_len:
        rel_path = os.path.relpath(filepath, env.get("PROJECT_DIR", ""))
        print(f"[strip_features] Processed {rel_path}: {original_len} -> {len(content)} bytes")
    
    return content


def strip_features_middleware(env, node):
    """
    Build middleware that intercepts source files and returns modified versions.
    Original files are never touched - modifications go to temp files.
    """
    # Handle NodeList (pioarduino compatibility)
    try:
        from SCons.Node import NodeList
        if isinstance(node, (list, NodeList)):
            node = node[0] if node else None
    except ImportError:
        pass
    
    if node is None:
        return node
    
    filepath = str(node.get_path())
    
    # Only process .cpp and .c files that are in our list
    if not filepath.endswith(('.cpp', '.c')):
        return node
    
    if not should_process_file(filepath):
        return node
    
    # Read original source
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            original_content = f.read()
    except Exception as e:
        print(f"[strip_features] Error reading {filepath}: {e}")
        return node
    
    # Apply stripping
    modified_content = process_source(original_content, filepath)
    
    # If no changes, return original node
    if modified_content == original_content:
        return node
    
    # Create temp file with modified content
    # Use hash of filepath to create unique temp filename
    os.makedirs(STRIP_TEMP_DIR, exist_ok=True)
    
    # Create deterministic temp filename based on original path
    path_hash = hashlib.md5(filepath.encode()).hexdigest()[:8]
    base_name = os.path.basename(filepath)
    temp_filename = f"{path_hash}_{base_name}"
    temp_filepath = os.path.join(STRIP_TEMP_DIR, temp_filename)
    
    # Write modified content to temp file
    with open(temp_filepath, 'w', encoding='utf-8') as f:
        f.write(modified_content)
    
    # Return a new node pointing to the temp file
    # This tells SCons to compile the temp file instead of the original
    return env.File(temp_filepath)


# Register the middleware
# This intercepts each source file before compilation
env.AddBuildMiddleware(strip_features_middleware)

print(f"[strip_features] Build middleware registered")
