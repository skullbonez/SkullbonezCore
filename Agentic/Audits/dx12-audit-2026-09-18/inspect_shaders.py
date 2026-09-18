"""Read existing DXIL and record static evidence; does not rebuild the repository."""
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

started = time.perf_counter()
repo = Path('C:/SkullbonezCore')
out = Path(__file__).resolve().parent
dxc = Path('C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe')
manifest = json.loads((repo / 'SkullbonezData/shaders/shader_manifest.json').read_text())
rows = []
for entry in manifest['entries']:
    bytecode = repo / entry['bytecode']
    dump = subprocess.run([str(dxc), '-dumpbin', str(bytecode)], capture_output=True, text=True, check=True).stdout
    source = repo / entry['source']
    checks = {entry['source']: entry['source_sha256'], entry['bytecode']: entry['bytecode_sha256'], **entry.get('dependencies_sha256', {})}
    mismatches = [path for path, expected in checks.items() if hashlib.sha256((repo / path).read_bytes()).hexdigest() != expected]
    rows.append({
        'shader': source.name, 'stage': entry['stage'], 'bytes': bytecode.stat().st_size,
        'sample_call_sites': len(re.findall(r'\bcall\b[^\n]*@dx\.op\.sample\w*\.', dump)),
        'texture_load_call_sites': len(re.findall(r'\bcall\b[^\n]*@dx\.op\.textureLoad\.', dump)),
        'ir_branch_sites': len(re.findall(r'^\s*br ', dump, re.MULTILINE)),
        'hash_mismatches': ';'.join(mismatches),
    })
    (out / 'dxil').mkdir(exist_ok=True)
    (out / 'dxil' / (bytecode.name + '.txt')).write_text(dump, encoding='utf-8')
with (out / 'shader-static-inventory.csv').open('w', newline='', encoding='utf-8') as handle:
    writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
summary = {'stages': len(rows), 'programs': len({row['shader'] for row in rows}),
           'hash_mismatch_stages': sum(bool(row['hash_mismatches']) for row in rows),
           'elapsed_seconds': round(time.perf_counter() - started, 3),
           'note': 'Static DXIL call sites, not executed texture operations, ISA instruction counts, register counts, or GPU timings.'}
(out / 'shader-inspection-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary))
for row in rows:
    if row['stage'] == 'ps' and row['shader'] in {'post_tonemap.hlsl', 'post_volumetric_light.hlsl', 'lit_textured.hlsl', 'lit_textured_instanced.hlsl', 'text.hlsl', 'UIBackdropBlur.hlsl'}:
        print(json.dumps(row))
