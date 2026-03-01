import math, os, struct, zlib
from pathlib import Path

root = Path(r"c:\Projects\other\Audiocity")
icon_dir = root / "assets" / "icons"
icon_dir.mkdir(parents=True, exist_ok=True)

def png_chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

def write_png(path: Path, w: int, h: int, rgba_bytes: bytes):
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        start = y * stride
        raw.extend(rgba_bytes[start:start+stride])
    idat = zlib.compress(bytes(raw), 9)
    data = sig + png_chunk(b'IHDR', ihdr) + png_chunk(b'IDAT', idat) + png_chunk(b'IEND', b'')
    path.write_bytes(data)

def lerp(a, b, t):
    return a + (b - a) * t

def blend(dst, src):
    da = dst[3] / 255.0
    sa = src[3] / 255.0
    out_a = sa + da * (1 - sa)
    if out_a <= 1e-6:
        return (0,0,0,0)
    out = []
    for i in range(3):
        c = (src[i] * sa + dst[i] * da * (1 - sa)) / out_a
        out.append(int(max(0, min(255, round(c)))))
    out.append(int(max(0, min(255, round(out_a * 255)))))
    return tuple(out)

def draw_icon(size: int):
    w = h = size
    px = [(0,0,0,0)] * (w*h)

    def set_px(x, y, c):
        if 0 <= x < w and 0 <= y < h:
            idx = y*w + x
            px[idx] = blend(px[idx], c)

    # gradient bg
    top = (25, 30, 58, 255)
    bot = (9, 11, 21, 255)
    for y in range(h):
        t = y / max(1, h-1)
        c = (int(lerp(top[0], bot[0], t)), int(lerp(top[1], bot[1], t)), int(lerp(top[2], bot[2], t)), 255)
        for x in range(w):
            set_px(x, y, c)

    # top glow ellipse
    cx, cy = w*0.5, h*0.30
    rx, ry = w*0.38, h*0.22
    for y in range(h):
        for x in range(w):
            nx = (x - cx) / rx
            ny = (y - cy) / ry
            d = nx*nx + ny*ny
            if d <= 1.0:
                a = int((1.0 - d) * 90)
                set_px(x, y, (72, 179, 255, a))

    ground_y = int(h*0.78)
    for y in range(ground_y, h):
        for x in range(w):
            set_px(x, y, (14, 17, 31, 255))

    buildings = [
        (0.08,0.09,0.28),(0.18,0.06,0.20),(0.25,0.11,0.36),(0.37,0.07,0.24),(0.45,0.10,0.42),
        (0.56,0.07,0.30),(0.64,0.09,0.34),(0.74,0.06,0.22),(0.81,0.11,0.40)
    ]
    for bx_n, bw_n, bh_n in buildings:
        bx = int(w*bx_n)
        bw = max(2, int(w*bw_n))
        bh = int(h*bh_n)
        by = ground_y - bh
        for y in range(by, ground_y):
            for x in range(bx, min(w, bx+bw)):
                set_px(x, y, (34, 42, 84, 255))
        step_x = max(3, bw//4)
        step_y = max(4, int(h*0.04))
        for wx in range(bx + int(step_x*0.6), min(w, bx+bw-1), step_x):
            for wy in range(by + int(step_y*0.8), ground_y-1, step_y):
                ww = max(1, int(step_x*0.25))
                wh = max(1, int(step_y*0.3))
                for yy in range(wy, min(h, wy+wh)):
                    for xx in range(wx, min(w, wx+ww)):
                        set_px(xx, yy, (127, 214, 255, 210))

    # waveform polyline
    pts = []
    left, right = w*0.08, w*0.92
    width = right - left
    mid_y, amp = h*0.47, h*0.11
    for i in range(37):
        t = i / 36.0
        x = left + width*t
        y = mid_y + math.sin(t*math.tau*1.25)*amp*(math.cos(t*math.tau*0.5)*0.35 + 0.75)
        pts.append((x,y))

    def draw_segment(x0,y0,x1,y1,radius,color):
        minx = int(max(0, math.floor(min(x0,x1)-radius-1)))
        maxx = int(min(w-1, math.ceil(max(x0,x1)+radius+1)))
        miny = int(max(0, math.floor(min(y0,y1)-radius-1)))
        maxy = int(min(h-1, math.ceil(max(y0,y1)+radius+1)))
        vx, vy = x1-x0, y1-y0
        vv = vx*vx + vy*vy
        if vv < 1e-6:
            vv = 1e-6
        for yy in range(miny, maxy+1):
            for xx in range(minx, maxx+1):
                wx, wy = xx-x0, yy-y0
                t = max(0.0, min(1.0, (wx*vx + wy*vy)/vv))
                px0, py0 = x0 + t*vx, y0 + t*vy
                dx, dy = xx-px0, yy-py0
                d = math.sqrt(dx*dx + dy*dy)
                if d <= radius:
                    fall = 1.0 if radius <= 1 else max(0.0, 1.0 - d/radius)
                    a = int(color[3] * (0.55 + 0.45*fall))
                    set_px(xx, yy, (color[0], color[1], color[2], a))

    glow_r = max(2.0, size*0.028)
    line_r = max(1.2, size*0.015)
    for i in range(len(pts)-1):
        x0,y0 = pts[i]
        x1,y1 = pts[i+1]
        draw_segment(x0,y0,x1,y1,glow_r,(84,225,255,85))
        draw_segment(x0,y0,x1,y1,line_r,(84,225,255,235))

    # flatten
    out = bytearray(w*h*4)
    for i,c in enumerate(px):
        off = i*4
        out[off:off+4] = bytes(c)
    return bytes(out)

sizes = [16,24,32,48,64,128,256,512,1024]
png_paths = []
for s in sizes:
    rgba = draw_icon(s)
    p = icon_dir / f"audiocity_icon_{s}.png"
    write_png(p, s, s, rgba)
    png_paths.append(p)

# write multi-resolution ico using png frames
ico_frames = [16,24,32,48,64,128,256]
entries = []
data_blobs = []
offset = 6 + 16*len(ico_frames)
for s in ico_frames:
    p = icon_dir / f"audiocity_icon_{s}.png"
    b = p.read_bytes()
    entries.append((s, s, len(b), offset))
    data_blobs.append(b)
    offset += len(b)

ico = bytearray()
ico += struct.pack('<HHH', 0, 1, len(entries))
for w,h,size,off in entries:
    ico += struct.pack('<BBBBHHII', 0 if w>=256 else w, 0 if h>=256 else h, 0, 0, 1, 32, size, off)
for b in data_blobs:
    ico += b
(icon_dir / 'audiocity_icon_multi.ico').write_bytes(bytes(ico))

svg = '''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#191e3a"/>
      <stop offset="100%" stop-color="#090b15"/>
    </linearGradient>
    <linearGradient id="glow" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#48b3ff" stop-opacity="0.36"/>
      <stop offset="100%" stop-color="#48b3ff" stop-opacity="0.04"/>
    </linearGradient>
  </defs>
  <rect width="1024" height="1024" fill="url(#bg)"/>
  <ellipse cx="512" cy="300" rx="390" ry="240" fill="url(#glow)"/>
  <rect y="798" width="1024" height="226" fill="#0e111f"/>
  <g fill="#222a54">
    <rect x="82" y="512" width="92" height="286"/>
    <rect x="184" y="590" width="60" height="208"/>
    <rect x="256" y="430" width="112" height="368"/>
    <rect x="379" y="552" width="72" height="246"/>
    <rect x="460" y="370" width="104" height="428"/>
    <rect x="574" y="488" width="72" height="310"/>
    <rect x="656" y="450" width="96" height="348"/>
    <rect x="766" y="566" width="60" height="232"/>
    <rect x="836" y="390" width="112" height="408"/>
  </g>
  <polyline fill="none" stroke="#54e1ff" stroke-opacity="0.32" stroke-width="56" stroke-linecap="round" stroke-linejoin="round"
    points="82,483 124,455 166,408 208,363 250,336 292,334 334,359 376,407 418,469 460,529 502,571 544,582 586,561 628,516 670,463 712,419 754,397 796,402 838,433 880,482 922,537"/>
  <polyline fill="none" stroke="#54e1ff" stroke-width="30" stroke-linecap="round" stroke-linejoin="round"
    points="82,483 124,455 166,408 208,363 250,336 292,334 334,359 376,407 418,469 460,529 502,571 544,582 586,561 628,516 670,463 712,419 754,397 796,402 838,433 880,482 922,537"/>
</svg>'''
(icon_dir / 'audiocity_icon_source.svg').write_text(svg, encoding='utf-8')

print('Generated', len(sizes), 'PNGs + multi-res ICO at', icon_dir)
