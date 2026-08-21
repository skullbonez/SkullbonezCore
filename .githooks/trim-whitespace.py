#!/usr/bin/env python3
"""
File: .githooks/trim-whitespace.py
Purpose:
  Remove trailing whitespace from staged C++ source and headers.

Summary:
  The hook rewrites each .cpp or .h line with normalized trailing whitespace and
  one newline. Per-file failures are reported without blocking the commit.

Invariants:
  - Unsupported suffixes are ignored.
  - Blank lines remain blank and non-whitespace text is preserved.
"""
import sys

for filepath in sys.argv[1:]:
    if not filepath.endswith(('.cpp', '.h')):
        continue
    
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        
        new_lines = [line.rstrip() + '\n' if line.strip() else '\n' for line in lines]
        
        with open(filepath, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
        
        print(f"Trimmed whitespace: {filepath}")
    except Exception as e:
        print(f"Error processing {filepath}: {e}", file=sys.stderr)

sys.exit(0)
