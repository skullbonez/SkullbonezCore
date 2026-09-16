"""Generate the original cute skull as a filtered UI texture and Windows icon."""
from pathlib import Path
import argparse
import io
import struct
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[1]
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
BONE = (230, 235, 240, 255)
SOCKET = (26, 31, 38, 255)


def render(size):
    # Preserve the original 24-unit skull. Supersampling smooths each output
    # size without changing its round head, large sockets or three teeth.
    supersample = 8
    image = Image.new('RGBA', (size * supersample, size * supersample))
    draw = ImageDraw.Draw(image)
    scale = size * supersample / 24

    def rounded(x, y, width, height, radius, color):
        draw.rounded_rectangle(
            (x * scale, y * scale, (x + width) * scale, (y + height) * scale),
            radius=min(radius, width / 2, height / 2) * scale,
            fill=color,
        )

    rounded(2, 1, 20, 19, 10, BONE)
    for tooth in range(3):
        rounded(6 + tooth * 4, 17, 3, 6, 1, BONE)
    rounded(5, 9, 6, 6, 3, SOCKET)
    rounded(13, 9, 6, 6, 3, SOCKET)
    draw.polygon([(12 * scale, 14 * scale), (10 * scale, 18 * scale),
                  (14 * scale, 18 * scale)], fill=SOCKET)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def window_icon(size):
    # Windows uses an opaque black square; the editor retains transparency.
    background = Image.new('RGBA', (size, size), (0, 0, 0, 255))
    return Image.alpha_composite(background, render(size))


def write_ico(path):
    entries = []
    payloads = []
    offset = 6 + 16 * len(SIZES)
    for size in SIZES:
        encoded = io.BytesIO()
        window_icon(size).save(encoded, format='PNG')
        data = encoded.getvalue()
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0,
                                   1, 32, len(data), offset))
        payloads.append(data)
        offset += len(data)
    path.write_bytes(struct.pack('<HHH', 0, 1, len(SIZES)) + b''.join(entries) + b''.join(payloads))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    output = REPO / 'SkullbonezData/branding'
    render(1024).save(output / 'split-state-skull.png')
    write_ico(output / 'Skullbonez.ico')
    # Premultiplied RGB preserves coverage through the existing UI sampler.
    texture = render(64)
    texture.putdata([(r * a // 255, g * a // 255, b * a // 255, a)
                     for r, g, b, a in texture.getdata()])
    texture.save(output / 'split-state-ui.png')
    if args.preview:
        sheet = Image.new('RGB', (720, 240), (25, 34, 43))
        draw = ImageDraw.Draw(sheet)
        for index, size in enumerate((16, 20, 22, 24, 26, 32, 48, 64)):
            x = 16 + index * 88
            glyph = render(size)
            sheet.paste(glyph, (x, 30), glyph)
            draw.text((x, 8), str(size) + ' px', fill='white')
            sheet.paste(window_icon(size).resize((64, 64), Image.Resampling.NEAREST), (x, 116))
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.preview)
    print('Generated original cute skull: transparent UI texture and nine opaque black Windows icon sizes')


if __name__ == '__main__':
    main()
