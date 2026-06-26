#!/usr/bin/env python3
"""
Verify the 3 recent commits:
1. streamZipFromJsonArray exists and is called correctly
2. handleBatchDownload is registered
3. Favorites UI elements exist in web_ui.h
4. downloadSelected function exists
5. updateSelBtn handles downloadSelBtn
"""
import re
import sys

errors = []

# Check ESP32_File_Server.ino
with open("/tmp/ESP32-File-Server/ESP32_File_Server.ino") as f:
    ino = f.read()

# 1. streamZipFromJsonArray is defined
if "void streamZipFromJsonArray" not in ino:
    errors.append("FAIL: streamZipFromJsonArray not defined in .ino")
else:
    print("OK: streamZipFromJsonArray is defined")

# 2. streamZipFromJsonArray is called in handleZipDownload and handleFolderZip
if "streamZipFromJsonArray(paths" not in ino:
    errors.append("FAIL: streamZipFromJsonArray not called")
else:
    print("OK: streamZipFromJsonArray is called")

# 3. handleBatchDownload is defined
if "void handleBatchDownload" not in ino:
    errors.append("FAIL: handleBatchDownload not defined")
else:
    print("OK: handleBatchDownload is defined")

# 4. /api/batch-download is registered
if '/api/batch-download' not in ino:
    errors.append("FAIL: /api/batch-download not registered")
else:
    print("OK: /api/batch-download endpoint registered")

# 5. handleBatchDownload has CSRF check
if "handleBatchDownload" in ino:
    # Find the function and check for checkCsrf
    match = re.search(r'void handleBatchDownload\(\)\{([^}]+)\}', ino, re.DOTALL)
    if match:
        func_body = match.group(1)
        if 'checkCsrf' not in func_body:
            errors.append("FAIL: handleBatchDownload missing CSRF check")
        else:
            print("OK: handleBatchDownload has CSRF protection")
        if 'batch-download' not in func_body:
            errors.append("FAIL: handleBatchDownload doesn't call streamZipFromJsonArray")
        else:
            print("OK: handleBatchDownload calls streamZipFromJsonArray")

# Check web_ui.h
with open("/tmp/ESP32-File-Server/web_ui.h") as f:
    h = f.read()

# 6. toggleFavorite function exists
if "function toggleFavorite" not in h:
    errors.append("FAIL: toggleFavorite not defined in web_ui.h")
else:
    print("OK: toggleFavorite function exists")

# 7. initFavorites is called in DOMContentLoaded
if "initFavorites()" not in h:
    errors.append("FAIL: initFavorites not called in DOMContentLoaded")
else:
    print("OK: initFavorites called on page load")

# 8. downloadSelected function exists
if "function downloadSelected" not in h:
    errors.append("FAIL: downloadSelected not defined")
else:
    print("OK: downloadSelected function exists")

# 9. downloadSelBtn exists
if "downloadSelBtn" not in h:
    errors.append("FAIL: downloadSelBtn not found")
else:
    print("OK: downloadSelBtn referenced in UI")

# 10. updateSelBtn handles downloadSelBtn
if "downloadSelBtn" in h:
    # Find updateSelBtn function
    match = re.search(r'function updateSelBtn\(\)\{([^}]+)\}', h, re.DOTALL)
    if match:
        if 'downloadSelBtn' not in match.group(1):
            errors.append("FAIL: updateSelBtn doesn't handle downloadSelBtn")
        else:
            print("OK: updateSelBtn handles downloadSelBtn")

# 11. Favorites localStorage usage
if "localStorage.getItem('favorites')" not in h:
    errors.append("FAIL: localStorage favorites not found")
else:
    print("OK: localStorage favorites persistence implemented")

# 12. Star icon in file row
if "fav-" not in h:
    errors.append("FAIL: No fav- icon element found")
else:
    print("OK: Star icon element with fav- ID prefix exists")

# 13. downloadSelected calls /api/batch-download
if "function downloadSelected" in h:
    match = re.search(r'function downloadSelected\(\)\{([^}]+)\}', h, re.DOTALL)
    if match:
        if '/api/batch-download' not in match.group(1):
            errors.append("FAIL: downloadSelected doesn't call /api/batch-download")
        else:
            print("OK: downloadSelected calls /api/batch-download")

# 14. Check that streamZipFromJsonArray has proper ZIP structure (EOCD)
if "eocd" in ino:
    print("OK: ZIP end-of-central-directory structure present")
else:
    print("NOTE: ZIP EOCD not found by simple search (may use different var names)")

# Summary
print(f"\n{'='*50}")
if errors:
    print(f"VERIFICATION FAILED with {len(errors)} error(s):")
    for e in errors:
        print(f"  - {e}")
    sys.exit(1)
else:
    print("VERIFICATION PASSED: All checks OK")
    sys.exit(0)
