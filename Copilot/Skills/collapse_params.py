import re, glob, os

SOURCE_DIR = r"G:\SkullbonezCoreOriginal\SkullbonezSource"

def collapse_multiline_params(content):
    """
    Collapse multi-line function parameter lists to a single line.
    When a line has more open parens than close parens,
    join continuation lines until parens balance.
    """
    lines = content.split('\n')
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        # Count parens in code (strip // comments first)
        code_part = re.sub(r'//.*$', '', line)
        depth = code_part.count('(') - code_part.count(')')
        if depth > 0:
            # Strip trailing comment from the opening line too —
            # if it has one, it would comment-out everything after the join.
            combined = re.sub(r'\s*//.*$', '', line).rstrip()
            while depth > 0 and i + 1 < len(lines):
                i += 1
                next_line = lines[i]
                next_code = re.sub(r'//.*$', '', next_line)
                next_depth = next_code.count('(') - next_code.count(')')
                stripped = next_line.strip()
                if next_depth + depth <= 0:
                    # Closing line — keep with its trailing comment (e.g. // description)
                    combined = combined + ' ' + stripped
                else:
                    # Intermediate line — strip trailing comment before merging
                    stripped_no_comment = re.sub(r'\s*//.*$', '', stripped)
                    combined = combined + ' ' + stripped_no_comment
                depth += next_depth
            result.append(combined)
        else:
            result.append(line)
        i += 1
    return '\n'.join(result)

all_files = (glob.glob(os.path.join(SOURCE_DIR, '*.h')))

count = 0
for filepath in all_files:
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        original = f.read()
    updated = collapse_multiline_params(original)
    if updated != original:
        with open(filepath, 'w', encoding='utf-8', newline='') as f:
            f.write(updated)
        count += 1
        print(f"  collapsed: {os.path.basename(filepath)}")

print(f"\nDone - {count} files updated")
