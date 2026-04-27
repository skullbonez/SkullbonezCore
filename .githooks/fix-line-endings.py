#!/usr/bin/env python3
"""Fix CRLF to LF line endings."""
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
