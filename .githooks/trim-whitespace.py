#!/usr/bin/env python3
"""Trim trailing whitespace from files."""
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
