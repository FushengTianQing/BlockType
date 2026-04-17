# Phase 0：项目基础设施
> **目标：** 建立完整的构建系统、项目骨架和测试框架，为后续开发奠定基础
> **前置依赖：** 无（这是第一个 Phase）
> **验收标准：** 项目能成功编译；测试框架运行正常；CI/CD 流水线配置完成

---

## 📌 阶段总览

```
Phase 0 包含 4 个 Stage，共 12 个 Task，预计 2 周完成。
建议并行度：Stage 0.1 和 0.2 可并行，Stage 0.3 依赖 0.1，Stage 0.4 最后完成。
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 0.1** | 构建系统 | CMake 配置、编译脚本 | 3 天 |
| **Stage 0.2** | 项目骨架 | 目录结构、核心头文件 | 3 天 |
| **Stage 0.3** | 测试框架 | GTest 集成、Lit 测试配置 | 4 天 |
| **Stage 0.4** | CI/CD 与文档 | GitHub Actions、README | 4 天 |

**Phase 0 架构图：**

```
BlockType/
├── CMakeLists.txt              # 主构建配置
├── cmake/                      # CMake 模块
│   ├── LLVM.cmake             # LLVM 配置
│   └── CompilerOptions.cmake  # 编译选项
├── include/BlockType/
│   ├── Basic/                 # 基础设施
│   ├── Lex/                   # 词法分析
│   ├── Parse/                 # 语法分析
│   ├── AST/                   # AST 定义
│   └── Sema/                  # 语义分析
├── src/
│   ├── Basic/
│   ├── Lex/
│   ├── Parse/
│   ├── AST/
│   └── Sema/
├── tests/
│   ├── unit/                  # GTest 单元测试
│   └── lit/                   # Lit 回归测试
├── docs/                      # 文档
└── tools/                     # 辅助工具
```

---

## Stage 0.1 — 构建系统

### Task 0.1.1 CMake 基础配置

**目标：** 建立 CMake 构建系统的基础配置

**开发要点：**

- **E0.1.1** 创建根目录 `CMakeLists.txt`：
  ```cmake
  cmake_minimum_required(VERSION 3.20)
  project(BlockType
    VERSION 0.1.0
    DESCRIPTION "A C++26 compiler built with AI"
    LANGUAGES CXX
  )
  
  # C++23 标准
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  
  # 构建类型
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  
  # 导出 compile_commands.json
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  ```

- **E0.1.2** 创建 `cmake/CompilerOptions.cmake`：
  ```cmake
  # 编译选项
  function(BlockType_cc_add_compile_options target)
    target_compile_options(${target} PRIVATE
      # 警告
      $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
      $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
      
      # 调试符号
      $<$<CONFIG:Debug>:-g -O0>
      $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
  endfunction()
  ```

- **E0.1.3** 创建 `cmake/LLVM.cmake` 配置 LLVM 依赖

**开发关键点提示：**
> 请为 BlockType 创建 CMake 基础配置。
>
> **根目录 CMakeLists.txt**：
> - CMake 最低版本 3.20
> - 项目名 BlockType，版本 0.1.0
> - C++23 标准，不使用扩展
> - 默认 Release 构建类型
> - 导出 compile_commands.json
>
> **cmake/CompilerOptions.cmake**：
> - 定义 `BlockType_cc_add_compile_options(target)` 函数
> - Clang/GCC: -Wall -Wextra -Wpedantic -Werror
> - MSVC: /W4 /WX
> - Debug: -g -O0
> - Release: -O3 -DNDEBUG
>
> **cmake/LLVM.cmake**：
> - 查找 LLVM 包（最低版本 18）
> - 配置 LLVM include 路径
> - 配置 LLVM 库链接
> - 支持 Linux x86_64 和 macOS arm64

**Checkpoint：** CMake 配置完成；`cmake -B build` 成功执行

---

### Task 0.1.2 LLVM 集成

**目标：** 集成 LLVM 库，为后端代码生成做准备

**开发要点：**

- **E0.2.1** 配置 LLVM 依赖：
  ```cmake
  # cmake/LLVM.cmake
  find_package(LLVM REQUIRED CONFIG)
  
  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
  
  include_directories(${LLVM_INCLUDE_DIRS})
  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
  add_definitions(${LLVM_DEFINITIONS_LIST})
  
  # LLVM 组件
  llvm_map_components_to_libnames(llvm_libs
    core
    support
    orcjit
    native
    asmparser
    bitwriter
    irreader
    transformutils
  )
  ```

- **E0.2.2** 创建 `src/LLVMCompat.cpp` 测试 LLVM 集成：
  ```cpp
  #include "llvm/IR/LLVMContext.h"
  #include "llvm/IR/Module.h"
  #include "llvm/Support/raw_ostream.h"
  
  int main() {
    llvm::LLVMContext Context;
    auto Module = std::make_unique<llvm::Module>("test", Context);
    Module->print(llvm::outs(), nullptr);
    return 0;
  }
  ```

- **E0.2.3** 配置交叉编译支持（Linux x86_64 + macOS arm64）

**开发关键点提示：**
> 请为 BlockType 集成 LLVM。
>
> **LLVM 版本**：最低 18.x
>
> **需要的 LLVM 组件**：
> - core: LLVM 核心库
> - support: 支持库
> - orcjit: JIT 编译
> - native: 目标平台支持
> - asmparser: 汇编解析
> - bitwriter: bitcode 写入
> - irreader: IR 读取
> - transformutils: 变换工具
>
> **测试集成**：
> - 创建简单的 main.cpp
> - 创建 LLVMContext 和 Module
> - 打印 Module 内容
> - 编译并运行，验证 LLVM 正确链接
>
> **平台支持**：
> - Linux x86_64
> - macOS arm64 (Apple Silicon)

**Checkpoint：** LLVM 正确链接；测试程序运行成功

---

### Task 0.1.3 编译脚本

**目标：** 创建便捷的编译和运行脚本

**开发要点：**

- **E0.3.1** 创建 `scripts/build.sh`：
  ```bash
  #!/bin/bash
  set -e
  
  BUILD_TYPE=${1:-Release}
  BUILD_DIR="build-${BUILD_TYPE,,}"
  
  echo "Building BlockType in ${BUILD_TYPE} mode..."
  
  cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/install" \
    -DLLVM_DIR="${LLVM_DIR:-/usr/lib/llvm-18/cmake}"
  
  cmake --build "${BUILD_DIR}" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
  
  echo "Build complete. Binary at ${BUILD_DIR}/bin/BlockType"
  ```

- **E0.3.2** 创建 `scripts/test.sh`：
  ```bash
  #!/bin/bash
  set -e
  
  BUILD_DIR=${1:-build-release}
  
  echo "Running tests..."
  cd "${BUILD_DIR}"
  ctest --output-on-failure -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
  ```

- **E0.3.3** 创建 `scripts/clean.sh`

**开发关键点提示：**
> 请为 BlockType 创建编译脚本。
>
> **scripts/build.sh**：
> - 接受参数：Release | Debug
> - 创建 build-release 或 build-debug 目录
> - 调用 cmake 配置
> - 调用 cmake --build 编译
> - 支持并行编译
>
> **scripts/test.sh**：
> - 运行 ctest
> - 输出测试结果
> - 失败时退出码非零
>
> **scripts/clean.sh**：
> - 删除所有 build-* 目录
> - 删除 install 目录
>
> 所有脚本需要可执行权限。

**Checkpoint：** `./scripts/build.sh Release` 编译成功

---

## Stage 0.2 — 项目骨架

### Task 0.2.1 目录结构创建

**目标：** 创建完整的项目目录结构

**开发要点：**

- **E0.2.1** 创建目录结构：
  ```
  BlockType/
  ├── CMakeLists.txt
  ├── cmake/
  ├── include/BlockType/
  │   ├── Basic/
  │   ├── Lex/
  │   ├── Parse/
  │   ├── AST/
  │   ├── Sema/
  │   └── CodeGen/
  ├── src/
  │   ├── Basic/
  │   ├── Lex/
  │   ├── Parse/
  │   ├── AST/
  │   ├── Sema/
  │   └── CodeGen/
  ├── tests/
  │   ├── unit/
  │   └── lit/
  ├── docs/
  ├── scripts/
  └── tools/
  ```

- **E0.2.2** 每个模块创建 CMakeLists.txt：
  ```cmake
  # src/Basic/CMakeLists.txt
  add_library(BlockType-basic
    SourceLocation.cpp
    Diagnostics.cpp
    FileManager.cpp
    # ...
  )
  target_include_directories(BlockType-basic PUBLIC
    ${PROJECT_SOURCE_DIR}/include
  )
  BlockType_cc_add_compile_options(BlockType-basic)
  ```

**开发关键点提示：**
> 请为 BlockType 创建项目目录结构。
>
> **目录说明**：
> - include/BlockType/：公共头文件
> - src/：源代码实现
> - tests/unit/：GTest 单元测试
> - tests/lit/：Lit 回归测试
> - docs/：文档
> - scripts/：构建脚本
> - tools/：辅助工具
>
> **模块划分**：
> - Basic：基础设施（源位置、诊断、文件管理）
> - Lex：词法分析
> - Parse：语法分析
> - AST：抽象语法树
> - Sema：语义分析
> - CodeGen：代码生成（LLVM IR）
>
> 每个模块需要独立的 CMakeLists.txt。

**Checkpoint：** 目录结构完整；每个模块的 CMakeLists.txt 存在

---

### Task 0.2.2 基础类型定义

**目标：** 定义项目的基础类型和基础设施

**开发要点：**

- **E0.2.1** 创建 `include/BlockType/Basic/LLVM.h`：
  ```cpp
  #pragma once
  
  #include "llvm/ADT/StringRef.h"
  #include "llvm/ADT/SmallVector.h"
  #include "llvm/ADT/SmallString.h"
  #include "llvm/ADT/ArrayRef.h"
  #include "llvm/ADT/PointerIntPair.h"
  #include "llvm/Support/raw_ostream.h"
  #include "llvm/Support/MemoryBuffer.h"
  
  namespace BlockType {
    using llvm::StringRef;
    using llvm::SmallVector;
    using llvm::SmallString;
    using llvm::ArrayRef;
    using llvm::raw_ostream;
    using llvm::MemoryBuffer;
  }
  ```

- **E0.2.2** 创建 `include/BlockType/Basic/SourceLocation.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/Basic/LLVM.h"
  
  namespace BlockType {
  
  class SourceLocation {
    unsigned ID = 0;
  public:
    SourceLocation() = default;
    explicit SourceLocation(unsigned id) : ID(id) {}
    
    bool isValid() const { return ID != 0; }
    bool isInvalid() const { return ID == 0; }
    unsigned getID() const { return ID; }
    
    bool operator==(const SourceLocation &RHS) const { return ID == RHS.ID; }
    bool operator!=(const SourceLocation &RHS) const { return ID != RHS.ID; }
    bool operator<(const SourceLocation &RHS) const { return ID < RHS.ID; }
  };
  
  class SourceRange {
    SourceLocation Begin, End;
  public:
    SourceRange() = default;
    SourceRange(SourceLocation B, SourceLocation E) : Begin(B), End(E) {}
    explicit SourceRange(SourceLocation Loc) : Begin(Loc), End(Loc) {}
    
    SourceLocation getBegin() const { return Begin; }
    SourceLocation getEnd() const { return End; }
    bool isValid() const { return Begin.isValid() && End.isValid(); }
  };
  
  } // namespace BlockType
  ```

- **E0.2.3** 创建 `include/BlockType/Basic/Diagnostics.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/Basic/LLVM.h"
  #include "BlockType/Basic/SourceLocation.h"
  
  namespace BlockType {
  
  enum class DiagLevel {
    Ignored, Note, Remark, Warning, Error, Fatal
  };
  
  class DiagnosticsEngine {
  public:
    void report(SourceLocation Loc, DiagLevel Level, StringRef Message);
    unsigned getNumErrors() const;
    unsigned getNumWarnings() const;
    bool hasErrorOccurred() const;
    void reset();
  };
  
  } // namespace BlockType
  ```

**开发关键点提示：**
> 请为 BlockType 创建基础类型定义。
>
> **include/BlockType/Basic/LLVM.h**：
> - 引入常用的 LLVM ADT 类型
> - 放在 BlockType 命名空间中
> - StringRef, SmallVector, ArrayRef, raw_ostream 等
>
> **include/BlockType/Basic/SourceLocation.h**：
> - SourceLocation 类：表示源代码位置
> - 内部使用 unsigned ID 表示（引用 SourceManager 中的位置）
> - SourceRange 类：表示源代码范围
> - 提供比较操作符
>
> **include/BlockType/Basic/Diagnostics.h**：
> - DiagLevel 枚举：Ignored, Note, Remark, Warning, Error, Fatal
> - DiagnosticsEngine 类：诊断引擎
> - report() 方法：报告诊断
> - 统计错误和警告数量

**Checkpoint：** 基础类型定义完成；编译通过

---

### Task 0.2.3 文件管理器

**目标：** 实现文件管理器，支持文件读取和缓存

**开发要点：**

- **E0.3.1** 创建 `include/BlockType/Basic/FileEntry.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/Basic/LLVM.h"
  
  namespace BlockType {
  
  class FileEntry {
    StringRef Name;       // 文件名
    StringRef Path;       // 完整路径
    size_t Size;          // 文件大小
    unsigned UID;         // 唯一标识符
  public:
    StringRef getName() const { return Name; }
    StringRef getPath() const { return Path; }
    size_t getSize() const { return Size; }
    unsigned getUID() const { return UID; }
  };
  
  } // namespace BlockType
  ```

- **E0.3.2** 创建 `include/BlockType/Basic/FileManager.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/Basic/LLVM.h"
  #include "BlockType/Basic/FileEntry.h"
  #include <memory>
  #include <map>
  
  namespace BlockType {
  
  class FileManager {
    std::map<StringRef, std::unique_ptr<FileEntry>> FileCache;
    unsigned NextUID = 0;
  public:
    const FileEntry* getFile(StringRef Path);
    std::unique_ptr<MemoryBuffer> getBuffer(StringRef Path);
    bool exists(StringRef Path) const;
  };
  
  } // namespace BlockType
  ```

- **E0.3.3** 实现 `src/Basic/FileManager.cpp`

**开发关键点提示：**
> 请为 BlockType 实现文件管理器。
>
> **FileEntry 类**：
> - 文件名、路径、大小
> - 唯一标识符（UID）
> - 不可变对象
>
> **FileManager 类**：
> - 缓存已打开的文件（避免重复打开）
> - getFile()：获取文件条目
> - getBuffer()：读取文件内容到内存
> - exists()：检查文件是否存在
>
> **实现要点**：
> - 使用 std::map 缓存 FileEntry
> - 使用 llvm::MemoryBuffer 读取文件
> - 处理文件打开失败的情况

**Checkpoint：** 文件管理器能正确读取和缓存文件

---

## Stage 0.3 — 测试框架

### Task 0.3.1 GTest 集成

**目标：** 集成 Google Test 测试框架

**开发要点：**

- **E0.3.1** 配置 GTest：
  ```cmake
  # cmake/GTest.cmake
  include(FetchContent)
  
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  
  FetchContent_MakeAvailable(googletest)
  
  enable_testing()
  include(GoogleTest)
  ```

- **E0.3.2** 创建测试模板：
  ```cmake
  # tests/unit/CMakeLists.txt
  add_executable(BlockType-tests
    Basic/SourceLocationTest.cpp
    Basic/FileManagerTest.cpp
    # ...
  )
  
  target_link_libraries(BlockType-tests
    BlockType-basic
    GTest::gtest_main
  )
  
  gtest_discover_tests(BlockType-tests)
  ```

- **E0.3.3** 创建第一个测试：
  ```cpp
  // tests/unit/Basic/SourceLocationTest.cpp
  #include <gtest/gtest.h>
  #include "BlockType/Basic/SourceLocation.h"
  
  using namespace BlockType;
  
  TEST(SourceLocationTest, DefaultInvalid) {
    SourceLocation Loc;
    EXPECT_FALSE(Loc.isValid());
    EXPECT_TRUE(Loc.isInvalid());
  }
  
  TEST(SourceLocationTest, ValidLocation) {
    SourceLocation Loc(1);
    EXPECT_TRUE(Loc.isValid());
    EXPECT_EQ(Loc.getID(), 1u);
  }
  ```

**开发关键点提示：**
> 请为 BlockType 集成 Google Test。
>
> **GTest 配置**：
> - 使用 FetchContent 下载 GTest v1.14.0
> - 启用 testing()
> - 包含 GoogleTest.cmake
>
> **测试结构**：
> - tests/unit/ 目录下按模块组织测试
> - 每个 .cpp 文件对应一个测试套件
> - 使用 gtest_discover_tests 自动发现测试
>
> **第一个测试**：
> - SourceLocationTest.cpp
> - 测试 SourceLocation 的有效性判断
> - 测试比较操作符

**Checkpoint：** `ctest` 运行成功，测试通过

---

### Task 0.3.2 Lit 测试配置

**目标：** 配置 LLVM Lit 测试框架

**开发要点：**

- **E0.3.1** 创建 `tests/lit/lit.cfg`：
  ```python
  import lit.formats
  
  config.name = "BlockType"
  config.test_format = lit.formats.ShTest(True)
  config.suffixes = ['.test']
  config.test_source_root = os.path.dirname(__file__)
  config.test_exec_root = os.path.join(config.binary_dir, 'tests', 'lit')
  
  # 工具路径
  config.substitutions.append(('%BlockType', os.path.join(config.binary_dir, 'bin', 'BlockType')))
  config.substitutions.append(('%FileCheck', 'FileCheck'))
  ```

- **E0.3.2** 创建 `tests/lit/lit.site.cfg`：
  ```cmake
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in
    ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
    @ONLY
  )
  ```

- **E0.3.3** 创建第一个 Lit 测试：
  ```lit
  // tests/lit/basic/version.test
  // RUN: %BlockType --version | FileCheck %s
  
  // CHECK: BlockType version {{[0-9]+\.[0-9]+\.[0-9]+}}
  ```

**开发关键点提示：**
> 请为 BlockType 配置 LLVM Lit 测试框架。
>
> **lit.cfg 配置**：
> - 测试名称：BlockType
> - 测试格式：ShTest
> - 测试后缀：.test
> - 配置 %BlockType 和 %FileCheck 替换
>
> **CMake 集成**：
> - 使用 configure_file 生成 lit.site.cfg
> - 添加 lit 测试目标
>
> **第一个测试**：
> - 测试 --version 输出
> - 使用 FileCheck 验证输出格式

**Checkpoint：** `lit tests/lit` 运行成功

---

### Task 0.3.3 测试辅助工具

**目标：** 创建测试辅助函数和工具

**开发要点：**

- **E0.3.1** 创建 `tests/TestHelpers.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/Basic/LLVM.h"
  #include <gtest/gtest.h>
  
  namespace BlockType {
  namespace test {
  
  // 创建临时文件
  std::string createTempFile(StringRef Content, StringRef Ext = ".cpp");
  
  // 从字符串解析
  class ParseHelper {
    std::unique_ptr<MemoryBuffer> Buffer;
  public:
    ParseHelper(StringRef Code);
    // ...
  };
  
  } // namespace test
  } // namespace BlockType
  ```

- **E0.3.2** 实现 `tests/TestHelpers.cpp`

**开发关键点提示：**
> 请为 BlockType 创建测试辅助工具。
>
> **TestHelpers.h**：
> - createTempFile()：创建临时文件，用于文件相关测试
> - ParseHelper 类：简化从字符串解析的测试
>
> **ParseHelper 功能**：
> - 接受源代码字符串
> - 创建 MemoryBuffer
> - 提供访问解析结果的接口

**Checkpoint：** 测试辅助工具编译通过；可在测试中使用

---

## Stage 0.4 — CI/CD 与文档

### Task 0.4.1 GitHub Actions 配置

**目标：** 配置 GitHub Actions CI/CD 流水线

**开发要点：**

- **E0.4.1** 创建 `.github/workflows/ci.yml`：
  ```yaml
  name: CI
  
  on:
    push:
      branches: [main, develop]
    pull_request:
      branches: [main]
  
  jobs:
    build-linux:
      runs-on: ubuntu-22.04
      steps:
        - uses: actions/checkout@v4
        - name: Install LLVM
          run: |
            wget https://apt.llvm.org/llvm.sh
            chmod +x llvm.sh
            sudo ./llvm.sh 18
        - name: Configure
          run: cmake -B build -DCMAKE_BUILD_TYPE=Release
        - name: Build
          run: cmake --build build -j$(nproc)
        - name: Test
          run: cd build && ctest --output-on-failure
  
    build-macos:
      runs-on: macos-14
      steps:
        - uses: actions/checkout@v4
        - name: Install LLVM
          run: brew install llvm@18
        - name: Configure
          run: cmake -B build -DCMAKE_BUILD_TYPE=Release
        - name: Build
          run: cmake --build build -j$(sysctl -n hw.ncpu)
        - name: Test
          run: cd build && ctest --output-on-failure
  ```

- **E0.4.2** 配置缓存：
  ```yaml
  - name: Cache CMake
    uses: actions/cache@v3
    with:
      path: |
        build/_deps
        ~/.cache/ccache
      key: ${{ runner.os }}-cmake-${{ hashFiles('CMakeLists.txt', 'cmake/*.cmake') }}
  ```

**开发关键点提示：**
> 请为 BlockType 配置 GitHub Actions CI/CD。
>
> **CI 流水线**：
> - Linux (ubuntu-22.04) + macOS (macos-14) 双平台
> - 安装 LLVM 18
> - CMake 配置
> - 编译
> - 运行测试
>
> **触发条件**：
> - push 到 main/develop 分支
> - pull_request 到 main 分支
>
> **缓存配置**：
> - 缓存 CMake 依赖
> - 使用 hash 作为缓存 key

**Checkpoint：** GitHub Actions 运行成功；两个平台都通过测试

---

### Task 0.4.2 README 与文档

**目标：** 创建项目 README 和基础文档

**开发要点：**

- **E0.4.1** 创建 `README.md`：
  ```markdown
  # BlockType
  
  A C++26 compiler built with AI assistance.
  
  ## Building
  
  ### Prerequisites
  
  - CMake 3.20+
  - C++23 compiler (Clang 18+, GCC 13+, or MSVC 19.35+)
  - LLVM 18+
  
  ### Quick Start
  
  ```bash
  ./scripts/build.sh Release
  ./scripts/test.sh
  ```
  
  ## Status
  
  This project is under active development. See [docs/ROADMAP.md](docs/ROADMAP.md) for the development plan.
  ```

- **E0.4.2** 创建 `docs/ROADMAP.md`
- **E0.4.3** 创建 `docs/ARCHITECTURE.md`
- **E0.4.4** 创建 `CONTRIBUTING.md`

**开发关键点提示：**
> 请为 BlockType 创建项目文档。
>
> **README.md**：
> - 项目介绍
> - 构建指南
> - 快速开始
> - 项目状态
>
> **docs/ROADMAP.md**：
> - 开发阶段规划
> - 当前进展
> - 未来计划
>
> **docs/ARCHITECTURE.md**：
> - 编译器架构概述
> - 模块划分
> - 数据流图
>
> **CONTRIBUTING.md**：
> - 贡献指南
> - 代码风格
> - 提交规范

**Checkpoint：** 文档完整；README 清晰易懂

---

### Task 0.4.3 编码规范与工具

**目标：** 定义编码规范并配置相关工具

**开发要点：**

- **E0.4.1** 创建 `.clang-format`：
  ```yaml
  BasedOnStyle: LLVM
  IndentWidth: 2
  UseTab: Never
  BreakBeforeBraces: Attach
  AllowShortFunctionsOnASingleLine: Inline
  ColumnLimit: 100
  SortIncludes: true
  IncludeBlocks: Regroup
  ```

- **E0.4.2** 创建 `.clang-tidy`：
  ```yaml
  Checks: >
    bugprone-*,
    modernize-*,
    readability-*,
    -modernize-use-trailing-return-type
  WarningsAsErrors: '*'
  ```

- **E0.4.3** 创建 `.editorconfig`：
  ```ini
  root = true
  
  [*]
  charset = utf-8
  end_of_line = lf
  indent_style = space
  indent_size = 2
  insert_final_newline = true
  
  [*.cpp]
  indent_size = 2
  
  [CMakeLists.txt]
  indent_size = 2
  ```

**开发关键点提示：**
> 请为 BlockType 配置编码规范工具。
>
> **.clang-format**：
> - 基于 LLVM 风格
> - 2 空格缩进
> - 最大行宽 100
> - 自动排序 include
>
> **.clang-tidy**：
> - 启用 bugprone、modernize、readability 检查
> - 警告作为错误
>
> **.editorconfig**：
> - UTF-8 编码
> - LF 换行
> - 2 空格缩进

**Checkpoint：** clang-format 和 clang-tidy 能正确检查代码

---

### Task 0.4.4 版本号与发布脚本

**目标：** 实现版本号管理和发布脚本

**开发要点：**

- **E0.4.1** 配置版本号：
  ```cmake
  # CMakeLists.txt
  set(BlockType_CC_VERSION_MAJOR 0)
  set(BlockType_CC_VERSION_MINOR 1)
  set(BlockType_CC_VERSION_PATCH 0)
  
  configure_file(
    ${PROJECT_SOURCE_DIR}/include/BlockType/Config/Version.h.in
    ${PROJECT_BINARY_DIR}/include/BlockType/Config/Version.h
  )
  ```

- **E0.4.2** 创建 `include/BlockType/Config/Version.h.in`：
  ```cpp
  #pragma once
  
  #define BlockType_CC_VERSION_MAJOR @BlockType_CC_VERSION_MAJOR@
  #define BlockType_CC_VERSION_MINOR @BlockType_CC_VERSION_MINOR@
  #define BlockType_CC_VERSION_PATCH @BlockType_CC_VERSION_PATCH@
  
  #define BlockType_CC_VERSION_STRING "@BlockType_CC_VERSION_MAJOR@.@BlockType_CC_VERSION_MINOR@.@BlockType_CC_VERSION_PATCH@"
  ```

- **E0.4.3** 创建 `scripts/release.sh`

**开发关键点提示：**
> 请为 BlockType 实现版本号管理。
>
> **版本号格式**：MAJOR.MINOR.PATCH
>
> **CMake 配置**：
> - 定义版本号变量
> - 使用 configure_file 生成 Version.h
>
> **Version.h.in**：
> - 定义版本号宏
> - 定义版本字符串宏
>
> **release.sh**：
> - 更新版本号
> - 创建 Git tag
> - 打包发布

**Checkpoint：** `BlockType --version` 输出正确版本号

---

## 📋 Phase 0 验收检查清单

```
[ ] CMake 配置完成
[ ] LLVM 正确链接
[ ] 编译脚本可用
[ ] 目录结构完整
[ ] 基础类型定义完成
[ ] 文件管理器实现完成
[ ] GTest 集成完成
[ ] Lit 测试配置完成
[ ] 测试辅助工具可用
[ ] GitHub Actions CI 通过
[ ] README 和文档完整
[ ] 编码规范工具配置完成
[ ] 版本号管理实现
```

---

*Phase 0 完成标志：项目能成功编译，测试框架运行正常，CI/CD 流水线配置完成，文档齐全。*
