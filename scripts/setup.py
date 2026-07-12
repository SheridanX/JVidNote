#!/usr/bin/env python3
"""
JVidNote 依赖下载脚本 (跨平台)
自动下载 sherpa-onnx 动态库、onnxruntime、SenseVoice 语音模型

用法:
    python scripts/setup.py          # 下载 CPU 版
    python scripts/setup.py --gpu    # 下载 GPU (CUDA) 版
    python scripts/setup.py --clean  # 仅复制本地 third_party_lib 缓存
"""

import os
import sys
import tarfile
import urllib.request
import shutil
import platform
from pathlib import Path

# ---- 配置 ----
SHERPA_VER = "v1.13.3"
MODEL_NAME = "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2025-09-09"
MODEL_URL = (
    f"https://github.com/k2-fsa/sherpa-onnx/releases/download/"
    f"asr-models/{MODEL_NAME}.tar.bz2"
)

IS_WINDOWS = platform.system() == "Windows"

if IS_WINDOWS:
    SHERPA_CPU_PKG = f"sherpa-onnx-{SHERPA_VER}-win-x64-shared-no-tts"
    SHERPA_GPU_PKG = f"sherpa-onnx-{SHERPA_VER}-win-x64-cuda"
    LIB_EXT = ".dll"
else:
    SHERPA_CPU_PKG = f"sherpa-onnx-{SHERPA_VER}-linux-x64-shared-no-tts"
    SHERPA_GPU_PKG = f"sherpa-onnx-{SHERPA_VER}-linux-x64-gpu"
    LIB_EXT = ".so"


def get_project_root() -> Path:
    """返回项目根目录 (scripts/ 的上一级)"""
    return Path(__file__).resolve().parent.parent


def download(url: str, dest: Path) -> None:
    """下载文件，带进度条"""
    print(f"  下载: {url}")

    req = urllib.request.Request(url, headers={"User-Agent": "JVidNote/setup"})
    try:
        resp = urllib.request.urlopen(req, timeout=10)
    except Exception as e:
        print(f"\n  ❌ 下载失败: {e}")
        print(f"  提示: 如果 GitHub 不可达，请将 .tar.bz2 放入 third_party_lib/")
        sys.exit(1)

    total = int(resp.headers.get("Content-Length", 0))
    downloaded = 0
    block_size = 8192
    with open(dest, "wb") as f:
        while True:
            chunk = resp.read(block_size)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total > 0:
                pct = min(downloaded * 100 // total, 100)
                bar_len = 30
                filled = pct * bar_len // 100
                bar = "█" * filled + "░" * (bar_len - filled)
                sys.stdout.write(
                    f"\r  [{bar}] {pct:3d}% "
                    f"({downloaded / 1024**2:.0f}/{total / 1024**2:.0f} MB)"
                )
            else:
                sys.stdout.write(
                    f"\r  已下载: {downloaded / 1024**2:.0f} MB"
                )
            sys.stdout.flush()
    print()


def extract_tar_bz2(archive: Path, dest: Path) -> Path:
    """解压 .tar.bz2，返回解压后的顶层目录"""
    print(f"  解压: {archive.name}")
    with tarfile.open(archive, "r:bz2") as tar:
        top_dir = tar.getnames()[0].split("/")[0]
        tar.extractall(dest, filter="data")
    return dest / top_dir


def copy_libs(src_dir: Path, lib_dir: Path) -> None:
    """复制动态库文件"""
    lib_dir.mkdir(parents=True, exist_ok=True)
    pattern = f"*{LIB_EXT}*"
    count = 0
    for f in src_dir.glob(pattern):
        shutil.copy2(f, lib_dir)
        print(f"    {f.name}")
        count += 1
    print(f"  已复制 {count} 个 {LIB_EXT} 文件到 {lib_dir}")


def copy_headers(src_dir: Path, include_dir: Path) -> None:
    """复制 C/C++ 头文件"""
    src = src_dir / "include" / "sherpa-onnx"
    dst = include_dir / "sherpa-onnx"
    if dst.exists():
        print(f"  头文件已存在: {dst}")
        return
    shutil.copytree(src, dst)
    print(f"  头文件已复制到 {dst}")


def download_sherpa(pkg_name: str, lib_dir: Path, include_dir: Path,
                    cache_dir: Path) -> None:
    """下载 sherpa-onnx 包并复制 .so/.dll 和头文件"""
    url = (f"https://github.com/k2-fsa/sherpa-onnx/releases/download/"
           f"{SHERPA_VER}/{pkg_name}.tar.bz2")

    archive = cache_dir / f"{pkg_name}.tar.bz2"
    if not archive.exists():
        download(url, archive)
    else:
        print(f"  使用本地缓存: {archive}")

    extracted = extract_tar_bz2(archive, cache_dir)
    copy_libs(extracted / "lib", lib_dir)
    copy_headers(extracted, include_dir)


def download_model(model_dir: Path, cache_dir: Path) -> Path:
    """下载 SenseVoice 模型"""
    target = model_dir / MODEL_NAME
    if target.exists():
        print(f"  模型已存在: {target}")
        return target

    archive = cache_dir / f"{MODEL_NAME}.tar.bz2"
    if not archive.exists():
        download(MODEL_URL, archive)
    else:
        print(f"  使用本地缓存: {archive}")

    model_dir.mkdir(parents=True, exist_ok=True)
    return extract_tar_bz2(archive, model_dir)


def verify(lib_dir: Path, model_path: Path, is_gpu: bool) -> bool:
    """验证关键文件"""
    print()
    print("=" * 60)
    print("验证依赖:")
    print()

    ok = True
    required_libs = [
        f"libonnxruntime{LIB_EXT}",
        f"libsherpa-onnx-c-api{LIB_EXT}",
        f"libsherpa-onnx-cxx-api{LIB_EXT}",
    ]
    if is_gpu:
        required_libs += [
            f"libonnxruntime_providers_cuda{LIB_EXT}",
            f"libonnxruntime_providers_shared{LIB_EXT}",
        ]
    for name in required_libs:
        p = lib_dir / name
        if p.exists():
            size_mb = p.stat().st_size / 1024 / 1024
            print(f"  ✅ {name} ({size_mb:.1f} MB)")
        else:
            print(f"  ❌ {name} 缺失!")
            ok = False

    for name in ["model.int8.onnx", "tokens.txt"]:
        p = model_path / name
        if p.exists():
            size_mb = p.stat().st_size / 1024 / 1024
            print(f"  ✅ model/{MODEL_NAME}/{name} ({size_mb:.1f} MB)")
        else:
            print(f"  ❌ model/{MODEL_NAME}/{name} 缺失!")
            ok = False

    return ok


def main():
    use_gpu = "--gpu" in sys.argv
    mode = "GPU (CUDA)" if use_gpu else "CPU"

    root = get_project_root()
    lib_dir = root / "lib"
    include_dir = lib_dir / "include"
    model_dir = root / "model"
    cache_dir = root / "third_party_lib"
    cache_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print(f"JVidNote 依赖下载 ({mode})")
    print(f"平台: {platform.system()} ({platform.machine()})")
    print(f"项目: {root}")
    print("=" * 60)
    print()

    # 1. sherpa-onnx 动态库 + 头文件
    print("[1/2] sherpa-onnx 动态库 + 头文件")
    pkg = SHERPA_GPU_PKG if use_gpu else SHERPA_CPU_PKG
    download_sherpa(pkg, lib_dir, include_dir, cache_dir)

    # 2. SenseVoice 模型
    print()
    print("[2/2] SenseVoice 语音识别模型")
    model_path = download_model(model_dir, cache_dir)

    # 验证
    if verify(lib_dir, model_path, use_gpu):
        print()
        print("=" * 60)
        print("依赖下载完成!")
        print()
        print("编译:")
        print("  cd build && cmake .. && cmake --build .")
        print()
        print("运行 (GPU):")
        model_arg = f"model/{MODEL_NAME}"
        print(f"  ./build/src/jvidnote_cli transcribe <audio.wav> \\")
        print(f"       {model_arg}/model.int8.onnx \\")
        print(f"       {model_arg}/tokens.txt --provider cuda")
        print("=" * 60)
    else:
        print()
        print("⚠️  部分依赖缺失，请检查网络连接后重试。")
        sys.exit(1)


if __name__ == "__main__":
    main()
