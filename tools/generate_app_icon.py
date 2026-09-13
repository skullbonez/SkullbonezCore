"""Generate matching Finite Element skull geometry and Windows icon sizes."""
from pathlib import Path
import re
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[1]
# One small mesh supplies the native UI and every embedded Windows icon size.
ROWS = [
    [(8, 1), (10, .6), (12, .4), (14, .6), (16, 1)],
    [(4, 4), (8, 3), (12, 4), (16, 3), (20, 4)],
    [(2, 8), (7, 7), (12, 8), (17, 7), (22, 8)],
    [(3, 13), (7, 12), (12, 12), (17, 12), (21, 13)],
    [(5, 17), (9, 16), (12, 17), (15, 16), (19, 17)],
    [(7, 20), (10, 19), (12, 20), (14, 19), (17, 20)],
    [(8, 23), (10, 23), (12, 23), (14, 23), (16, 23)],
]


def colour(x, y):
    # Stress rises toward the right temple: ivory, cyan, blue, yellow, orange.
    stress = x + max(0, 8 - abs(y - 8)) * .28
    stops = [(9, (233, 232, 218)), (13, (66, 212, 233)),
             (17, (18, 153, 213)), (20, (255, 196, 48)), (23, (245, 75, 24))]
    if stress <= stops[0][0]: return stops[0][1]
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if stress <= b:
            t = (stress - a) / (b - a)
            return tuple(round(v + (w - v) * t) for v, w in zip(ca, cb))
    return stops[-1][1]


def triangles():
    for row in range(len(ROWS) - 1):
        for column in range(4):
            a, b = ROWS[row][column:column + 2]
            c, d = ROWS[row + 1][column:column + 2]
            for points in ((a, c, b), (b, c, d)):
                center = tuple(sum(p[axis] for p in points) / 3 for axis in (0, 1))
                yield points, colour(*center)


def main():
    image = Image.new('RGBA', (1024, 1024))
    draw = ImageDraw.Draw(image)
    scale = 1024 / 24
    mesh = list(triangles())
    lines = []
    for points, rgb in mesh:
        center = tuple(sum(p[axis] for p in points) / 3 for axis in (0, 1))
        draw.polygon([(x*scale, y*scale) for x, y in points], fill=(26, 36, 44, 255))
        inset = [(cx + (x-cx)*.94, cy + (y-cy)*.94) for x, y in points for cx, cy in [center]]
        draw.polygon([(x*scale, y*scale) for x, y in inset], fill=(*rgb, 255))
        values = [v for point in points for v in point] + [c/255 for c in rgb]
        lines.append('    { '+', '.join(f'{v:.5f}f' for v in values)+' },')
    socket = (26, 36, 44, 255)
    for x in (5, 14):
        draw.rounded_rectangle((x*scale, 9*scale, (x+5)*scale, 14*scale), radius=2*scale, fill=socket)
    draw.polygon([(12*scale,14*scale),(10*scale,18*scale),(14*scale,18*scale)],fill=socket)
    for x in (9.5, 11.75, 14):
        draw.rectangle((x*scale,20*scale,(x+.5)*scale,23*scale),fill=socket)
    output = REPO/'SkullbonezData/branding'
    image.save(output/'finite-element-skull.png')
    image.resize((256,256),Image.Resampling.LANCZOS).save(output/'Skullbonez.ico',sizes=[(s,s) for s in (16,20,24,32,40,48,64,128,256)])
    cpp = REPO/'SkullbonezSource/Runtime/UI/GameUI/GameUILayout.cpp'
    source = cpp.read_text()
    start = source.find('// Generated Finite Element mesh')
    if start < 0: start = source.index('void DrawSkullLogo(')
    end = source.index('HeaderRects ComputeHeaderRects', start)
    generated = '''// Generated Finite Element mesh: tools/generate_app_icon.py keeps native and
// Windows icon geometry identical. Each row is three XY points followed by RGB.
namespace
{
constexpr float SKULL_MESH[][9] = {
'''+ '\n'.join(lines)+'''
};
}

void DrawSkullLogo( const UIDrawContext& draw, const UIRect& bounds )
{
    const float size = (std::max)( 0.0f, (std::min)( bounds.w, bounds.h ) );
    const float scale = size / 24.0f;
    const float x = bounds.x + ( bounds.w - size ) * 0.5f;
    const float y = bounds.y + ( bounds.h - size ) * 0.5f;
    for ( const auto& face : SKULL_MESH )
    {
        const float cx = (face[0] + face[2] + face[4]) / 3;
        const float cy = (face[1] + face[3] + face[5]) / 3;
        draw.Triangle(x+face[0]*scale,y+face[1]*scale,x+face[2]*scale,y+face[3]*scale,x+face[4]*scale,y+face[5]*scale,26.0f/255,36.0f/255,44.0f/255,1);
        draw.Triangle(x+(cx+(face[0]-cx)*.94f)*scale,y+(cy+(face[1]-cy)*.94f)*scale,x+(cx+(face[2]-cx)*.94f)*scale,y+(cy+(face[3]-cy)*.94f)*scale,x+(cx+(face[4]-cx)*.94f)*scale,y+(cy+(face[5]-cy)*.94f)*scale,face[6],face[7],face[8],1);
    }
    for ( float socketX : { 5.0f, 14.0f } )
    {
        draw.RoundedRect(x+socketX*scale,y+9*scale,5*scale,5*scale,2*scale,26.0f/255,36.0f/255,44.0f/255,1);
    }
    draw.Triangle(x+12*scale,y+14*scale,x+10*scale,y+18*scale,x+14*scale,y+18*scale,26.0f/255,36.0f/255,44.0f/255,1);
    for ( float toothX : { 9.5f, 11.75f, 14.0f } )
    {
        draw.Rect(x+toothX*scale,y+20*scale,.5f*scale,3*scale,26.0f/255,36.0f/255,44.0f/255,1);
    }
}

'''
    cpp.write_text(source[:start]+generated+source[end:])
    print('Generated Finite Element native mesh, PNG and nine ICO sizes')


if __name__ == '__main__': main()
