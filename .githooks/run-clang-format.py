#!/usr/bin/env python3
"""
Wrapper around clang-format to format C++ files in-place.
Finds clang-format in common locations (VS2022 LLVM, PATH).
"""
import sys
import subprocess
import os
from pathlib import Path

def find_clang_format():
    """Find clang-format binary."""
    # Try PATH first
    result = subprocess.run(['where', 'clang-format'], capture_output=True, text=True)
    if result.returncode == 0:
        return result.stdout.strip().split('\n')[0]
    
    # Try VS2022 LLVM paths
    candidates = [
        r'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe',
        r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe',
    ]
    for path in candidates:
        if os.path.exists(path):
            return path
    
    print("ERROR: clang-format not found. Install LLVM or add to PATH.", file=sys.stderr)
    return None

if __name__ == '__main__':
    cf = find_clang_format()
    if not cf:
        sys.exit(1)
    
    # Run clang-format -i (in-place) on all provided files
    cmd = [cf, '-i', '--style=file'] + sys.argv[1:]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.stdout:
        print(result.stdout, file=sys.stderr)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    
    sys.exit(result.returncode)
