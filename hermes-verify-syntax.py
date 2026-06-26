#!/usr/bin/env python3
"""Verify basic syntax: balanced braces in key functions, no duplicate definitions"""
import re
import sys

errors = []

with open("/tmp/ESP32-File-Server/ESP32_File_Server.ino") as f:
    ino = f.read()

# Check for duplicate function definitions
funcs_called = ["streamZipFromJsonArray", "handleBatchDownload", "handleZipDownload", "handleFolderZip"]
for func in funcs_called:
    # Count definitions (void funcName()
    pattern = r'void\s+' + func + r'\s*\('
    matches = re.findall(pattern, ino)
    if len(matches) != 1:
        errors.append(f"FAIL: {func} defined {len(matches)} times (expected 1)")
    else:
        print(f"OK: {func} defined exactly once")

# Verify streamZipFromJsonArray has balanced braces
match = re.search(r'void streamZipFromJsonArray\([^)]*\)\s*\{', ino)
if match:
    start = match.end()
    depth = 1
    i = start
    while i < len(ino) and depth > 0:
        if ino[i] == '{':
            depth += 1
        elif ino[i] == '}':
            depth -= 1
        i += 1
    if depth != 0:
        errors.append(f"FAIL: streamZipFromJsonArray has unbalanced braces (depth={depth})")
    else:
        func_len = i - start
        print(f"OK: streamZipFromJsonArray braces balanced ({func_len} chars)")

# Verify handleBatchDownload has balanced braces
match = re.search(r'void handleBatchDownload\(\)\s*\{', ino)
if match:
    start = match.end()
    depth = 1
    i = start
    while i < len(ino) and depth > 0:
        if ino[i] == '{':
            depth += 1
        elif ino[i] == '}':
            depth -= 1
        i += 1
    if depth != 0:
        errors.append(f"FAIL: handleBatchDownload has unbalanced braces (depth={depth})")
    else:
        func_len = i - start
        print(f"OK: handleBatchDownload braces balanced ({func_len} chars)")

# Check web_ui.h for balanced braces in key JS functions
with open("/tmp/ESP32-File-Server/web_ui.h") as f:
    h = f.read()

js_funcs = ["toggleFavorite", "downloadSelected", "initFavorites", "updateSelBtn"]
for func in js_funcs:
    match = re.search(r'function\s+' + func + r'\s*\([^)]*\)\s*\{', h)
    if match:
        start = match.end()
        depth = 1
        i = start
        while i < len(h) and depth > 0:
            if h[i] == '{':
                depth += 1
            elif h[i] == '}':
                depth -= 1
            i += 1
        if depth != 0:
            errors.append(f"FAIL: JS {func} has unbalanced braces (depth={depth})")
        else:
            func_len = i - start
            print(f"OK: JS {func} braces balanced ({func_len} chars)")
    else:
        errors.append(f"FAIL: JS function {func} not found")

# Check that handleFolderZip still works (uses streamZipFromJsonArray)
match = re.search(r'void handleFolderZip\(\)\s*\{([^}]+)\}', h, re.DOTALL)
# Actually it's in the .ino file
match = re.search(r'void handleFolderZip\(\)\s*\{([^}]+)\}', ino, re.DOTALL)
if match:
    if 'streamZipFromJsonArray' in match.group(1):
        print("OK: handleFolderZip still uses streamZipFromJsonArray")
    else:
        errors.append("FAIL: handleFolderZip doesn't use streamZipFromJsonArray")

print(f"\n{'='*50}")
if errors:
    print(f"SYNTAX CHECK FAILED with {len(errors)} error(s):")
    for e in errors:
        print(f"  - {e}")
    sys.exit(1)
else:
    print("SYNTAX CHECK PASSED: All functions well-formed")
    sys.exit(0)
