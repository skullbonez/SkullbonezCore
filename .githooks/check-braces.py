#!/usr/bin/env python3
"""
Check for multi-line braceless if/for/while statements.
Single-line forms like "if ( x ) doThing();" are OK.
Multi-line without braces like:
  if ( x )
      doThing();
Are NOT OK — must use braces.
"""
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
        
        # Check if this line is a bare if/for/while with condition but no braces or single-line body
        # Pattern: keyword ( condition ) with next line indented (multi-line without braces)
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
