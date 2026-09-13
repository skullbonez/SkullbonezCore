"""Generate Split State native geometry and individually hinted Windows icons."""
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
    """Return the same bounded polygons used by native UI, in 32-unit coordinates."""
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


def render(size):
    # Rasterize each size independently: a 16 px glyph gets a full-pixel mesh,
    # not the vanishing hairlines produced by shrinking a detailed large icon.
    supersample=8
    image=Image.new('RGBA',(size*supersample,size*supersample))
    draw=ImageDraw.Draw(image)
    scale=size*supersample/32
    for points,color in polygons(size):
        draw.polygon([(x*scale,y*scale) for x,y in points],fill=(*color,255))
    return image.resize((size,size),Image.Resampling.LANCZOS)


def write_ico(path):
    entries=[];payloads=[];offset=6+16*len(SIZES)
    for size in SIZES:
        encoded=io.BytesIO();render(size).save(encoded,format='PNG');data=encoded.getvalue()
        entries.append(struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(data),offset))
        payloads.append(data);offset+=len(data)
    path.write_bytes(struct.pack('<HHH',0,1,len(SIZES))+b''.join(entries)+b''.join(payloads))


def write_native():
    def rows(values):
        return ',\n'.join('    { '+', '.join(f'{v:.6f}f' for v in row)+' }' for row in values)
    generated = """// Generated Split State geometry: tools/generate_app_icon.py shares these
// contours with Windows icons. Native strokes stay at least one physical pixel.
namespace
{
constexpr float SKULL_HALF[][2] = {
"""+rows(HALF)+"""
};
constexpr float SKULL_WIRES[][4] = {
"""+rows(WIRES)+"""
};
constexpr float SKULL_DETAIL[][4] = {
"""+rows(DETAIL)+"""
};
constexpr float SKULL_CIRCLE[][2] = {
"""+rows([(math.cos(i*math.tau/32),math.sin(i*math.tau/32)) for i in range(32)])+"""
};
constexpr Style::UIColor SKULL_IVORY { 235.0f / 255, 239.0f / 255, 232.0f / 255, 1 };
constexpr Style::UIColor SKULL_CYAN { 91.0f / 255, 217.0f / 255, 238.0f / 255, 1 };
constexpr Style::UIColor SKULL_INK { 19.0f / 255, 38.0f / 255, 49.0f / 255, 1 };

void DrawSkullStroke( const UIDrawContext& draw, const UIRect& mark, const float (&line)[4], const Style::UIColor& color, float weight = 1.0f )
{
    const float scale = mark.w / 32.0f;
    const float ax = mark.x + line[0] * scale, ay = mark.y + line[1] * scale;
    const float bx = mark.x + line[2] * scale, by = mark.y + line[3] * scale;
    const float dx = bx - ax, dy = by - ay;
    const float halfWidth = (std::max)( 1.0f, .85f * scale ) * weight * .5f;
    const float length = std::sqrt( dx * dx + dy * dy );
    const float nx = -dy / length * halfWidth, ny = dx / length * halfWidth;
    draw.Triangle( ax + nx, ay + ny, bx + nx, by + ny, ax - nx, ay - ny, color.r, color.g, color.b, 1 );
    draw.Triangle( ax - nx, ay - ny, bx + nx, by + ny, bx - nx, by - ny, color.r, color.g, color.b, 1 );
}

void DrawSkullHalf( const UIDrawContext& draw, const UIRect& mark, bool wire )
{
    const float scale = mark.w / 32.0f;
    const auto& fill = wire ? SKULL_INK : SKULL_IVORY;
    const auto& edge = wire ? SKULL_CYAN : SKULL_INK;
    const float cx = mark.x + 16 * scale, cy = mark.y + 15 * scale;
    for ( std::size_t index = 1; index < std::size( SKULL_HALF ); ++index )
    {
        const auto& a = SKULL_HALF[index - 1];
        const auto& b = SKULL_HALF[index];
        const float line[] = { wire ? 32 - a[0] : a[0], a[1], wire ? 32 - b[0] : b[0], b[1] };
        const float ax = mark.x + line[0] * scale, ay = mark.y + line[1] * scale;
        const float bx = mark.x + line[2] * scale, by = mark.y + line[3] * scale;
        // Mirroring reverses winding; both halves must face the screen-Y UI camera.
        if ( wire )
        {
            draw.Triangle( cx, cy, bx, by, ax, ay, fill.r, fill.g, fill.b, 1 );
        }
        else
        {
            draw.Triangle( cx, cy, ax, ay, bx, by, fill.r, fill.g, fill.b, 1 );
        }
        DrawSkullStroke( draw, mark, line, edge, wire ? 1.0f : .7f );
    }
}

void DrawSkullEllipse( const UIDrawContext& draw, const UIRect& mark, const float (&ellipse)[4], const Style::UIColor& color )
{
    const float scale = mark.w / 32.0f;
    const float x = mark.x + ellipse[0] * scale, y = mark.y + ellipse[1] * scale;
    for ( std::size_t index = 0; index < std::size( SKULL_CIRCLE ); ++index )
    {
        const auto& a = SKULL_CIRCLE[index];
        const auto& b = SKULL_CIRCLE[( index + 1 ) % std::size( SKULL_CIRCLE )];
        draw.Triangle( x, y, x + b[0] * ellipse[2] * scale, y + b[1] * ellipse[3] * scale,
                       x + a[0] * ellipse[2] * scale, y + a[1] * ellipse[3] * scale,
                       color.r, color.g, color.b, 1 );
    }
}
}

void DrawSkullLogo( const UIDrawContext& draw, const UIRect& bounds )
{
    const float size = std::floor( (std::max)( 0.0f, (std::min)( bounds.w, bounds.h ) ) );
    if ( size <= 0 )
    {
        return;
    }
    // Pixel-aligned origins keep the small editor marks stable at compact widths.
    const UIRect mark { std::round( bounds.x + ( bounds.w - size ) * .5f ), std::round( bounds.y + ( bounds.h - size ) * .5f ), size, size };
    const float scale = size / 32.0f;
    const float stroke = (std::max)( .85f, 32.0f / size );
    DrawSkullHalf( draw, mark, false );
    DrawSkullHalf( draw, mark, true );
    for ( const auto& line : SKULL_WIRES )
    {
        DrawSkullStroke( draw, mark, line, SKULL_CYAN );
    }
    if ( size >= 32 )
    {
        for ( const auto& line : SKULL_DETAIL )
        {
            DrawSkullStroke( draw, mark, line, SKULL_CYAN );
        }
    }
    DrawSkullStroke( draw, mark, { 16, 2, 16, 29 }, SKULL_CYAN );
    DrawSkullEllipse( draw, mark, { 10, 14.3f, 3.8f, 4 }, SKULL_INK );
    DrawSkullEllipse( draw, mark, { 22, 14.3f, 4.4f, 4.6f }, SKULL_CYAN );
    DrawSkullEllipse( draw, mark, { 22, 14.3f, 4.4f - stroke, 4.6f - stroke }, SKULL_INK );
    draw.Triangle( mark.x + 16 * scale, mark.y + 18.6f * scale, mark.x + 13.7f * scale, mark.y + 22.3f * scale,
                   mark.x + 18.3f * scale, mark.y + 22.3f * scale, SKULL_INK.r, SKULL_INK.g, SKULL_INK.b, 1 );
    for ( float tooth : { 13.0f, 18.0f } )
    {
        draw.Rect( mark.x + tooth * scale, mark.y + 25.3f * scale, stroke * scale, 4.2f * scale, SKULL_INK.r, SKULL_INK.g, SKULL_INK.b, 1 );
    }
}

"""
    cpp=REPO/'SkullbonezSource/Runtime/UI/GameUI/GameUILayout.cpp'
    source=cpp.read_text(encoding='utf-8')
    start=source.index('// Generated Split State geometry:')
    end=source.index('HeaderRects ComputeHeaderRects',start)
    cpp.write_text(source[:start]+generated+source[end:],encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',type=Path)
    args=parser.parse_args()
    output=REPO/'SkullbonezData/branding'
    render(1024).save(output/'split-state-skull.png')
    write_ico(output/'Skullbonez.ico');write_native()
    if args.preview:
        sheet=Image.new('RGB',(720,240),(25,34,43));draw=ImageDraw.Draw(sheet)
        for index,size in enumerate((16,20,22,24,26,32,48,64)):
            glyph=render(size);x=16+index*88
            sheet.paste(glyph,(x,30),glyph);draw.text((x,8),str(size)+' px',fill='white')
            zoom=glyph.resize((64,64),Image.Resampling.NEAREST)
            sheet.paste(zoom,(x,116),zoom)
        args.preview.parent.mkdir(parents=True,exist_ok=True);sheet.save(args.preview)
    print('Generated Split State native vectors, PNG and nine individually hinted ICO sizes')


if __name__=='__main__': main()
