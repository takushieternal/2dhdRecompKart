"""Software reference renderer for the HD-2D floor (numpy). Used to validate the camera model
against the PPU's own image before the OpenGL renderer."""
import numpy as np
from ppudump import *; from camera import *

def render_floor(d, cam, rows, x0=128.0, y0=99.5, width=256, scale=1, x_off=0):
    pal = palette_rgb(d['cgram'])
    tex, fill = mode7_texture(d['vram'])
    N, R, U = basis(cam); E, F = eye(cam)
    f = cam['les']
    ys = np.arange(rows[0] * scale, rows[1] * scale) / scale + 0.5 / scale
    xs = (np.arange(width * scale) / scale + 0.5 / scale) + x_off
    X, Y = np.meshgrid(xs, ys)
    dx = -N[0] * f + R[0] * (X - x0) - U[0] * (Y - y0)
    dy = -N[1] * f + R[1] * (X - x0) - U[1] * (Y - y0)
    dz = -N[2] * f + R[2] * (X - x0) - U[2] * (Y - y0)
    ok = dz < -1e-6
    t = np.where(ok, -E[2] / np.where(ok, dz, -1), 0)
    gx = E[0] + t * dx; gy = E[1] + t * dy
    inside = (gx >= 0) & (gx < 1024) & (gy >= 0) & (gy < 1024)
    ix = np.clip(gx, 0, 1023).astype(int); iy = np.clip(gy, 0, 1023).astype(int)
    idx = np.where(inside, tex[iy, ix], fill[(gy.astype(int) & 7), (gx.astype(int) & 7)])
    img = pal[idx]
    img[~ok] = 0
    return img
