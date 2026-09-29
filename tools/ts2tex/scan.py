"""scan.py - offline texture dump for Toy Story 2 (PC).

Finds every BMP embedded in the game's data files (level textures live inside the .ngn files) and writes each one as
<key>.png, with the key ts2tex.asi computes at load time (tools/ts2tex/ts2tex.cpp): so a texture pack can cover every
level without playing through them. The picture is what the game shows for colour-keyed textures (flag 8: pure green
is transparent), which is how the level files load all but tex14/36/37.

  py scan.py                                   data -> work/texpack/src (+ index.csv)
  py scan.py --check C:/toy_story_2/game/texdump
      every texture the game dumped at run time must have been found here, with the same pixels
"""
import argparse, csv, hashlib, os, struct, sys
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def key_of(w, h, bpp, palette, bits):
    return hashlib.sha1(struct.pack('<iii', w, h, bpp) + (palette or b'') + bits).hexdigest()[:16]


def bmps(buf):
    """(offset, w, h, bpp, palette, bits) for each BMP the game's reader (ProcessBmpInfoFromStream) would accept."""
    at = buf.find(b'BM')
    while at >= 0:
        if at + 54 <= len(buf):
            off_bits, = struct.unpack_from('<I', buf, at + 10)
            bi_size, w, h, planes, bpp, comp = struct.unpack_from('<IiiHHI', buf, at + 14)
            if bi_size == 40 and 0 < w <= 4096 and 0 < h <= 4096 and planes == 1 and bpp in (8, 24) and comp == 0 \
                    and 54 <= off_bits <= 54 + 1024:
                size = w * h if bpp == 8 else w * (h * 24) // 8               # the game's fread size, no row padding
                start = at + off_bits
                if start + size <= len(buf):
                    palette = bytes(buf[at + 54:at + 54 + 1024]) if bpp == 8 else None
                    yield at, w, h, bpp, palette, bytes(buf[start:start + size])
                    at = buf.find(b'BM', start + size)
                    continue
        at = buf.find(b'BM', at + 2)


def picture(w, h, bpp, palette, bits, flags=8):
    """RGBA, top row first: SampleBitmapPixel 0x004B0870 at the bitmap's own size (the bytes land in a DIB whose
    rows are padded to 4 bytes; memory row 0 is the bottom row)."""
    stride = (w * (bpp // 8) + 3) & ~3
    mem = np.zeros(stride * h, np.uint8)
    mem[:len(bits)] = np.frombuffer(bits, np.uint8)
    rows = mem.reshape(h, stride)[::-1]
    if bpp == 8:
        pal = np.frombuffer(palette, np.uint8).reshape(256, 4)
        bgr = pal[rows[:, :w]][:, :, :3]
    else:
        bgr = rows[:, :w * 3].reshape(h, w, 3)
    rgba = np.empty((h, w, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2], rgba[..., 3] = bgr[..., 2], bgr[..., 1], bgr[..., 0], 255
    r, g, b = rgba[..., 0], rgba[..., 1], rgba[..., 2]
    if flags & 2:
        rgba[..., 3] = np.where((r == 255) & (g == 255) & (b == 255), 0, 255)
    elif flags & 4:
        rgba[..., 3] = np.where((r == 0) & (g == 0) & (b == 0), 0, 255)
    elif flags & 8:
        rgba[(r == 0) & (g == 255) & (b == 0)] = 0
    return rgba


def scan(data_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    seen, rows = set(), []
    for dirpath, _, files in os.walk(data_dir):
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            with open(path, 'rb') as f:
                buf = f.read()
            for at, w, h, bpp, palette, bits in bmps(buf):
                k = key_of(w, h, bpp, palette, bits)
                rel = os.path.relpath(path, data_dir)
                rows.append((k, rel, at, w, h, bpp))
                if k in seen:
                    continue
                seen.add(k)
                Image.fromarray(picture(w, h, bpp, palette, bits)).save(os.path.join(out_dir, k + '.png'))
    with open(os.path.join(out_dir, 'index.csv'), 'w', newline='') as f:
        wr = csv.writer(f)
        wr.writerow(['key', 'file', 'offset', 'width', 'height', 'bpp'])
        wr.writerows(rows)
    print(f'{len(rows)} bitmaps in {data_dir}, {len(seen)} distinct -> {out_dir}')


def check(runtime_dir, out_dir):
    with open(os.path.join(runtime_dir, 'index.csv'), newline='') as f:
        runtime = list(csv.DictReader(f))
    found = {os.path.splitext(n)[0] for n in os.listdir(out_dir) if n.endswith('.png')}
    missing = [r for r in runtime if r['key'] not in found]
    same = differ = 0
    for r in runtime:
        if r['key'] in found and int(r['flags']) & 0xF == 8:
            a = np.asarray(Image.open(os.path.join(runtime_dir, r['key'] + '.png')).convert('RGBA'))
            b = np.asarray(Image.open(os.path.join(out_dir, r['key'] + '.png')).convert('RGBA'))
            if a.shape == b.shape and (a == b).all():
                same += 1
            else:
                differ += 1
    print(f'runtime textures {len(runtime)}: found by the scan {len(runtime) - len(missing)}, missing {len(missing)}; '
          f'flag-8 pictures identical {same}, different {differ}')
    for r in missing[:20]:
        print('  missing', r['key'], r['name'], r['width'], 'x', r['height'], r['bpp'], 'bpp flags', r['flags'], r['file'])
    return 0 if not missing and not differ else 1


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default=os.path.join(ROOT, 'game', 'data'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'work', 'texpack', 'src'))
    ap.add_argument('--check', metavar='RUNTIME_DUMP_DIR')
    a = ap.parse_args()
    sys.exit(check(a.check, a.out) if a.check else scan(a.data, a.out))
