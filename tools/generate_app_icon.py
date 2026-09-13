"""Generate antialiased Split State UI texture and individually hinted Windows icons."""
from pathlib import Path
import argparse
import io
import math
import struct
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[1]
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
IVORY = (235, 239, 232)
CYAN = (91, 217, 238)
INK = (19, 38, 49)
# A rounded, neutral skull with an open jaw. Sparse mesh edges survive at 16 px.
HALF = [(16, 2), (11.5, 2.4), (7.6, 4.1), (4.9, 7), (3.6, 10.5),
        (3.4, 14.5), (4.1, 18), (6, 20.7), (9.6, 22.1), (10.1, 26.4),
        (11.5, 28.4), (13.2, 29), (16, 29)]
WIRES = [(16, 2, 20, 9), (22.4, 4.1, 20, 9), (27.1, 7, 20, 9),
         (16, 10, 20, 9), (20, 9, 28.6, 14.5), (28, 18, 21, 21),
         (21, 21, 16, 22), (21, 21, 20.5, 28.4)]
DETAIL = [(16, 2, 22.4, 4.1), (16, 10, 16, 2), (16, 10, 21, 21),
          (22.4, 4.1, 27.1, 7), (21, 21, 25.8, 20.7)]


def polygons(size):
    """Return the artwork in 32-unit coordinates, with size-dependent detail."""
    stroke = max(.85, 32 / size)
    result = []

    def polygon(points, color):
        result.append((points, color))

    def line(a, b, width, color):
        dx, dy = b[0]-a[0], b[1]-a[1]
        length = math.hypot(dx, dy)
        nx, ny = -dy/length*width/2, dx/length*width/2
        polygon([(a[0]+nx,a[1]+ny),(b[0]+nx,b[1]+ny),
                 (b[0]-nx,b[1]-ny),(a[0]-nx,a[1]-ny)],color)

    def ellipse(cx, cy, rx, ry, color):
        polygon([(cx+rx*math.cos(i*math.tau/32), cy+ry*math.sin(i*math.tau/32))
                 for i in range(32)], color)

    polygon(HALF, IVORY)
    right = [(32-x,y) for x,y in HALF]
    polygon(right, INK)
    # A dark silhouette keyline keeps ivory legible on light editor themes.
    for a,b in zip(HALF,HALF[1:]): line(a,b,stroke*.7,INK)
    for a,b in zip(right,right[1:]): line(a,b,stroke,CYAN)
    for x0,y0,x1,y1 in WIRES + (DETAIL if size >= 32 else []):
        line((x0,y0),(x1,y1),stroke,CYAN)
    line((16,2),(16,29),stroke,CYAN)
    ellipse(10,14.3,3.8,4,INK)
    ellipse(22,14.3,4.4,4.6,CYAN)
    ellipse(22,14.3,4.4-stroke,4.6-stroke,INK)
    polygon([(16,18.6),(13.7,22.3),(18.3,22.3)],INK)
    for x in (13,18):
        polygon([(x,25.3),(x+stroke,25.3),(x+stroke,29.5),(x,29.5)],INK)
    return result


def render(size, hint_size=None):
    # Rasterize each size independently: a 16 px glyph gets a full-pixel mesh,
    # not the vanishing hairlines produced by shrinking a detailed large icon.
    supersample=8
    image=Image.new('RGBA',(size*supersample,size*supersample))
    draw=ImageDraw.Draw(image)
    scale=size*supersample/32
    for points,color in polygons(hint_size or size):
        draw.polygon([(x*scale,y*scale) for x,y in points],fill=(*color,255))
    return image.resize((size,size),Image.Resampling.LANCZOS)


def write_ico(path):
    entries=[];payloads=[];offset=6+16*len(SIZES)
    for size in SIZES:
        encoded=io.BytesIO();render(size).save(encoded,format='PNG');data=encoded.getvalue()
        entries.append(struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(data),offset))
        payloads.append(data);offset+=len(data)
    path.write_bytes(struct.pack('<HHH',0,1,len(SIZES))+b''.join(entries)+b''.join(payloads))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',type=Path)
    args=parser.parse_args()
    output=REPO/'SkullbonezData/branding'
    render(1024).save(output/'split-state-skull.png')
    write_ico(output/'Skullbonez.ico')
    # Premultiplied RGB keeps transparent texels from darkening filtered edges.
    # The small-mark mesh is retained when the GPU reduces the 64 px texture.
    texture = render(64, hint_size=24)
    texture.putdata([(r*a//255, g*a//255, b*a//255, a)
                     for r,g,b,a in texture.getdata()])
    texture.save(output/'split-state-ui.png')
    if args.preview:
        sheet=Image.new('RGB',(720,240),(25,34,43));draw=ImageDraw.Draw(sheet)
        for index,size in enumerate((16,20,22,24,26,32,48,64)):
            glyph=render(size);x=16+index*88
            sheet.paste(glyph,(x,30),glyph);draw.text((x,8),str(size)+' px',fill='white')
            zoom=glyph.resize((64,64),Image.Resampling.NEAREST)
            sheet.paste(zoom,(x,116),zoom)
        args.preview.parent.mkdir(parents=True,exist_ok=True);sheet.save(args.preview)
    print('Generated Split State filtered UI texture, PNG and nine individually hinted ICO sizes')


if __name__=='__main__': main()
