# Solver observation binary format

`tools/convert_solver_diagnostics.py <bundle> [<bundle> ...]` converts existing
complete comparisons. `tools/package_solver_lab.py` also emits this format when
packaging captures. Neither operation reruns physics or replaces the source
archives or `.skreplay` motion/checkpoint files.

The derived `.skobs` file contains the numeric fields consumed by Solver Lab's
contact and solver-iteration comparison. Additional diagnostic log fields remain
in the original JSON archives. Conversion hashes every original source byte and
requires a match with the capture's `diagnosticsSha256` before publishing output.

Each side's manifest adds a `diagnosticsBinary` object with `path`, `sha256`, and
`sourceSha256`. Runtime verifies the binary hash and its original source identity.
A declared binary that fails verification is an error. Older manifests without
this object continue to use the existing JSON loader.

## SKOBS1 layout

All integers are explicitly little endian. Float fields are IEEE binary32,
rounded from JSON through binary64 to match the current reader's stored values.
No C++ object layout, padding, pointer, or native `bool` is written to disk.

| Section | Layout |
|---|---|
| Header, 44 bytes | Eight magic bytes `SKOBS1\r\n`, original SHA-256 as 32 raw bytes, uint32 directory count |
| Directory, 16 bytes per entry | int32 scene frame, uint32 flags, uint32 contact count, uint32 iteration count |
| Frame payload | All contact records followed by all iteration records, in directory order |
| Contact, 52 bytes | int32 body A row, int32 body B row, uint32 feature, uint32 warm-started (0 or 1), nine float32 values |
| Iteration, 20 bytes | int32 iteration, int32 dropped count, float32 stopping/normal/tangent impulse delta squared |

Contact float order is normal x/y/z, penetration, normal impulse, tangent
impulse, pre-solve normal speed, pre-solve slip speed, post-solve slip speed.
Directory flag bit 0 means a frame row was recorded; bit 1 means solver stats
were recorded. A missing frame and an observed frame with zero contacts remain
distinct. Only these two flag bits are defined. An empty directory is valid for
a source containing no consumed rows.

Directory scene frames strictly increase. Contact and iteration ordering within
each frame matches the source stream. Payload offsets are prefix sums of the
directory counts, so no text scan is needed to allocate or locate a frame.
Scene-frame-to-tick offset and body-row-to-stable-identity resolution remain
runtime operations using the original motion recording. Unresolved body rows
remain zero, preserving the comparison's existing ambiguous-evidence behavior.

Runtime checks magic/version, directory bounds/order/flags, exact file length,
tick coverage, numeric validity, cancellation and the existing memory budget.
It allocates exact per-frame output sizes and reuses one bounded frame buffer.
The preflight charge includes binary directory and frame decode scratch.

## Conversion on 2026-09-07

| Comparison | Side | Original JSON bytes | Binary bytes |
|---|---|---:|---:|
| Ragdoll & Wall | A | 1,064,769,467 | 33,933,924 |
| Ragdoll & Wall | B | 999,704,771 | 26,634,932 |
| Wall Only | A | 944,334,922 | 25,371,512 |
| Wall Only | B | 938,469,664 | 24,783,656 |

All four conversions completed with source hash admission, each with 2,400
directory entries. Original archives were retained. At the user's direction,
no build, tests, runtime timing or round-trip equivalence test was run; runtime
correctness and speed remain unverified.
