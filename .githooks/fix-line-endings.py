#!/usr/bin/env python3
"""
File: .githooks/fix-line-endings.py
Purpose:
  Normalize staged C++ source and headers from CRLF to LF in the pre-commit hook.

Summary:
  The hook receives candidate paths, filters to .cpp and .h files, and performs
  a byte-preserving newline substitution. Per-file failures are reported but do
  not block the commit, so this remains an advisory cleanup step.

Invariants:
  - Bytes other than CRLF newline pairs are unchanged.
  - Unsupported suffixes are ignored.
"""
import sys

for filepath in sys.argv[1:]:
    if not filepath.endswith(('.cpp', '.h')):
        continue
    
    try:
        with open(filepath, 'rb') as f:
            content = f.read()
        
        # Convert CRLF to LF
        new_content = content.replace(b'\r\n', b'\n')
        
        if content != new_content:
            with open(filepath, 'wb') as f:
                f.write(new_content)
            print(f"Fixed line endings: {filepath}")
    except Exception as e:
        print(f"Error processing {filepath}: {e}", file=sys.stderr)

sys.exit(0)
