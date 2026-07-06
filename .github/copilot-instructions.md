# JVidNote 项目规范

## 编码约定

### 成员变量命名

| 类型 | 前缀 | 示例 |
|------|------|------|
| `std::unique_ptr<T>` | `m_up_` | `m_up_in_fmt_ctx`, `m_up_dec_ctx` |
| `std::shared_ptr<T>` | `m_sp_` | `m_sp_buffer` |
| 裸指针 (T*) | `m_p_` | `m_p_in_audio_stream`, `m_p_out_stream` |
| 普通变量/标量 | `m_` | `m_audio_stream_index`, `m_sample_rate` |

### 代码风格

- C++17 标准
- 2 空格缩进，不使用 Tab
- 类型/类名: PascalCase（如 `AudioExtractor`）
- 函数/方法: camel_case（如 `open_input`, `setup_encoder`）
- 局部变量: camel_case（如 `raw`, `decoded_frame`）
- 不使用 trailing underscore（`_` 后缀），统一用 `m_` 前缀体系
- `#pragma once` 而非 include guard
- 头文件只做前向声明，不暴露 FFmpeg 细节
- 枚举: EXAMPLE_TYPE 全大写加下划线，枚举值: EXAMPLE_VALUE 全大写加下划线

- 一个函数不允许出现多个return 出口， 如果函数比较复杂请使用 
```cpp do {
    ...
    if (error) break;
    ...
} while (false);
```
- 列数不超过80列， 偶尔可以超过80列不超过90
- 函数 后的{ 需要换行， 并且{ 需要和函数名对齐

### FFmpeg 资源管理

- 所有分配堆的资源必须用 `std::unique_ptr` + 自定义删除器管理(如果需要的话)
- 如果需要在多个地方使用同一个资源，使用 `std::shared_ptr` 管理 + 自定义删除器(如果需要的话)
- `AVStream*` 等借用指针不拥有所有权，用 `m_p` 前缀表示
- 禁止手动 `av_*_free` / `av_*_close`，统一由 unique_ptr 删除器处理

### 头文件组织

```cpp
#pragma once

#include <标准库>

struct 前向声明;

class ClassName { ... };
```

### 错误处理

- 函数返回 `bool` 表示成功/失败
- 错误信息输出到 `stderr`，正常信息输出到 `stdout`
- 不抛异常

## 项目结构

```
JVidNote/
├── CMakeLists.txt
├── .clang-format
├── .clang-tidy
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── lib/
│       ├── CMakeLists.txt
│       ├── audio_extractor.h
│       └── audio_extractor.cpp
└── build/
```

## 构建

```bash
cd build && cmake .. && cmake --build .
```
