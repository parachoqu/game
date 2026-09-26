"""Write deterministic world-generation products outside Blender."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image

from worldgen_core import WorldData, build_export_manifest, generate_world_data, validate_config


def quantize_height_u16(height: np.ndarray) -> tuple[np.ndarray, dict[str, float | str]]:
    minimum = float(np.min(height))
    maximum = float(np.max(height))
    span = maximum - minimum
    if span <= 1e-12:
        encoded = np.zeros(height.shape, dtype=np.uint16)
    else:
        encoded = np.rint((height.astype(np.float64) - minimum) * (65535.0 / span)).astype(np.uint16)
    return encoded, {"min": minimum, "max": maximum, "encoding": "linear_uint16"}


def _digest(array: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()


def _write_png16(path: Path, data: np.ndarray) -> dict[str, float | str]:
    encoded, metadata = quantize_height_u16(data)
    Image.fromarray(encoded).save(path)
    return metadata


def _write_png8(path: Path, data: np.ndarray) -> None:
    Image.fromarray(np.asarray(data, dtype=np.uint8), mode="L").save(path)


def _serialize_world(path: Path, data: WorldData) -> None:
    np.savez_compressed(
        path,
        height=data.height,
        land=data.land,
        water=data.water,
        moisture=data.moisture,
        slope=data.slope,
        biome=data.biome,
    )


def _world_hashes(data: WorldData) -> dict[str, str]:
    return {
        "height": _digest(data.height),
        "land": _digest(data.land),
        "water": _digest(data.water),
        "moisture": _digest(data.moisture),
        "slope": _digest(data.slope),
        "biome": _digest(data.biome),
    }


def write_world_bundle(config: dict[str, Any], root: str | Path) -> dict[str, Any]:
    validate_config(config)
    root = Path(root)
    maps_dir = root / "exports" / "maps"
    data_dir = root / "source" / "data"
    reports_dir = root / "reports"
    maps_dir.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)
    reports_dir.mkdir(parents=True, exist_ok=True)

    worlds = {kind: generate_world_data(config, kind) for kind in ("atlas", "region", "turbulent")}
    hashes = {kind: _world_hashes(data) for kind, data in worlds.items()}
    second_hashes = {
        kind: _world_hashes(generate_world_data(config, kind))
        for kind in ("atlas", "region", "turbulent")
    }
    deterministic = hashes == second_hashes
    if not deterministic:
        raise RuntimeError("world generation is not deterministic for the configured seeds")

    height_encoding: dict[str, dict[str, float | str]] = {}
    for kind, data in worlds.items():
        _serialize_world(data_dir / f"{kind}.npz", data)
        height_encoding[kind] = _write_png16(maps_dir / f"{kind}_height.png", data.height)
        _write_png8(maps_dir / f"{kind}_biome.png", data.biome * 32)
        _write_png8(maps_dir / f"{kind}_water.png", data.water.astype(np.uint8) * 255)

    manifest = build_export_manifest(config, worlds)
    manifest["height_encoding"] = height_encoding
    manifest["source_data"] = {
        kind: f"../source/data/{kind}.npz" for kind in worlds
    }
    manifest_path = root / "exports" / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    report = {
        "schema_version": config["schema_version"],
        "deterministic": deterministic,
        "seeds": config["seeds"],
        "hashes": hashes,
        "shapes": {
            kind: list(data.height.shape) for kind, data in worlds.items()
        },
        "height_ranges": {
            kind: [float(data.height.min()), float(data.height.max())]
            for kind, data in worlds.items()
        },
        "files": {
            "manifest": "exports/manifest.json",
            "maps": sorted(path.name for path in maps_dir.glob("*.png")),
            "source_data": sorted(path.name for path in data_dir.glob("*.npz")),
        },
    }
    (reports_dir / "generation-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report
