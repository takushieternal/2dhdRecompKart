"""Reader for smktrace `dumpppu` files + helpers shared by the HD-2D prototypes."""
import struct, numpy as np

def load(path):
    b = open(path, "rb").read()
    o = 0
    vram = np.frombuffer(b, "<u2", 0x8000, o); o += 0x10000
    cgram = np.frombuffer(b, "<u2", 256, o); o += 512
    lineIsM7 = np.frombuffer(b, "u1", 240, o).astype(bool); o += 240
    lineM7 = np.frombuffer(b, "<i2", 240 * 8, o).reshape(240, 8); o += 240 * 16
    ncam = struct.unpack_from("<i", b, o)[0]; o += 4
    cams = []
    for i in range(8):
        v = struct.unpack_from("<11h2x2i", b, o); o += 32
        if i < ncam:
            cams.append(dict(zip("fx fy fz lfe les aas azs vof vva cx cy first n".split(), v)))
    nproj = struct.unpack_from("<i", b, o)[0]; o += 4
    projs = []
    for i in range(128):
        v = struct.unpack_from("<6hi", b, o); o += 16
        if i < nproj:
            projs.append(dict(zip("x y z h v m cam".split(), v)))
    return dict(vram=vram, cgram=cgram, lineIsM7=lineIsM7, lineM7=lineM7, cams=cams, projs=projs)

def palette_rgb(cgram):
    c = cgram.astype(np.int32)
    r = (c & 31) * 255 // 31; g = ((c >> 5) & 31) * 255 // 31; bl = ((c >> 10) & 31) * 255 // 31
    return np.stack([r, g, bl], 1).astype(np.uint8)

def mode7_texture(vram):
    """1024x1024 palette-index image of the Mode 7 plane, plus the 8x8 tile-0 fill pattern."""
    tiles = (vram[:0x4000] & 0xff).astype(np.int32).reshape(128, 128)        # tilemap (low bytes)
    chr_ = (vram[:0x4000] >> 8).astype(np.uint8).reshape(256, 8, 8)          # tile pixels (high bytes)
    img = chr_[tiles]                                                        # 128,128,8,8
    img = img.transpose(0, 2, 1, 3).reshape(1024, 1024)
    return img, chr_[0]

def ppu_m7_texel(r, line, x):
    """Texel coordinate the PPU samples for screen (x, line), from the per-line registers (no flips)."""
    A, B, C, Dd, X, Y, H, V = [int(v) for v in r]
    sx = lambda v: ((v << 3) & 0xffff) - (0x10000 if ((v << 3) & 0x8000) else 0)
    hs, vs, xc, yc = sx(H & 0x1fff) >> 3, sx(V & 0x1fff) >> 3, sx(X & 0x1fff) >> 3, sx(Y & 0x1fff) >> 3
    ch, cv = hs - xc, vs - yc
    ch = (ch | ~1023) if ch & 0x2000 else (ch & 1023)
    cv = (cv | ~1023) if cv & 0x2000 else (cv & 1023)
    ry = line
    startX = ((A * ch) & ~63) + ((B * ry) & ~63) + ((B * cv) & ~63) + (xc << 8)
    startY = ((C * ch) & ~63) + ((Dd * ry) & ~63) + ((Dd * cv) & ~63) + (yc << 8)
    return (startX + A * x) / 256.0, (startY + C * x) / 256.0
