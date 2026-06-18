#!/usr/bin/env python3
#
# File: .githooks/run-clang-format.py
# Purpose:
#   Finds clang-format and applies the repository C++ style to requested files.
#
# Mental model:
#   The hook is a thin launcher. It resolves the formatter from PATH or common
#   Visual Studio LLVM locations, then delegates formatting to clang-format.
#
# Glossary:
#   clang-format: LLVM formatter that rewrites C++ whitespace according to the
#   checked-in style rules.
#   LLVM: Compiler toolchain bundled with Visual Studio and commonly installed
#   separately for C++ formatting tools.
#   Hook: Local Git script that runs before a commit and can block style or
#   validation failures.
#
# Invariants:
#   - The script formats only paths supplied by the caller.
#   - Missing clang-format is a hard error so commits do not silently skip
#   style normalization.
#
# Related:
#   - .clang-format
#   - AGENTS.md
#
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
