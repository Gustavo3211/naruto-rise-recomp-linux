"""Extracts every structurally valid shader container of game/xeshader.sdb.

Usage: python -I extract-sdb-containers.py <sdb> <new output dir>

The SDB stores the containers without alignment, so the scan is bytewise (the
directory scanner of XenosRecomp steps 4 bytes at a time). Each container goes to
a file of its own, starting at offset 0, and duplicates (by SHA-256) are written
once.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

source, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=False)
(out / 'containers').mkdir()
data = source.read_bytes()
entries, seen, cursor, hits = [], {}, 0, 0
while (start := data.find(b'\x10\x2a\x11', cursor)) >= 0:
    cursor = start + 1
    if start + 36 > len(data):
        continue
    flags, virtual, physical, fieldc, constant, definition, shader, last1, last2 = struct.unpack_from('>9I', data, start)
    if flags & 0xFFFFFF00 != 0x102A1100:
        continue
    size = virtual + physical
    if not (last1 == last2 == 0 and virtual >= 36 and physical > 0 and size <= len(data) - start and 0 < shader < virtual - 24):
        continue
    code_offset, code_size = struct.unpack_from('>2I', data, start + shader)
    if code_offset + code_size > physical or not code_size:
        continue
    hits += 1
    blob = data[start:start + size]
    digest = hashlib.sha256(blob).hexdigest().upper()
    stage = 'vs' if flags & 1 else 'ps'
    if digest in seen:
        seen[digest]['offsets'].append(hex(start))
        continue
    name = f'{stage}-{start:08X}.bin'
    (out / 'containers' / name).write_bytes(blob)
    entry = dict(stage=stage, file=name, offsets=[hex(start)], bytes=size, virtual_bytes=virtual,
                 physical_bytes=physical, code_bytes=code_size, sha256=digest)
    seen[digest] = entry
    entries.append(entry)
report = dict(source=str(source), source_sha256=hashlib.sha256(data).hexdigest().upper(),
              layout_source='hedge-dev/XenosRecomp@990d03b28a27b50277ee5d8d942e1c5f873869d1: shader.h',
              scan_alignment='bytewise', structurally_valid_headers=hits, unique_containers=len(entries),
              vertex=sum(e['stage'] == 'vs' for e in entries), pixel=sum(e['stage'] == 'ps' for e in entries),
              extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), containers=entries)
(out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
print(json.dumps({k: report[k] for k in ['structurally_valid_headers', 'unique_containers', 'vertex', 'pixel']}))
