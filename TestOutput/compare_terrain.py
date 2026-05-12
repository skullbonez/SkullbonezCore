#!/usr/bin/env python3
r"""
compare_terrain.py

Runs the engine in DX11 and DX12 modes, captures screenshots of the terrain
(no balls, no HUD), then performs a pixel-level comparison to analyse texture
filtering differences.

Usage:
    cd G:\skore0\TestOutput
    python compare_terrain.py

Output:
    terrain_dx11.bmp   — DX11 capture
    terrain_dx12.bmp   — DX12 capture
    terrain_diff.bmp   — absolute diff, amplified x8 (bright = big difference)
    terrain_diff_hot.bmp — false-colour heatmap (blue=small, red=large diff)
"""

import os
import sys
import shutil
import subprocess
import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------
SKORE0 = r"G:\skore0"
EXE    = os.path.join(SKORE0, "Debug", "SKULLBONEZ_CORE.exe")
SCENE  = r"SkullbonezData\scenes\terrain_compare.scene"
OUT    = os.path.join(SKORE0, "TestOutput")

CAP_PATH  = os.path.join(OUT, "terrain_cap.bmp")
DX11_PATH = os.path.join(OUT, "terrain_dx11.bmp")
DX12_PATH = os.path.join(OUT, "terrain_dx12.bmp")
DIFF_PATH = os.path.join(OUT, "terrain_diff.bmp")
HOT_PATH  = os.path.join(OUT, "terrain_diff_hot.bmp")
# ---------------------------------------------------------------------------


def run_engine(renderer: str) -> bool:
    """Launch the engine, wait for it to exit. Returns True on success."""
    print(f"\n[RUN] {renderer.upper()} — please wait (window will appear briefly)...")
    # Remove stale capture from a previous run
    if os.path.exists(CAP_PATH):
        os.remove(CAP_PATH)

    result = subprocess.run(
        [EXE, "--renderer", renderer, "--scene", SCENE],
        cwd=SKORE0,
        timeout=90,
    )
    if result.returncode != 0:
        print(f"  WARNING: engine exited with code {result.returncode}")

    if os.path.exists(CAP_PATH):
        return True
    print(f"  ERROR: screenshot was not created at {CAP_PATH}")
    return False


def heatmap(arr: np.ndarray) -> np.ndarray:
    """Convert a 2D float array [0..1] to an RGB heatmap (blue→green→red)."""
    h, w = arr.shape
    out = np.zeros((h, w, 3), dtype=np.uint8)
    # blue → cyan  (0.0 – 0.25)
    mask = arr < 0.25
    t = arr[mask] / 0.25
    out[mask, 2] = 255
    out[mask, 1] = (t * 255).astype(np.uint8)
    # cyan → green (0.25 – 0.5)
    mask = (arr >= 0.25) & (arr < 0.5)
    t = (arr[mask] - 0.25) / 0.25
    out[mask, 1] = 255
    out[mask, 2] = ((1 - t) * 255).astype(np.uint8)
    # green → yellow (0.5 – 0.75)
    mask = (arr >= 0.5) & (arr < 0.75)
    t = (arr[mask] - 0.5) / 0.25
    out[mask, 1] = 255
    out[mask, 0] = (t * 255).astype(np.uint8)
    # yellow → red (0.75 – 1.0)
    mask = arr >= 0.75
    t = (arr[mask] - 0.75) / 0.25
    out[mask, 0] = 255
    out[mask, 1] = ((1 - t) * 255).astype(np.uint8)
    return out


def compare(dx11_path: str, dx12_path: str):
    print("\n" + "=" * 60)
    print("PIXEL COMPARISON: DX11 vs DX12")
    print("=" * 60)

    img11 = Image.open(dx11_path).convert("RGB")
    img12 = Image.open(dx12_path).convert("RGB")

    if img11.size != img12.size:
        print(f"  SIZE MISMATCH — DX11: {img11.size}  DX12: {img12.size}")
        return

    w, h = img11.size
    print(f"  Resolution : {w} x {h}")

    a11 = np.array(img11, dtype=np.int32)   # (H, W, 3)
    a12 = np.array(img12, dtype=np.int32)
    diff = np.abs(a11 - a12)                 # per-channel absolute diff

    for ch, name in enumerate(("R", "G", "B")):
        ch_diff = diff[:, :, ch]
        print(f"\n  Channel {name}:")
        print(f"    Mean diff  : {ch_diff.mean():.3f}")
        print(f"    Max diff   : {ch_diff.max()}")
        print(f"    Std dev    : {ch_diff.std():.3f}")

    overall = diff.mean(axis=2)              # mean across RGB channels per pixel
    rmse = float(np.sqrt((diff.astype(np.float64) ** 2).mean()))
    print(f"\n  RMSE (all channels) : {rmse:.3f}")

    total = w * h
    for threshold in (1, 5, 10, 20, 40):
        count = int(np.any(diff > threshold, axis=2).sum())
        print(f"  Pixels with diff > {threshold:2d} : {count:6d}  ({100.0*count/total:.1f}%)")

    # --- diff image (amplified x8) ---
    diff_amp = np.clip(diff * 8, 0, 255).astype(np.uint8)
    Image.fromarray(diff_amp, "RGB").save(DIFF_PATH)
    print(f"\n  Saved diff image (8x amp): {DIFF_PATH}")

    # --- false-colour heatmap ---
    norm = overall.astype(np.float32) / max(1.0, float(overall.max()))
    Image.fromarray(heatmap(norm), "RGB").save(HOT_PATH)
    print(f"  Saved heatmap           : {HOT_PATH}")

    # --- row-by-row breakdown (top 10 worst rows) ---
    row_means = diff.mean(axis=(1, 2))
    top_rows = np.argsort(row_means)[-10:][::-1]
    print("\n  Top 10 rows with largest mean diff:")
    for r in top_rows:
        bar = "#" * int(row_means[r] / 2)
        print(f"    row {r:4d}  mean={row_means[r]:6.2f}  {bar}")

    # --- column-by-column breakdown (top 10 worst columns) ---
    col_means = diff.mean(axis=(0, 2))
    top_cols = np.argsort(col_means)[-10:][::-1]
    print("\n  Top 10 columns with largest mean diff:")
    for c in top_cols:
        bar = "#" * int(col_means[c] / 2)
        print(f"    col {c:4d}  mean={col_means[c]:6.2f}  {bar}")

    # --- quadrant analysis ---
    print("\n  Quadrant mean diffs (TL / TR / BL / BR):")
    mh, mw = h // 2, w // 2
    for label, region in [
        ("TL", diff[:mh,  :mw, :]),
        ("TR", diff[:mh, mw:,  :]),
        ("BL", diff[mh:, :mw,  :]),
        ("BR", diff[mh:, mw:,  :]),
    ]:
        print(f"    {label}: {region.mean():.3f}")

    print("\n  Done. Open terrain_dx11.bmp and terrain_dx12.bmp side-by-side,")
    print("  and terrain_diff_hot.bmp to see where differences are largest.")


def main():
    os.makedirs(OUT, exist_ok=True)

    if not os.path.exists(EXE):
        print(f"ERROR: exe not found: {EXE}")
        sys.exit(1)

    # --- capture DX11 ---
    if run_engine("dx11"):
        shutil.move(CAP_PATH, DX11_PATH)
        print(f"  Saved: {DX11_PATH}")
    else:
        sys.exit(1)

    # --- capture DX12 ---
    if run_engine("dx12"):
        shutil.move(CAP_PATH, DX12_PATH)
        print(f"  Saved: {DX12_PATH}")
    else:
        sys.exit(1)

    # --- compare ---
    compare(DX11_PATH, DX12_PATH)


if __name__ == "__main__":
    main()
