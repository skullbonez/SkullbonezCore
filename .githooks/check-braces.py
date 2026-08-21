#!/usr/bin/env python3
#
# File: .githooks/check-braces.py
# Purpose:
#   Rejects multi-line braceless if/for/while statements before commit.
#
# Summary:
#   This hook is a lightweight style guard. It scans staged source text for
#   risky control-flow shapes that are easy to misread in reviews.
#
# Glossary:
#   Braceless conditional: An if/for/while whose body is controlled only by
#   indentation instead of an explicit `{ ... }` block.
#   Hook: Local Git script that runs before a commit and can block style or
#   validation failures.
#
# Invariants:
#   - Single-line conditionals remain allowed for legacy source style.
#   - Multi-line conditionals must use braces so later edits cannot change
#   control flow accidentally.
#
# Related:
#   - AGENTS.md
#
import sys
import re

def check_file(filepath):
    """Return True if file passes, False if it fails."""
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    
    failed = False
    for i, line in enumerate(lines, 1):
        stripped = line.strip()
        
        # Skip comments, strings, empty lines
        if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
            continue
        
        # Check for single-line conditionals (these are OK)
        # e.g., "if ( x ) y = z;" or "for ( int i = 0; i < 10; ++i ) x++;"
        if re.search(r'(if|for|while)\s*\([^)]*\)\s*\w+.*;', stripped):
            continue
        
        # Hazard: a condition on one line with an indented next line can grow a
        # second statement later while only the first remains controlled.
        if re.match(r'(if|else\s+if|for|while)\s*\([^)]*\)\s*$', stripped):
            # Condition ends here, next line should start with { or else it's an error
            if i < len(lines):
                next_line = lines[i].strip()
                if next_line and not next_line.startswith('{') and not next_line.startswith('else'):
                    print(f"{filepath}:{i}: ERROR: Multi-line braceless conditional")
                    print(f"  {stripped}")
                    print(f"  {next_line}")
                    failed = True
    
    return not failed

if __name__ == '__main__':
    all_passed = True
    for filepath in sys.argv[1:]:
        if not filepath.endswith(('.cpp', '.h')):
            continue
        if not check_file(filepath):
            all_passed = False
    
    sys.exit(0 if all_passed else 1)
