#!/usr/bin/env python3
"""Convert ONNX models to RKNN format for RK3576/RK3588 deployment.

Usage:
    # Convert ASR zipformer models (default target: rk3588)
    python3 scripts/convert_to_rknn.py asr

    # Convert with specific target platform
    python3 scripts/convert_to_rknn.py asr --target rk3576

    # Specify custom input/output dirs
    python3 scripts/convert_to_rknn.py asr --models-dir models/zipformer --output-dir prebuilt/aarch64/rknn-models/asr

Requirements:
    pip install rknn-toolkit2 onnx onnxsim
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List

SUPPORTED_TARGETS = ("rk3576", "rk3588")

# ============================================================
# ASR: zipformer-bilingual (encoder/decoder/joiner)
# Model: sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20
# ============================================================
ASR_MODEL_SPECS: Dict[str, Dict[str, List]] = {
    "encoder-epoch-99-avg-1.onnx": {
        "input_size_list": [
            [1, 103, 80],
            [2, 1], [2, 1], [2, 1], [2, 1], [2, 1],
            [2, 1, 256], [2, 1, 256], [2, 1, 256], [2, 1, 256], [2, 1, 256],
            [2, 192, 1, 192], [2, 96, 1, 192], [2, 48, 1, 192], [2, 24, 1, 192], [2, 96, 1, 192],
            [2, 192, 1, 96], [2, 96, 1, 96], [2, 48, 1, 96], [2, 24, 1, 96], [2, 96, 1, 96],
            [2, 192, 1, 96], [2, 96, 1, 96], [2, 48, 1, 96], [2, 24, 1, 96], [2, 96, 1, 96],
            [2, 1, 256, 30], [2, 1, 256, 30], [2, 1, 256, 30], [2, 1, 256, 30], [2, 1, 256, 30],
            [2, 1, 256, 30], [2, 1, 256, 30], [2, 1, 256, 30], [2, 1, 256, 30], [2, 1, 256, 30],
        ],
    },
    "decoder-epoch-99-avg-1.onnx": {
        "input_size_list": [[1, 2]],
    },
    "joiner-epoch-99-avg-1.onnx": {
        "input_size_list": [[1, 512], [1, 512]],
    },
}

# Future: add TTS, Vision specs here
VISION_MODEL_SPECS: Dict[str, Dict[str, List]] = {
    "retinaface_mnet_v2_fp16.onnx": {
        "input_size_list": [[1, 3, 640, 640]],
    },
    "affecnet7_fp16.onnx": {
        "input_size_list": [[1, 3, 112, 112]],
    },
}

MODEL_REGISTRY = {
    "asr": ASR_MODEL_SPECS,
    "vision": VISION_MODEL_SPECS,
}

DEFAULT_MODEL_DIRS = {
    "asr": "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20",
    "vision": "face_emotion",
}


def simplify_onnx(src: Path, dst: Path) -> None:
    import onnx
    from onnxsim import simplify

    model = onnx.load(str(src))
    simp, ok = simplify(model)
    if not ok:
        raise RuntimeError(f"onnxsim simplify failed for {src}")
    onnx.save(simp, str(dst))


def convert_one(onnx_path: Path, rknn_path: Path, io_spec: Dict[str, List], target: str) -> None:
    from rknn.api import RKNN

    tmp_path = onnx_path.with_suffix(".sim.onnx")
    print(f"  Simplifying: {onnx_path.name}")
    simplify_onnx(onnx_path, tmp_path)

    rknn = RKNN(verbose=False)
    try:
        rknn.config(target_platform=target)
        ret = rknn.load_onnx(model=str(tmp_path), input_size_list=io_spec["input_size_list"])
        if ret != 0:
            raise RuntimeError(f"load_onnx failed: {onnx_path.name}")
        ret = rknn.build(do_quantization=False)
        if ret != 0:
            raise RuntimeError(f"build failed: {onnx_path.name}")
        ret = rknn.export_rknn(str(rknn_path))
        if ret != 0:
            raise RuntimeError(f"export_rknn failed: {rknn_path.name}")
        print(f"  Exported: {rknn_path}")
    finally:
        rknn.release()
        if tmp_path.exists():
            tmp_path.unlink()


def convert_module(module: str, models_dir: Path, output_dir: Path, target: str) -> None:
    specs = MODEL_REGISTRY[module]
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Converting {module} models -> RKNN ({target})")
    print(f"  Source: {models_dir}")
    print(f"  Output: {output_dir}")

    for onnx_name, io_spec in specs.items():
        onnx_path = models_dir / onnx_name
        if not onnx_path.exists():
            print(f"  [SKIP] {onnx_path} not found")
            continue
        rknn_name = onnx_name.replace(".onnx", ".rknn")
        rknn_path = output_dir / rknn_name
        if rknn_path.exists():
            print(f"  [SKIP] {rknn_path} already exists")
            continue
        convert_one(onnx_path, rknn_path, io_spec, target)

    # Copy tokens.txt if present
    tokens_src = models_dir / "tokens.txt"
    tokens_dst = output_dir / "tokens.txt"
    if tokens_src.exists() and not tokens_dst.exists():
        import shutil
        shutil.copy2(tokens_src, tokens_dst)
        print(f"  Copied: tokens.txt")

    print("Done.")


def main() -> None:
    root = Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(
        description="Convert ONNX models to RKNN for RK3576/RK3588",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("module", choices=list(MODEL_REGISTRY.keys()),
                        help="Module to convert (asr)")
    parser.add_argument("--target", choices=SUPPORTED_TARGETS, default="rk3588",
                        help="Target platform (default: rk3588)")
    parser.add_argument("--models-dir",
                        help="Source ONNX models directory (default: models/<module-default-dir>)")
    parser.add_argument("--output-dir",
                        help="Output RKNN directory (default: models/zipformer-rknn)")
    args = parser.parse_args()

    if args.models_dir:
        models_dir = Path(args.models_dir)
    else:
        models_dir = root / "models" / DEFAULT_MODEL_DIRS[args.module]

    if args.output_dir:
        output_dir = Path(args.output_dir)
    else:
        output_dir = root / "models" / "zipformer-rknn"

    if not models_dir.exists():
        print(f"Error: models directory not found: {models_dir}", file=sys.stderr)
        sys.exit(1)

    convert_module(args.module, models_dir, output_dir, args.target)


if __name__ == "__main__":
    try:
        main()
    except ImportError as exc:
        print(f"Missing dependency: {exc}", file=sys.stderr)
        print("Install: pip install rknn-toolkit2 onnx onnxsim", file=sys.stderr)
        sys.exit(1)
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
