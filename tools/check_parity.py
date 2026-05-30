"""
Cross-renderer visual parity check.

Compares GL vs DX11 and GL vs DX12 screenshots. Reports average pixel
difference per pair. Fails if any pair exceeds threshold (avg_diff > 10.0).

Expects screenshots at: {REPO}/Profile/{renderer}_{scene}.bmp
Exit 0 = parity acceptable, Exit 1 = parity violation.
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow not installed. Run: py -m pip install Pillow")
    sys.exit(99)

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROFILE = os.path.join(REPO, "Profile")
THRESHOLD = 10.0

PAIRS = [
    ("gl_screenshot.bmp", "dx11_screenshot.bmp", "water_ball_test GL vs DX11"),
    ("gl_screenshot.bmp", "dx12_screenshot.bmp", "water_ball_test GL vs DX12"),
    ("gl_legacy_smoke.bmp", "dx11_legacy_smoke.bmp", "legacy_smoke GL vs DX11"),
    ("gl_legacy_smoke.bmp", "dx12_legacy_smoke.bmp", "legacy_smoke GL vs DX12"),
]


def main():
    all_pass = True

    for a_file, b_file, name in PAIRS:
        a_path = os.path.join(PROFILE, a_file)
        b_path = os.path.join(PROFILE, b_file)

        if not os.path.exists(a_path) or not os.path.exists(b_path):
            missing = a_file if not os.path.exists(a_path) else b_file
            print(f"  {name}: MISSING ({missing})")
            all_pass = False
            continue

        a_img = Image.open(a_path).convert("RGB")
        b_img = Image.open(b_path).convert("RGB")

        if a_img.size != b_img.size:
            print(f"  {name}: SIZE MISMATCH {a_img.size} vs {b_img.size}")
            all_pass = False
            continue

        pixel_count = a_img.size[0] * a_img.size[1] * 3
        total_diff = sum(abs(pa - pb) for pa, pb in zip(a_img.tobytes(), b_img.tobytes()))
        avg_diff = total_diff / pixel_count

        status = "PASS" if avg_diff <= THRESHOLD else "FAIL"
        if avg_diff > THRESHOLD:
            all_pass = False
        print(f"  {name}: avg_diff={avg_diff:.4f} [{status}]")

    if all_pass:
        print("PASS: Cross-renderer parity acceptable.")
    else:
        print("FAIL: Cross-renderer parity violated.")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
