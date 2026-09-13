"""Export the native header skull as a multi-resolution Windows icon (Pillow)."""

from pathlib import Path

from PIL import Image, ImageDraw


def main() -> None:
    # Match DrawSkullLogo's 24-unit geometry and colours in GameUILayout.cpp.
    # Render above the largest icon size so every embedded size has smooth edges.
    scale = 1024 / 24
    image = Image.new("RGBA", (1024, 1024))
    draw = ImageDraw.Draw(image)
    bone = (230, 235, 240, 255)
    socket = (26, 31, 38, 255)

    def rounded(x: float, y: float, w: float, h: float, radius: float, colour: tuple) -> None:
        draw.rounded_rectangle(
            (x * scale, y * scale, (x + w) * scale, (y + h) * scale),
            radius=min(radius, w / 2, h / 2) * scale,
            fill=colour,
        )

    rounded(2, 1, 20, 19, 10, bone)
    for tooth in range(3):
        rounded(6 + tooth * 4, 17, 3, 6, 1, bone)
    rounded(5, 9, 6, 6, 3, socket)
    rounded(13, 9, 6, 6, 3, socket)
    draw.polygon([(12 * scale, 14 * scale), (10 * scale, 18 * scale),
                  (14 * scale, 18 * scale)], fill=socket)

    output = Path(__file__).resolve().parents[1] / "SkullbonezData/branding/Skullbonez.ico"
    output.parent.mkdir(parents=True, exist_ok=True)
    image.resize((256, 256), Image.Resampling.LANCZOS).save(
        output, sizes=[(size, size) for size in (16, 20, 24, 32, 40, 48, 64, 128, 256)]
    )
    print(output)


if __name__ == "__main__":
    main()
