"""DSP-1 camera -> 3D pinhole camera in Mode 7 texel space (x east, y south, z up)."""
import math, numpy as np

def ang(a): return a * math.pi / 32768.0

def basis(cam):
    sa, ca = math.sin(ang(cam['aas'])), math.cos(ang(cam['aas']))
    sz, cz = math.sin(ang(cam['azs'])), math.cos(ang(cam['azs']))
    N = np.array([-sz * sa, sz * ca, cz])          # from focus towards the eye
    R = np.array([ca, sa, 0.0])                     # screen right
    U = np.array([sa * cz, -ca * cz, sz])           # screen up
    return N, R, U

def eye(cam, unit=4.0):
    N, R, U = basis(cam)
    F = np.array([cam['fx'], cam['fy'], cam['fz']], float)
    return (F + cam['lfe'] * N) / unit, F / unit

def ground_hit(cam, px, py, x0, y0, unit=4.0):
    """Texel where screen pixel (px, py) meets the z=0 ground plane."""
    N, R, U = basis(cam)
    E, F = eye(cam, unit)
    f = cam['les']
    d = -N * f + R * (px - x0) - U * (py - y0)
    if d[2] >= 0: return None
    t = -E[2] / d[2]
    return E + t * d
