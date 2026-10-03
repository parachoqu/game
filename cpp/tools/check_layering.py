#!/usr/bin/env python3
"""Confere a separação de camadas descrita em ARQUITETURA.md.

Cada diretório de src/ só pode incluir as camadas que estão abaixo dele:

    core    → (nada do projeto além de core)
    net     → core
    sim     → core                (a simulação não conhece rede, servidor nem cliente)
    server  → core, net, sim
    client  → core, net           (o cliente nunca inclui a simulação nem o servidor)

Além disso, core/net/sim/server não podem incluir bibliotecas de janela, GPU, áudio ou UI, e dentro do
cliente só client/platform, client/render, client/audio e client/app falam com a plataforma (SDL): o resto
(rpg_client_core) é lógica testável sem janela.
Sai com código 1 e lista cada violação (arquivo:linha) se alguma regra for quebrada.

    python3 tools/check_layering.py [raiz-do-cpp]
"""
import pathlib
import re
import sys

ALLOWED = {
    "core": {"core"},
    "net": {"core", "net"},
    "sim": {"core", "sim"},
    "server": {"core", "net", "sim", "server"},
    "client": {"core", "net", "client"},
}
LAYERS = set(ALLOWED)

# Bibliotecas que só o cliente (e os apps que o usam) pode incluir.
CLIENT_ONLY = re.compile(r"^(SDL[23]?/|SDL|RmlUi/|imgui|backends/imgui|fastgltf/|meshoptimizer|webp/|GL/|vulkan/|d3d|Metal/)")

# Partes do cliente que podem usar a plataforma (SDL, GPU, áudio).
CLIENT_PLATFORM_DIRS = {"platform", "render", "audio", "app"}
PLATFORM_ONLY = re.compile(r"^(SDL[23]?/|SDL)")

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".inl"}


def check(root: pathlib.Path) -> list[str]:
    problems = []
    src = root / "src"
    for path in sorted(src.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        layer = path.relative_to(src).parts[0]
        if layer not in ALLOWED:
            problems.append(f"{path.relative_to(root)}: diretório fora das camadas conhecidas ({', '.join(sorted(LAYERS))})")
            continue
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            target = m.group(1)
            first = target.split("/", 1)[0]
            where = f"{path.relative_to(root)}:{lineno}"
            if first in LAYERS and first not in ALLOWED[layer]:
                problems.append(f"{where}: a camada '{layer}' não pode incluir '{target}'")
            if layer != "client" and CLIENT_ONLY.match(target):
                problems.append(f"{where}: '{target}' é biblioteca de cliente (janela/GPU/UI) e não pode entrar em '{layer}'")
            parts = path.relative_to(src).parts
            if layer == "client" and PLATFORM_ONLY.match(target) and (len(parts) < 3 or parts[1] not in CLIENT_PLATFORM_DIRS):
                problems.append(f"{where}: '{target}' é da plataforma; em client/ só {sorted(CLIENT_PLATFORM_DIRS)} podem incluí-la")
    return problems


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
    problems = check(root)
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} violação(ões) de camada")
        return 1
    print("camadas ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
