"""upscale.py - AI first pass for the Toy Story 2 HD texture pack (Real-ESRGAN, ncnn-vulkan, on the GPU).

  py upscale.py                         every texture in work/texpack/src -> game/mods/hd-textures/textures
  py upscale.py --only KEY [KEY ...]    just these (e.g. to compare models)
  py upscale.py --model realesrgan-x4plus --pack work/texpack/try-x4plus

Per texture (src/<key>.png from scan.py or a ts2tex dump):
 1. Regions. Level textures are 256x256 pages of 64x64 cells (PS1-style atlases); each cell is its own surface and
    is often tiled across polygons. Neighbouring cells whose shared edge is continuous are one picture (a region);
    everything else stands alone. Bitmaps whose sides are not multiples of 64 (menu screens) are one region.
 2. Each region: colour is bled into transparent pixels (no green or black fringe from the colour key), padded
    (wrap for atlas regions, so tiling stays seamless; edge for the rest), upscaled x4, padding cropped. Alpha is
    upscaled bicubic and, when the source alpha is only 0/255, thresholded back to 0/255.
 3. Regions go back in place; images over 2048 per side are reduced to fit (the game stretches every texture to a
    power-of-two square anyway).
"""
import argparse, os, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = os.path.join(ROOT, 'vendor', 'realesrgan-ncnn-vulkan', 'realesrgan-ncnn-vulkan.exe')
CELL, PAD, SCALE, MAX_SIDE = 64, 16, 4, 2048


def regions(rgb):
    """Lists of (row, col) cells that form one picture; None when the image is not a grid of 64x64 cells."""
    h, w = rgb.shape[:2]
    if h % CELL or w % CELL or max(h, w) > 256:
        return None
    rows, cols = h // CELL, w // CELL
    img = rgb.astype(np.float32)
    parent = {(r, c): (r, c) for r in range(rows) for c in range(cols)}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def continuous(a_edge, a_inner, b_edge, b_inner):
        across = np.abs(a_edge - b_edge).mean()
        within = (np.abs(a_edge - a_inner).mean() + np.abs(b_edge - b_inner).mean()) / 2
        return across < 2 * within + 4

    for r in range(rows):
        for c in range(cols):
            y, x = r * CELL, c * CELL
            if c + 1 < cols and continuous(img[y:y + CELL, x + CELL - 1], img[y:y + CELL, x + CELL - 2],
                                           img[y:y + CELL, x + CELL], img[y:y + CELL, x + CELL + 1]):
                parent[find((r, c))] = find((r, c + 1))
            if r + 1 < rows and continuous(img[y + CELL - 1, x:x + CELL], img[y + CELL - 2, x:x + CELL],
                                           img[y + CELL, x:x + CELL], img[y + CELL + 1, x:x + CELL]):
                parent[find((r, c))] = find((r + 1, c))
    groups = {}
    for cell in parent:
        groups.setdefault(find(cell), []).append(cell)
    return list(groups.values())


def bleed(rgba):
    """RGB of transparent pixels taken from the nearest opaque ones, so upscaling does not smear the key colour."""
    rgb = rgba[..., :3].astype(np.float32)
    known = rgba[..., 3] > 0
    if known.all() or not known.any():
        return rgba[..., :3].copy()
    for _ in range(64):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(known, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * k[..., None]
            cnt += k
        grow = ~known & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    return rgb.clip(0, 255).astype(np.uint8)


def jobs_for(key, rgba):
    """(job name, padded RGB input, padded alpha, box in source pixels, padding mode) for each region."""
    h, w = rgba.shape[:2]
    groups = regions(rgba[..., :3])
    if groups is None:
        boxes, mode = [(0, 0, w, h)], 'edge'
    else:
        boxes, mode = [], 'wrap'
        for cells in groups:
            rs, cs = [c[0] for c in cells], [c[1] for c in cells]
            box = (min(cs) * CELL, min(rs) * CELL, (max(cs) + 1) * CELL, (max(rs) + 1) * CELL)
            if len(cells) == ((box[2] - box[0]) // CELL) * ((box[3] - box[1]) // CELL):
                boxes.append(box)
            else:                                                   # an L-shaped group: its cells one by one
                boxes += [(c * CELL, r * CELL, (c + 1) * CELL, (r + 1) * CELL) for r, c in cells]
    out = []
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        part = bleed(rgba[y0:y1, x0:x1])
        alpha = rgba[y0:y1, x0:x1, 3]
        out.append((f'{key}_{i:02d}', np.pad(part, ((PAD, PAD), (PAD, PAD), (0, 0)), mode),
                    np.pad(alpha, ((PAD, PAD), (PAD, PAD)), mode), (x0, y0, x1, y1)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=os.path.join(ROOT, 'work', 'texpack', 'src'))
    ap.add_argument('--pack', default=os.path.join(ROOT, 'game', 'mods', 'hd-textures', 'textures'))
    ap.add_argument('--model', default='realesrgan-x4plus-anime')   # truer to the originals than x4plus (wood grain)
    ap.add_argument('--only', nargs='*')
    a = ap.parse_args()
    keys = a.only or sorted(os.path.splitext(n)[0] for n in os.listdir(a.src) if n.endswith('.png'))
    os.makedirs(a.pack, exist_ok=True)
    work = tempfile.mkdtemp(prefix='ts2up_')
    try:
        inp, outp = os.path.join(work, 'in'), os.path.join(work, 'out')
        os.makedirs(inp), os.makedirs(outp)
        textures = {}
        for key in keys:
            rgba = np.asarray(Image.open(os.path.join(a.src, key + '.png')).convert('RGBA'))
            jobs = jobs_for(key, rgba)
            textures[key] = (rgba, jobs)
            for name, rgb, _, _ in jobs:
                Image.fromarray(rgb).save(os.path.join(inp, name + '.png'))
        n_jobs = sum(len(j) for _, j in textures.values())
        print(f'{len(keys)} textures, {n_jobs} regions -> {a.model}', flush=True)
        r = subprocess.run([EXE, '-i', inp, '-o', outp, '-n', a.model, '-s', str(SCALE), '-f', 'png', '-g', '0'],
                           capture_output=True, text=True, cwd=os.path.dirname(EXE))
        if r.returncode:
            print(r.stderr[-2000:])
            return 1
        for key, (rgba, jobs) in textures.items():
            h, w = rgba.shape[:2]
            page = np.zeros((h * SCALE, w * SCALE, 4), np.uint8)
            for name, _, alpha, (x0, y0, x1, y1) in jobs:
                up = np.asarray(Image.open(os.path.join(outp, name + '.png')).convert('RGB'))
                p, bw, bh = PAD * SCALE, (x1 - x0) * SCALE, (y1 - y0) * SCALE
                page[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE, :3] = up[p:p + bh, p:p + bw]
                al = np.asarray(Image.fromarray(alpha).resize((alpha.shape[1] * SCALE, alpha.shape[0] * SCALE),
                                                              Image.BICUBIC))[p:p + bh, p:p + bw]
                src_a = rgba[y0:y1, x0:x1, 3]
                if np.isin(src_a, (0, 255)).all():
                    al = np.where(al >= 128, 255, 0).astype(np.uint8)
                page[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE, 3] = al
            img = Image.fromarray(page)
            if max(img.size) > MAX_SIDE:
                f = MAX_SIDE / max(img.size)
                img = img.resize((round(img.width * f), round(img.height * f)), Image.LANCZOS)
            img.save(os.path.join(a.pack, key + '.png'))
        print(f'{len(keys)} textures written to {a.pack}')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
