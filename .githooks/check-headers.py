#!/usr/bin/env python3
#
# File: .githooks/check-headers.py
# Purpose:
#   Blocks restored legacy ASCII-art headers in C++ source files.
#
# Summary:
#   This hook preserves the post-cleanup file shape. It is not a learning-header
#   validator; it only prevents large decorative banners from coming back.
#
# Glossary:
#   ASCII-art header: Decorative block comment banner that used to dominate the
#   top of source files without teaching file purpose or invariants.
#   Hook: Local Git script that runs before a commit and can block style or
#   validation failures.
#
# Invariants:
#   - SkullbonezCommon.h is exempt because it keeps a different historical
#   layout.
#   - Unexpected starts warn instead of failing; the hard failure is only the
#   retired banner format.
#
# Related:
#   - AGENTS.md
#
import sys
import os

EXEMPT = {"SkullbonezCommon.h"}

def check_file(filepath):
    """Return True if file passes, False if it fails."""
    basename = os.path.basename(filepath)
    
    # SkullbonezCommon.h is exempt
    if basename in EXEMPT:
        return True
    
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        first_line = f.readline().strip()
    
    # Files should start with /* -- (standard section marker) or #ifndef/#include
    # NOT with a lengthy ASCII art comment block
    if first_line.startswith('/*') and 'THE SKULLBONEZ CORE' in first_line:
        print(f"{filepath}: ERROR: Still has old ASCII art header. Run header removal script.")
        return False
    
    # Should start with either:
    # - /* -- INCLUDE GUARDS ... (standard pattern)
    # - #ifndef (header guard)
    # - // (comment)
    # - #include (include statement)
    if not (first_line.startswith('/*') or first_line.startswith('#ifndef') or 
            first_line.startswith('#include') or first_line.startswith('//') or
            first_line == ''):
        print(f"{filepath}: WARNING: Unexpected file start: {first_line[:50]}")
        return True  # Not an error, just warning
    
    return True

if __name__ == '__main__':
    failed = False
    for filepath in sys.argv[1:]:
        if not filepath.endswith(('.cpp', '.h')):
            continue
        if not check_file(filepath):
            failed = True
    
    sys.exit(1 if failed else 0)
