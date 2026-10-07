#!/usr/bin/env python3
"""Compara o --benchmark do rpg_local com o da demo (demo/tools/measure-benchmark.mjs).

    python3 cpp/tools/compare_benchmark.py [DEMO.json] [CPP.json]

DEMO: padrão demo/benchmark-results.json ({nível: {ponto: {median, p95, tris, calls}}}).
CPP: padrão benchmark-cpp.json (o mesmo formato, gravado por `rpg_local --benchmark`).

Imprime, por nível e ponto, a mediana e o p95 do tempo de quadro (ms) dos dois, a razão demo / C++
(acima de 1 = o C++ é mais rápido), triângulos e chamadas. Só usa a biblioteca padrão do Python.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    demo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'demo', 'benchmark-results.json')
    cpp = sys.argv[2] if len(sys.argv) > 2 else 'benchmark-cpp.json'
    with open(demo, encoding='utf-8') as f:
        d = json.load(f)
    with open(cpp, encoding='utf-8') as f:
        c = json.load(f)
    print(f"{'nível':7} {'ponto':18} {'demo med':>9} {'C++ med':>9} {'razão':>7} {'demo p95':>9} {'C++ p95':>9} "
          f"{'tri demo':>10} {'tri C++':>10} {'cham. demo':>10} {'cham. C++':>10}")
    worse = 0
    for tier, points in c.items():
        for point, m in points.items():
            r = d.get(tier, {}).get(point)
            if not r:
                print(f'{tier:7} {point:18} (sem referência)')
                continue
            ratio = r['median'] / m['median'] if m['median'] > 0 else float('inf')
            worse += ratio < 1
            print(f"{tier:7} {point:18} {r['median']:9.1f} {m['median']:9.1f} {ratio:7.2f} {r['p95']:9.1f} {m['p95']:9.1f} "
                  f"{r['tris']:10d} {m['tris']:10d} {r['calls']:10d} {m['calls']:10d}")
    print(f"\n{worse} ponto(s) mais lentos que a demo")
    return 1 if worse else 0


if __name__ == '__main__':
    sys.exit(main())
