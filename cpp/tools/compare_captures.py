#!/usr/bin/env python3
"""Compara as capturas do cliente C++ com as da demo, enquadramento a enquadramento.

    python3 cpp/tools/compare_captures.py REFERENCIA CPP [--out RELATORIO]

REFERENCIA: pasta com os PNG da demo (demo/tools/capture-reference.mjs; ou demo/captures/after).
CPP: pasta com os PNG do rpg_local (--screenshot), mesmos nomes de arquivo.

Para cada par, imprime a diferença média absoluta por canal (0–255) e o SSIM da luminância (janelas
de 8 × 8 numa redução de 1/2). Com --out, grava para cada par um PNG lado a lado
(demo | C++ | diferença ×4) e um resumo em texto. Só usa a biblioteca padrão do Python.
"""
import argparse
import os
import struct
import sys
import zlib


def read_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: não é PNG')
    pos, idat, w = 8, [], 0
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            if depth != 8 or ctype not in (2, 6):
                raise ValueError(f'{path}: só RGB/RGBA de 8 bits')
            bpp = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat.append(body)
        elif kind == b'IEND':
            break
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        prev = line
        if bpp == 3:
            out[y * w * 3:(y + 1) * w * 3] = line
        else:
            row = out[y * w * 3:(y + 1) * w * 3]
            row[0::3] = line[0::4]
            row[1::3] = line[1::4]
            row[2::3] = line[2::4]
            out[y * w * 3:(y + 1) * w * 3] = row
    return w, h, out


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 6)))
        f.write(chunk(b'IEND', b''))


def luma_half(w, h, rgb):
    hw, hh = w // 2, h // 2
    lum = [0.0] * (hw * hh)
    for y in range(hh):
        for x in range(hw):
            s = 0.0
            for dy in (0, 1):
                i = ((2 * y + dy) * w + 2 * x) * 3
                s += 0.299 * rgb[i] + 0.587 * rgb[i + 1] + 0.114 * rgb[i + 2]
                s += 0.299 * rgb[i + 3] + 0.587 * rgb[i + 4] + 0.114 * rgb[i + 5]
            lum[y * hw + x] = s / 4.0
    return hw, hh, lum


def ssim(w, h, a, b, win=8):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    total, n = 0.0, 0
    for y0 in range(0, h - win + 1, win):
        for x0 in range(0, w - win + 1, win):
            sa = sb = saa = sbb = sab = 0.0
            for y in range(y0, y0 + win):
                row = y * w
                for x in range(x0, x0 + win):
                    va, vb = a[row + x], b[row + x]
                    sa += va
                    sb += vb
                    saa += va * va
                    sbb += vb * vb
                    sab += va * vb
            k = win * win
            ma, mb = sa / k, sb / k
            va, vb = saa / k - ma * ma, sbb / k - mb * mb
            cov = sab / k - ma * mb
            total += ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))
            n += 1
    return total / max(n, 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('reference')
    ap.add_argument('candidate')
    ap.add_argument('--out')
    args = ap.parse_args()
    names = sorted(f for f in os.listdir(args.reference) if f.endswith('.png') and os.path.exists(os.path.join(args.candidate, f)))
    if not names:
        print('nenhum par de capturas com o mesmo nome', file=sys.stderr)
        return 1
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    lines = [f'{"enquadramento":32} {"dif. média":>10} {"SSIM":>6}']
    for name in names:
        wa, ha, a = read_png(os.path.join(args.reference, name))
        wb, hb, b = read_png(os.path.join(args.candidate, name))
        if (wa, ha) != (wb, hb):
            lines.append(f'{name[:-4]:32} tamanhos diferentes ({wa}x{ha} e {wb}x{hb})')
            continue
        diff = sum(abs(x - y) for x, y in zip(a, b)) / len(a)
        hw, hh, la = luma_half(wa, ha, a)
        _, _, lb = luma_half(wb, hb, b)
        s = ssim(hw, hh, la, lb)
        lines.append(f'{name[:-4]:32} {diff:10.1f} {s:6.3f}')
        print(lines[-1], flush=True)
        if args.out:
            W = wa * 3
            img = bytearray(W * ha * 3)
            for y in range(ha):
                ra = a[y * wa * 3:(y + 1) * wa * 3]
                rb = b[y * wa * 3:(y + 1) * wa * 3]
                rd = bytes(min(255, abs(p - q) * 4) for p, q in zip(ra, rb))
                img[y * W * 3:(y * W + wa) * 3] = ra
                img[(y * W + wa) * 3:(y * W + 2 * wa) * 3] = rb
                img[(y * W + 2 * wa) * 3:(y + 1) * W * 3] = rd
            write_png(os.path.join(args.out, name), W, ha, img)
    if args.out:
        with open(os.path.join(args.out, 'resumo.txt'), 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')
    print(lines[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
