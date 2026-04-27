#!/usr/bin/env python3
"""
Check that all .cpp/.h files (except SkullbonezCommon.h) start with no ASCII art header
(headers were removed in cleanup). SkullbonezCommon.h is exempt and has its own format.
"""
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
