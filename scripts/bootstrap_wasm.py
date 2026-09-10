#!/usr/bin/env python3
"""Install pinned Wasmtime and WASI SDK distributions for Felt's Wasm bots."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import platform
import shutil
import tarfile
import tempfile
import urllib.request

WASMTIME_VERSION = "48.0.1"
WASI_SDK_VERSION = "34.0"
WASI_SDK_RELEASE = "34"

ARCHES = {
    "arm64": {
        "wasmtime_arch": "aarch64",
        "wasmtime_sha256": "9e3c636ed487a41026ff76388c5fa6f3a48ea0968408d033ed4b5e8082c20d69",
        "wasi_arch": "arm64",
        "wasi_sha256": "9c59398106b417f8f14913380fdf0097a8cc0ff4af9eb3ce0065a859e88d49e9",
    },
    "x86_64": {
        "wasmtime_arch": "x86_64",
        "wasmtime_sha256": "a5d92170718d41e4bd08173049019f0cedb318d0156365a52667d4a35ea3ca69",
        "wasi_arch": "x86_64",
        "wasi_sha256": "87d27fa8adc68dee59bfbf2e22a6d34ef717c34d6bf1d8af2a56fc929d9ce0eb",
    },
}


def download(url: str, destination: Path, expected_sha256: str) -> None:
    print(f"Downloading {url}")
    with urllib.request.urlopen(url) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)
    hasher = hashlib.sha256()
    with destination.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(block)
    digest = hasher.hexdigest()
    if digest != expected_sha256:
        raise RuntimeError(
            f"checksum mismatch for {destination.name}: {digest}, "
            f"expected {expected_sha256}"
        )


def extract_single_directory(archive: Path, destination: Path) -> None:
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        temporary_path = Path(temporary)
        with tarfile.open(archive) as bundle:
            root = temporary_path.resolve()
            for member in bundle.getmembers():
                target = (temporary_path / member.name).resolve()
                if target != root and root not in target.parents:
                    raise RuntimeError(f"unsafe archive member: {member.name}")
            bundle.extractall(temporary_path)
        entries = list(temporary_path.iterdir())
        if len(entries) != 1 or not entries[0].is_dir():
            raise RuntimeError(f"unexpected archive layout in {archive.name}")
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(entries[0]), destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-dir",
        default="build/release",
        help="CMake build directory (default: build/release)",
    )
    parser.add_argument(
        "--runtime-only",
        action="store_true",
        help="install Wasmtime but not the C/C++ compiler toolchain",
    )
    args = parser.parse_args()

    if platform.system() != "Darwin":
        raise RuntimeError("Felt's pinned Wasm bootstrap currently supports macOS")
    architecture = platform.machine().lower()
    if architecture not in ARCHES:
        raise RuntimeError(f"unsupported Mac architecture: {architecture}")
    selected = ARCHES[architecture]

    build_dir = Path(args.build_dir).resolve()
    dependency_dir = build_dir / "deps"
    dependency_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as temporary:
        temporary_path = Path(temporary)
        wasmtime_name = (
            f"wasmtime-v{WASMTIME_VERSION}-"
            f"{selected['wasmtime_arch']}-macos-c-api.tar.xz"
        )
        wasmtime_url = (
            "https://github.com/bytecodealliance/wasmtime/releases/download/"
            f"v{WASMTIME_VERSION}/{wasmtime_name}"
        )
        wasmtime_archive = temporary_path / wasmtime_name
        download(wasmtime_url, wasmtime_archive, selected["wasmtime_sha256"])
        extract_single_directory(wasmtime_archive, dependency_dir / "wasmtime")

        if not args.runtime_only:
            wasi_name = (
                f"wasi-sdk-{WASI_SDK_VERSION}-{selected['wasi_arch']}-macos.tar.gz"
            )
            wasi_url = (
                "https://github.com/WebAssembly/wasi-sdk/releases/download/"
                f"wasi-sdk-{WASI_SDK_RELEASE}/{wasi_name}"
            )
            wasi_archive = temporary_path / wasi_name
            download(wasi_url, wasi_archive, selected["wasi_sha256"])
            extract_single_directory(wasi_archive, dependency_dir / "wasi-sdk")

    print(f"Installed Wasmtime in {dependency_dir / 'wasmtime'}")
    if not args.runtime_only:
        print(f"Installed WASI SDK in {dependency_dir / 'wasi-sdk'}")
    print(f"Reconfigure with: cmake -S . -B {build_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
