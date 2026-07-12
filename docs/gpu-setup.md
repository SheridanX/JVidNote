# JVidNote GPU (CUDA) 环境搭建指南

本文档记录在 Linux (WSL2 Ubuntu 24.04) 上启用 sherpa-onnx GPU 加速的完整步骤，包括所有依赖库和常见问题解决。

---

## 前提条件

- NVIDIA 显卡 + 驱动已安装（`nvidia-smi` 能看到 GPU）
- 项目已能正常用 CPU 编译运行

验证 GPU 是否可见：

```bash
nvidia-smi
```

---

## 1. 下载 GPU 版 sherpa-onnx

从 [sherpa-onnx releases](https://github.com/k2-fsa/sherpa-onnx/releases) 下载与当前 CPU 版**同版本号**的 GPU 包。

> 本项目当前使用 v1.13.3

```bash
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.3/sherpa-onnx-v1.13.3-linux-x64-gpu.tar.bz2
tar xjf sherpa-onnx-v1.13.3-linux-x64-gpu.tar.bz2
```

---

## 2. 替换项目中的 .so 文件(项目中已经替换了， 不需要再次替换这里只是做一个记录)

GPU 包比 CPU 包多 3 个关键文件，**全部 6 个 .so 都要复制**：

```bash
cp sherpa-onnx-v1.13.3-linux-x64-gpu/lib/*.so /path/to/JVidNote/lib/
```

`JVidNote/lib/` 最终应包含：

| 文件 | 说明 |
|------|------|
| `libonnxruntime.so` | onnxruntime 主体（替换 CPU 版） |
| `libonnxruntime_providers_cuda.so` | ⭐ CUDA 执行后端（GPU 独有） |
| `libonnxruntime_providers_shared.so` | provider 加载桥接（GPU 独有） |
| `libonnxruntime_providers_tensorrt.so` | TensorRT 后端（GPU 独有，可选） |
| `libsherpa-onnx-c-api.so` | sherpa-onnx C API（替换 CPU 版） |
| `libsherpa-onnx-cxx-api.so` | sherpa-onnx C++ API（替换 CPU 版） |

---

## 3. 安装 NVIDIA CUDA 运行时库

GPU 版 onnxruntime 依赖以下 CUDA 库。本项目下载的 GPU 包链接的是 **CUDA 11.x**，而 Ubuntu 24.04 默认只提供 CUDA 12。需要**多版本共存**。

### 3.1 添加 NVIDIA 官方 apt 源

```bash
wget https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update
```

### 3.2 安装 CUDA 11.8 运行时

```bash
sudo apt install -y cuda-runtime-11-8
```

这会安装到 `/usr/local/cuda-11.8/lib64/`，与系统已有的 CUDA 12 不冲突。

**包含的关键库**：

| 库文件 | 包名 | 说明 |
|--------|------|------|
| `libcublas.so.11` | `libcublas-11-8` | 矩阵运算 |
| `libcublasLt.so.11` | `libcublas-11-8` | 矩阵运算（轻量版） |
| `libcufft.so.11` | `libcufft-11-8` | FFT 变换 |
| `libcurand.so.10` | `libcurand-11-8` | 随机数 |
| `libcusolver.so.11` | （系统自带） | 线性求解器 |
| `libcusparse.so.12` | （系统自带） | 稀疏矩阵 |

### 3.3 注册 CUDA 11.8 到系统链接器

```bash
echo "/usr/local/cuda-11.8/lib64" | sudo tee /etc/ld.so.conf.d/cuda-11-8.conf
sudo ldconfig
```

### 3.4 安装 cuDNN 8

```bash
sudo apt install -y nvidia-cudnn
```

这会下载约 860MB 的 cuDNN 8.9.2 并安装到 `/lib/x86_64-linux-gnu/`。

**包含的关键库**：

| 库文件 | 说明 |
|--------|------|
| `libcudnn.so.8` | 深度神经网络加速 |
| `libcudnn_ops_infer.so.8` | 推理算子 |
| `libcudnn_cnn_infer.so.8` | CNN 推理 |
| `libcudnn_adv_infer.so.8` | 高级推理 |

---

## 4. ABI 兼容性修复

GPU 版 sherpa-onnx 使用 **新 C++11 ABI**（`_GLIBCXX_USE_CXX11_ABI=1`），需要在 `CMakeLists.txt` 中确认：

```cmake
# GPU 版 sherpa-onnx 使用新 C++11 ABI（__cxx11），与 GCC 5+ 默认一致
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)
```

验证方法：

```bash
# 查看 .so 是否包含 __cxx11（新 ABI 标志）
nm -D lib/libsherpa-onnx-cxx-api.so | grep ReadWave
# 输出中有 __cxx11 → 新 ABI，需要 _GLIBCXX_USE_CXX11_ABI=1
# 输出中无 __cxx11 → 旧 ABI，需要 _GLIBCXX_USE_CXX11_ABI=0
```

---

## 5. 编译项目

```bash
cd build
cmake ..
cmake --build .
```

---

## 6. 运行

运行时需要同时加载项目自带的 .so 和 CUDA 11.8 的 .so：

```bash
# 先确保音频是 16kHz 单声道（避免运行时重采样开销）
ffmpeg -i input.wav -ar 16000 -ac 1 -c:a pcm_s16le input_16k.wav

# GPU 转写
LD_LIBRARY_PATH=../lib:/usr/local/cuda-11.8/lib64 \
  ./src/jvidnote_cli transcribe input_16k.wav \
  ../model/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2025-09-09/model.int8.onnx \
  ../model/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2025-09-09/tokens.txt \
  --provider cuda

# CPU 转写（对比用）
LD_LIBRARY_PATH=../lib \
  ./src/jvidnote_cli transcribe input_16k.wav \
  ../model/.../model.int8.onnx \
  ../model/.../tokens.txt
```


## 依赖库完整清单

GPU 环境下 `ldd libonnxruntime_providers_cuda.so` 会显示所有需要的 NVIDIA 库：

```
libcublas.so.11        → cuda-runtime-11-8
libcublasLt.so.11      → cuda-runtime-11-8
libcufft.so.11         → cuda-runtime-11-8
libcurand.so.10        → cuda-runtime-11-8
libcusolver.so.11      → 系统自带
libcusparse.so.12      → 系统自带
libcudnn.so.8          → nvidia-cudnn
libcudart.so.11.0      → cuda-runtime-11-8
libnvrtc.so.11.2       → cuda-runtime-11-8
```
