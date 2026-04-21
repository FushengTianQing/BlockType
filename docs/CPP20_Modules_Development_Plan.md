# C++20 模块系统开发规划方案

**版本**: v1.0  
**日期**: 2026-04-21  
**状态**: 阶段1已完成，阶段2-3规划中

---

## 目录

1. [概述](#概述)
2. [阶段划分](#阶段划分)
3. [阶段1：基础语义修复（已完成）](#阶段1基础语义修复已完成)
4. [阶段2：核心基础设施（设计阶段）](#阶段2核心基础设施设计阶段)
5. [阶段3：高级集成（规划阶段）](#阶段3高级集成规划阶段)
6. [技术设计细节](#技术设计细节)
7. [测试策略](#测试策略)
8. [风险评估](#风险评估)
9. [时间估算](#时间估算)

---

## 概述

### 当前状态

| 功能 | 状态 | 完成度 |
|------|------|--------|
| Parser层解析 | ✅ 完成 | 95% |
| AST节点定义 | ✅ 完成 | 100% |
| 符号表注册 | ✅ 完成 | 100% |
| export块解析 | ✅ 完成 | 100% |
| 模块加载机制 | ❌ 未实现 | 0% |
| BMI文件格式 | ❌ 未实现 | 0% |
| 模块依赖图 | ❌ 未实现 | 0% |
| 链接器集成 | ❌ 未实现 | 0% |
| 可见性规则 | ❌ 未实现 | 0% |
| 跨模块符号解析 | ❌ 未实现 | 0% |

### 目标

实现完整的 C++20 模块系统，支持：
- 模块声明与导入
- 模块分区（partition）
- 导出可见性控制
- 跨模块符号解析
- 与链接器集成

---

## 阶段划分

```
阶段1（已完成）──────────────────────────────────────────
  ├─ 符号表注册修复
  └─ export块解析支持

阶段2（核心基础设施）────────────────────────────────────
  ├─ ModuleManager接口设计
  ├─ BMI文件格式实现
  └─ 模块依赖图构建

阶段3（高级集成）────────────────────────────────────────
  ├─ 链接器集成
  ├─ 模块可见性规则
  └─ 跨模块符号解析
```

---

## 阶段1：基础语义修复（已完成）

### 已完成任务

#### 1.1 符号表注册修复

**位置**: `src/Sema/Sema.cpp`

**修改内容**:
```cpp
// ActOnModuleDecl (L549-555)
DeclResult Sema::ActOnModuleDecl(...) {
  auto *MD = Context.create<ModuleDecl>(...);
  registerDecl(MD);                    // ✅ 新增
  if (CurContext) CurContext->addDecl(MD);  // ✅ 新增
  return DeclResult(MD);
}

// ActOnImportDecl (L560-566)
DeclResult Sema::ActOnImportDecl(...) {
  auto *ID = Context.create<ImportDecl>(...);
  registerDecl(ID);                    // ✅ 新增
  if (CurContext) CurContext->addDecl(ID);  // ✅ 新增
  return DeclResult(ID);
}
```

**影响**: ModuleDecl/ImportDecl 现在正确注册到符号表和作用域。

---

#### 1.2 export块解析支持

**位置**: `src/Parse/ParseDecl.cpp:1043-1100`

**新增功能**:
```cpp
// 支持: export { decl1; decl2; }
ExportDecl *Parser::parseExportDeclaration() {
  if (Tok.is(TokenKind::l_brace)) {
    consumeToken(); // consume '{'
    
    llvm::SmallVector<Decl*, 8> ExportedDecls;
    while (!Tok.is(TokenKind::r_brace)) {
      Decl *D = parseDeclaration();
      if (D) ExportedDecls.push_back(D);
    }
    consumeToken(); // consume '}'
    // ...
  }
}
```

**影响**: 现在支持 `export { int x; int y; }` 语法。

---

#### 1.3 单元测试验证

**新增测试**:
- `DeclarationTest.ExportBlock` - 测试 export 块
- `DeclarationTest.ExportSingleDeclaration` - 测试单个 export

**结果**: ✅ 所有测试通过 (5/5)

---

## 阶段2：核心基础设施（设计阶段）

### 任务清单

| ID | 任务 | 优先级 | 依赖 | 预估时间 |
|----|------|--------|------|----------|
| 2.1 | ModuleManager接口设计 | P0 | - | 3天 |
| 2.2 | BMI文件格式实现 | P0 | 2.1 | 5天 |
| 2.3 | 模块依赖图构建 | P1 | 2.1 | 3天 |
| 2.4 | 模块编译流程集成 | P1 | 2.2, 2.3 | 2天 |

---

### 2.1 ModuleManager 接口设计

**目标**: 设计模块管理器，负责模块加载、缓存和生命周期管理。

#### 接口设计

```cpp
// include/blocktype/Module/ModuleManager.h

namespace blocktype {

/// ModuleInfo - 模块元信息
struct ModuleInfo {
  llvm::StringRef Name;              // 模块名 (e.g., "std.core")
  llvm::StringRef PrimaryModule;     // 主模块名
  llvm::StringRef Partition;         // 分区名（如果有）
  std::string BMIPath;               // BMI文件路径
  bool IsExported;                   // 是否导出
  bool IsPartition;                  // 是否为分区
  bool IsGlobalFragment;             // 是否为全局模块片段
  bool IsPrivateFragment;            // 是否为私有模块片段
  
  // 依赖信息
  llvm::SmallVector<llvm::StringRef, 8> Imports;
  llvm::SmallVector<llvm::StringRef, 4> Partitions;
};

/// ModuleManager - 模块管理器
class ModuleManager {
  ASTContext &Context;
  DiagnosticsEngine &Diags;
  
  /// 已加载模块缓存: Name → ModuleInfo
  llvm::StringMap<std::unique_ptr<ModuleInfo>> LoadedModules;
  
  /// 模块名 → ModuleDecl* 映射
  llvm::StringMap<ModuleDecl*> ModuleDecls;
  
  /// 当前模块（正在编译的模块）
  ModuleDecl *CurrentModule = nullptr;
  
  /// 模块搜索路径
  llvm::SmallVector<std::string, 4> SearchPaths;
  
public:
  explicit ModuleManager(ASTContext &C, DiagnosticsEngine &D);
  
  //===------------------------------------------------------------------===//
  // 模块加载
  //===------------------------------------------------------------------===//
  
  /// 加载模块（从BMI文件）
  /// \param Name 模块名
  /// \return 模块信息，失败返回 nullptr
  ModuleInfo *loadModule(llvm::StringRef Name);
  
  /// 从源文件编译模块
  /// \param SourcePath 源文件路径
  /// \return 模块信息
  ModuleInfo *compileModule(llvm::StringRef SourcePath);
  
  /// 查找模块BMI文件
  /// \param Name 模块名
  /// \return BMI文件路径，未找到返回空
  std::string findModuleBMI(llvm::StringRef Name);
  
  //===------------------------------------------------------------------===//
  // 模块注册
  //===------------------------------------------------------------------===//
  
  /// 注册模块声明
  void registerModuleDecl(ModuleDecl *MD);
  
  /// 注册导入声明
  void registerImportDecl(ImportDecl *ID);
  
  /// 获取模块声明
  ModuleDecl *getModuleDecl(llvm::StringRef Name) const;
  
  //===------------------------------------------------------------------===//
  // 当前模块管理
  //===------------------------------------------------------------------===//
  
  /// 设置当前模块
  void setCurrentModule(ModuleDecl *MD);
  
  /// 获取当前模块
  ModuleDecl *getCurrentModule() const { return CurrentModule; }
  
  /// 检查是否在模块中
  bool isInModule() const { return CurrentModule != nullptr; }
  
  //===------------------------------------------------------------------===//
  // 模块查询
  //===------------------------------------------------------------------===//
  
  /// 检查模块是否已加载
  bool isModuleLoaded(llvm::StringRef Name) const;
  
  /// 获取已加载模块列表
  llvm::ArrayRef<std::pair<std::string, std::unique_ptr<ModuleInfo>>>
  getLoadedModules() const;
  
  /// 获取模块依赖（递归）
  llvm::SmallVector<ModuleInfo*, 8> getModuleDependencies(llvm::StringRef Name);
  
  //===------------------------------------------------------------------===//
  // 搜索路径管理
  //===------------------------------------------------------------------===//
  
  /// 添加模块搜索路径
  void addSearchPath(llvm::StringRef Path);
  
  /// 设置模块搜索路径
  void setSearchPaths(llvm::ArrayRef<std::string> Paths);
  
  //===------------------------------------------------------------------===//
  // 导出符号管理
  //===------------------------------------------------------------------===//
  
  /// 标记符号为导出
  void markExported(NamedDecl *D);
  
  /// 检查符号是否导出
  bool isExported(NamedDecl *D) const;
  
  /// 获取模块的所有导出符号
  llvm::ArrayRef<NamedDecl*> getExportedSymbols(llvm::StringRef ModuleName) const;
};

} // namespace blocktype
```

#### 关键设计决策

1. **缓存策略**: 使用 `llvm::StringMap` 缓存已加载模块，避免重复加载
2. **延迟加载**: 模块在首次导入时加载，而非编译开始时
3. **搜索路径**: 支持多个搜索路径，按顺序查找
4. **当前模块**: 跟踪当前正在编译的模块，用于符号可见性判断

---

### 2.2 BMI 文件格式实现

**目标**: 设计并实现 Binary Module Interface (BMI) 文件格式。

#### BMI 文件结构

```
┌─────────────────────────────────────────┐
│ BMI Header (固定 64 字节)                │
├─────────────────────────────────────────┤
│ Module Metadata Section                  │
│   - Module Name                          │
│   - Partition Name                       │
│   - Flags (exported, partition, etc.)   │
├─────────────────────────────────────────┤
│ Dependency Section                       │
│   - Imported Modules                     │
│   - Partitions                           │
├─────────────────────────────────────────┤
│ Exported Symbols Section                 │
│   - Symbol Name                          │
│   - Symbol Kind                          │
│   - Type Information                     │
├─────────────────────────────────────────┤
│ AST Serialization Section                │
│   - Serialized AST Nodes                 │
│   - Type Information                     │
└─────────────────────────────────────────┘
```

#### BMI Header 定义

```cpp
// include/blocktype/Module/BMIFormat.h

namespace blocktype {

/// BMI 魔数
constexpr char BMI_MAGIC[] = "BTBMI";  // BlockType Binary Module Interface
constexpr uint32_t BMI_VERSION = 1;

/// BMI 文件头
struct BMIHeader {
  char Magic[6];           // "BTBMI\0"
  uint32_t Version;        // 版本号
  uint32_t ModuleNameOff;  // 模块名偏移
  uint32_t ModuleNameLen;  // 模块名长度
  uint32_t PartitionOff;   // 分区名偏移
  uint32_t PartitionLen;   // 分区名长度
  uint32_t Flags;          // 标志位
  uint32_t NumImports;     // 导入模块数
  uint32_t NumExports;     // 导出符号数
  uint32_t ASTSectionOff;  // AST段偏移
  uint32_t ASTSectionSize; // AST段大小
  uint8_t Padding[28];     // 填充至64字节
};

static_assert(sizeof(BMIHeader) == 64, "BMIHeader must be 64 bytes");

/// BMI 标志位
enum class BMIFlags : uint32_t {
  None = 0,
  IsExported = 1 << 0,
  IsPartition = 1 << 1,
  IsGlobalFragment = 1 << 2,
  IsPrivateFragment = 1 << 3,
};

} // namespace blocktype
```

#### BMI 读写接口

```cpp
// include/blocktype/Module/BMIReader.h
// include/blocktype/Module/BMIWriter.h

class BMIWriter {
  llvm::raw_fd_ostream &OS;
  ASTContext &Context;
  
public:
  /// 将模块序列化到BMI文件
  bool writeModule(ModuleDecl *MD, llvm::StringRef OutputPath);
  
private:
  void writeHeader(const BMIHeader &H);
  void writeModuleMetadata(ModuleDecl *MD);
  void writeDependencies(ModuleDecl *MD);
  void writeExportedSymbols(ModuleDecl *MD);
  void writeAST(ModuleDecl *MD);
};

class BMIReader {
  llvm::MemoryBuffer *Buffer;
  ASTContext &Context;
  
public:
  /// 从BMI文件加载模块
  ModuleInfo *readModule(llvm::StringRef BMIPath);
  
private:
  bool readHeader(BMIHeader &H);
  bool readModuleMetadata(ModuleInfo &Info);
  bool readDependencies(ModuleInfo &Info);
  bool readExportedSymbols(ModuleInfo &Info);
  bool readAST(ModuleInfo &Info);
};
```

#### AST 序列化策略

**方案A**: 简化序列化（推荐用于阶段2）
- 仅序列化导出符号的元信息（名称、类型、签名）
- 不序列化完整AST
- 优点：实现简单，文件小
- 缺点：需要源文件重新解析

**方案B**: 完整AST序列化（阶段3考虑）
- 序列化完整的AST节点
- 使用 LLVM 的 Bitcode 格式
- 优点：无需重新解析
- 缺点：实现复杂，文件大

**建议**: 阶段2采用方案A，阶段3评估是否需要方案B。

---

### 2.3 模块依赖图构建

**目标**: 构建模块依赖图，支持循环依赖检测和拓扑排序。

#### 依赖图数据结构

```cpp
// include/blocktype/Module/ModuleDependencyGraph.h

namespace blocktype {

/// ModuleNode - 依赖图节点
struct ModuleNode {
  llvm::StringRef Name;
  ModuleInfo *Info;
  llvm::SmallVector<ModuleNode*, 8> Dependencies;  // 直接依赖
  llvm::SmallVector<ModuleNode*, 8> Dependents;    // 被依赖
  bool Visited = false;   // 用于遍历
  bool InStack = false;   // 用于循环检测
};

/// ModuleDependencyGraph - 模块依赖图
class ModuleDependencyGraph {
  llvm::StringMap<std::unique_ptr<ModuleNode>> Nodes;
  
public:
  /// 添加模块节点
  ModuleNode *addNode(llvm::StringRef Name, ModuleInfo *Info);
  
  /// 添加依赖边 (A depends on B)
  void addDependency(llvm::StringRef A, llvm::StringRef B);
  
  /// 获取拓扑排序（编译顺序）
  /// \return 拓扑序，存在循环依赖返回空
  llvm::SmallVector<ModuleNode*, 16> topologicalSort();
  
  /// 检测循环依赖
  /// \return 循环依赖链，无循环返回空
  llvm::SmallVector<ModuleNode*, 8> detectCycle();
  
  /// 获取模块的所有依赖（递归）
  llvm::SmallVector<ModuleNode*, 16> getAllDependencies(llvm::StringRef Name);
  
  /// 获取模块的所有被依赖者（递归）
  llvm::SmallVector<ModuleNode*, 16> getAllDependents(llvm::StringRef Name);
  
private:
  bool dfsTopologicalSort(ModuleNode *Node, 
                          llvm::SmallVectorImpl<ModuleNode*> &Result);
  bool dfsDetectCycle(ModuleNode *Node,
                      llvm::SmallVectorImpl<ModuleNode*> &Path);
};

} // namespace blocktype
```

#### 依赖图构建流程

```
1. 解析所有模块声明
   └─ 收集 ModuleDecl 和 ImportDecl

2. 构建依赖图
   ├─ 为每个模块创建节点
   └─ 根据 import 声明添加边

3. 检测循环依赖
   └─ 使用 DFS 检测环

4. 拓扑排序
   └─ 确定编译顺序

5. 输出编译计划
   └─ 按拓扑序编译模块
```

---

### 2.4 模块编译流程集成

**目标**: 将模块编译集成到编译器主流程。

#### 编译流程

```cpp
// src/Frontend/Compiler.cpp

class Compiler {
  ModuleManager ModMgr;
  
public:
  bool compileModule(llvm::StringRef SourcePath) {
    // 1. 解析模块声明
    ModuleDecl *MD = parseModuleDeclaration(SourcePath);
    if (!MD) return false;
    
    // 2. 注册模块
    ModMgr.registerModuleDecl(MD);
    ModMgr.setCurrentModule(MD);
    
    // 3. 加载依赖模块
    for (auto &Import : MD->getImports()) {
      if (!ModMgr.loadModule(Import)) {
        return false;
      }
    }
    
    // 4. 语义分析（在模块上下文中）
    performSemaAnalysis(MD);
    
    // 5. 生成BMI文件
    llvm::StringRef BMIPath = getOutputPath(SourcePath, ".bmi");
    if (!writeBMI(MD, BMIPath)) {
      return false;
    }
    
    // 6. 生成目标文件
    llvm::StringRef ObjPath = getOutputPath(SourcePath, ".o");
    if (!generateObjectFile(MD, ObjPath)) {
      return false;
    }
    
    return true;
  }
};
```

---

## 阶段3：高级集成（规划阶段）

### 任务清单

| ID | 任务 | 优先级 | 依赖 | 预估时间 |
|----|------|--------|------|----------|
| 3.1 | 链接器集成 | P0 | 2.2, 2.4 | 4天 |
| 3.2 | 模块可见性规则 | P0 | 2.1, 2.3 | 3天 |
| 3.3 | 跨模块符号解析 | P0 | 3.2 | 4天 |
| 3.4 | 模块分区支持 | P1 | 3.3 | 3天 |
| 3.5 | 全局模块片段 | P1 | 3.3 | 2天 |

---

### 3.1 链接器集成

**目标**: 与链接器集成，支持模块链接。

#### 链接策略

**策略A**: 每个模块一个目标文件
- `MyModule.cppm` → `MyModule.o` + `MyModule.bmi`
- 链接器按依赖顺序链接目标文件
- 优点：简单，与现有工具链兼容
- 缺点：模块间符号解析需要额外信息

**策略B**: 模块合并链接
- 多个模块合并为一个目标文件
- 减少链接开销
- 缺点：实现复杂

**建议**: 阶段3采用策略A。

#### 链接器接口

```cpp
// include/blocktype/Driver/ModuleLinker.h

class ModuleLinker {
  ModuleManager &ModMgr;
  
public:
  /// 链接模块
  bool linkModules(llvm::ArrayRef<llvm::StringRef> ModuleNames,
                   llvm::StringRef OutputPath);
  
private:
  /// 收集模块目标文件
  llvm::SmallVector<std::string, 16> collectObjectFiles(
      llvm::ArrayRef<llvm::StringRef> ModuleNames);
  
  /// 调用系统链接器
  bool invokeLinker(llvm::ArrayRef<std::string> ObjectFiles,
                    llvm::StringRef OutputPath);
};
```

---

### 3.2 模块可见性规则

**目标**: 实现模块可见性规则。

#### 可见性规则

```cpp
// src/Sema/ModuleVisibility.cpp

/// 检查符号是否在当前模块可见
bool Sema::isSymbolVisible(NamedDecl *D, ModuleDecl *CurrentMod) {
  // 1. 同一模块内，所有符号可见
  if (D->getOwningModule() == CurrentMod) {
    return true;
  }
  
  // 2. 检查是否为导出符号
  ModuleDecl *OwnerMod = D->getOwningModule();
  if (!ModMgr.isExported(OwnerMod, D)) {
    return false;
  }
  
  // 3. 检查导入关系
  if (!CurrentMod->imports(OwnerMod)) {
    return false;
  }
  
  // 4. 检查传递导出
  // import A; A export import B; → B 的导出符号也可见
  return checkTransitiveExport(CurrentMod, OwnerMod);
}
```

#### 符号查找修改

```cpp
// src/Sema/Sema.cpp

NamedDecl *Sema::LookupName(llvm::StringRef Name, Scope *S) {
  // 1. 查找当前作用域链
  if (NamedDecl *D = S->lookup(Name)) {
    return D;
  }
  
  // 2. 查找全局符号表
  if (NamedDecl *D = SymbolTable.lookup(Name)) {
    // ✅ 新增：检查模块可见性
    if (ModMgr.isInModule()) {
      if (!isSymbolVisible(D, ModMgr.getCurrentModule())) {
        return nullptr;  // 符号不可见
      }
    }
    return D;
  }
  
  // 3. 查找导入模块
  if (ModMgr.isInModule()) {
    return lookupInImportedModules(Name, ModMgr.getCurrentModule());
  }
  
  return nullptr;
}
```

---

### 3.3 跨模块符号解析

**目标**: 实现跨模块符号解析。

#### 符号解析流程

```
1. 遇到 import A;
   └─ 加载 A.bmi

2. 解析 A 的导出符号
   └─ 注册到符号表（标记来源模块）

3. 查找符号 X
   ├─ 本地符号 → 返回
   ├─ 导入符号（A 导出 X）→ 返回
   └─ 未找到 → 错误

4. 类型检查
   └─ 确保跨模块类型一致
```

#### 跨模块类型比较

```cpp
// src/Sema/ModuleTypeCheck.cpp

/// 检查跨模块类型是否一致
bool Sema::checkCrossModuleType(const Type *T1, const Type *T2,
                                 ModuleDecl *M1, ModuleDecl *M2) {
  // 1. 相同类型
  if (T1 == T2) return true;
  
  // 2. 结构等价（对于导入类型）
  if (T1->getKind() != T2->getKind()) return false;
  
  // 3. 递归检查子类型
  switch (T1->getKind()) {
    case TypeKind::Pointer:
      return checkCrossModuleType(
          cast<PointerType>(T1)->getPointeeType(),
          cast<PointerType>(T2)->getPointeeType(),
          M1, M2);
    // ... 其他类型
  }
}
```

---

### 3.4 模块分区支持

**目标**: 支持模块分区（partition）。

#### 分区语法

```cpp
// 主模块接口
export module MyLib;

// 分区接口
export module MyLib:Part1;
export module MyLib:Part2;

// 分区实现
module MyLib:Part1;

// 主模块实现
module MyLib;
import :Part1;
import :Part2;
```

#### 分区处理

```cpp
// src/Sema/ModulePartition.cpp

/// 处理分区导入
DeclResult Sema::ActOnPartitionImport(llvm::StringRef PartitionName) {
  ModuleDecl *CurrentMod = ModMgr.getCurrentModule();
  
  // 1. 检查分区是否属于当前模块
  if (!CurrentMod->hasPartition(PartitionName)) {
    Diags.Report(DiagID::err_partition_not_found);
    return DeclResult::getInvalid();
  }
  
  // 2. 加载分区
  ModuleInfo *Partition = ModMgr.loadModulePartition(CurrentMod, PartitionName);
  if (!Partition) {
    return DeclResult::getInvalid();
  }
  
  // 3. 合并分区符号到主模块
  mergePartitionSymbols(CurrentMod, Partition);
  
  return DeclResult(/*success*/);
}
```

---

### 3.5 全局模块片段

**目标**: 支持全局模块片段（global module fragment）。

#### 语法

```cpp
module;  // 全局模块片段开始

#include <iostream>  // 传统头文件

export module MyLib;

export void print() {
  std::cout << "Hello" << std::endl;
}
```

#### 处理逻辑

```cpp
// src/Parse/ParseDecl.cpp

/// 解析全局模块片段
DeclResult Parser::parseGlobalModuleFragment() {
  // 1. 消费 'module;'
  consumeToken();
  
  // 2. 进入全局模块片段模式
  Actions.enterGlobalModuleFragment();
  
  // 3. 解析声明（通常是 #include）
  while (!Tok.is(TokenKind::kw_module) || !isModuleDeclaration()) {
    Decl *D = parseDeclaration();
    if (D) {
      // 标记为全局模块片段的一部分
      Actions.addToGlobalModuleFragment(D);
    }
  }
  
  // 4. 退出全局模块片段模式
  Actions.exitGlobalModuleFragment();
  
  return DeclResult(/*success*/);
}
```

---

## 技术设计细节

### 现有基础设施

#### AST 节点（已完成）

```cpp
// ModuleDecl (include/blocktype/AST/Decl.h:1288-1329)
class ModuleDecl : public NamedDecl {
  llvm::StringRef ModuleName;
  llvm::StringRef PartitionName;
  bool IsExported;
  bool IsModulePartition;
  bool IsGlobalModuleFragment;
  bool IsPrivateModuleFragment;
  // ...
};

// ImportDecl (include/blocktype/AST/Decl.h:1337-1367)
class ImportDecl : public NamedDecl {
  llvm::StringRef ModuleName;
  llvm::StringRef PartitionName;
  llvm::StringRef HeaderName;
  bool IsExported;
  bool IsHeaderImport;
  // ...
};

// ExportDecl (include/blocktype/AST/Decl.h:1375-1391)
class ExportDecl : public Decl {
  Decl *ExportedDecl;
  // ...
};
```

#### 符号表（已完成）

```cpp
// include/blocktype/Sema/SymbolTable.h
class SymbolTable {
  llvm::StringMap<llvm::SmallVector<NamedDecl *, 4>> OrdinarySymbols;
  llvm::StringMap<TagDecl *> Tags;
  llvm::StringMap<TypedefNameDecl *> Typedefs;
  llvm::StringMap<NamespaceDecl *> Namespaces;
  llvm::StringMap<TemplateDecl *> Templates;
  llvm::StringMap<ConceptDecl *> Concepts;
  // ...
};
```

#### 作用域（已完成）

```cpp
// include/blocktype/Sema/Scope.h
class Scope {
  Scope *Parent;
  ScopeFlags Flags;
  llvm::StringMap<NamedDecl *> Declarations;
  // ...
};
```

### 需要新增的基础设施

#### 1. ModuleManager（阶段2.1）

```cpp
// 新增文件
include/blocktype/Module/ModuleManager.h
src/Module/ModuleManager.cpp
```

#### 2. BMI 格式（阶段2.2）

```cpp
// 新增文件
include/blocktype/Module/BMIFormat.h
include/blocktype/Module/BMIReader.h
include/blocktype/Module/BMIWriter.h
src/Module/BMIReader.cpp
src/Module/BMIWriter.cpp
```

#### 3. 依赖图（阶段2.3）

```cpp
// 新增文件
include/blocktype/Module/ModuleDependencyGraph.h
src/Module/ModuleDependencyGraph.cpp
```

#### 4. 可见性检查（阶段3.2）

```cpp
// 新增文件
include/blocktype/Sema/ModuleVisibility.h
src/Sema/ModuleVisibility.cpp
```

---

## 测试策略

### 单元测试

| 阶段 | 测试文件 | 测试内容 |
|------|----------|----------|
| 阶段2.1 | `ModuleManagerTest.cpp` | 模块加载、缓存、查询 |
| 阶段2.2 | `BMIFormatTest.cpp` | BMI读写、序列化 |
| 阶段2.3 | `DependencyGraphTest.cpp` | 依赖图构建、循环检测 |
| 阶段3.1 | `ModuleLinkerTest.cpp` | 链接器集成 |
| 阶段3.2 | `ModuleVisibilityTest.cpp` | 可见性规则 |
| 阶段3.3 | `CrossModuleResolutionTest.cpp` | 跨模块符号解析 |

### 集成测试

```cpp
// tests/integration/ModuleIntegrationTest.cpp

TEST_F(ModuleIntegrationTest, SimpleModule) {
  // MyModule.cppm
  const char *Code = R"(
    export module MyModule;
    export int add(int a, int b) { return a + b; }
  )";
  
  // 编译模块
  ASSERT_TRUE(compileModule("MyModule.cppm", Code));
  
  // 使用模块
  const char *UserCode = R"(
    import MyModule;
    int main() { return add(1, 2); }
  )";
  
  ASSERT_TRUE(compileAndRun(UserCode));
}

TEST_F(ModuleIntegrationTest, ModuleWithPartition) {
  // 测试模块分区
  // ...
}

TEST_F(ModuleIntegrationTest, CyclicDependency) {
  // 测试循环依赖检测
  // ...
}
```

### LIT 测试

```llvm
; tests/lit/modules/simple-module.test

; RUN: %blocktype -c %s -o %t.bmi
; RUN: %blocktype -c %S/Inputs/user.cpp -fmodule-file=%t.bmi -o %t.o
; RUN: %linker %t.o -o %t
; RUN: %t | FileCheck %s

export module Simple;
export int getValue() { return 42; }

; CHECK: 42
```

---

## 风险评估

### 技术风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| BMI格式设计不当 | 高 | 中 | 参考Clang PCM格式，迭代优化 |
| 循环依赖处理复杂 | 中 | 高 | 早期检测，明确错误提示 |
| 跨模块类型不一致 | 高 | 中 | 严格类型检查，BMI包含完整类型信息 |
| 链接器兼容性 | 中 | 中 | 提供多种链接策略，渐进集成 |
| 性能问题 | 中 | 低 | 延迟加载，缓存优化 |

### 实现风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| AST序列化复杂度高 | 高 | 高 | 阶段2采用简化方案，阶段3评估完整方案 |
| 与现有代码冲突 | 中 | 中 | 充分测试，渐进集成 |
| 测试覆盖不足 | 中 | 中 | 编写全面的单元测试和集成测试 |

---

## 时间估算

### 阶段2（核心基础设施）

| 任务 | 预估时间 | 依赖 |
|------|----------|------|
| 2.1 ModuleManager接口设计 | 3天 | - |
| 2.2 BMI文件格式实现 | 5天 | 2.1 |
| 2.3 模块依赖图构建 | 3天 | 2.1 |
| 2.4 模块编译流程集成 | 2天 | 2.2, 2.3 |
| **阶段2总计** | **13天** | - |

### 阶段3（高级集成）

| 任务 | 预估时间 | 依赖 |
|------|----------|------|
| 3.1 链接器集成 | 4天 | 2.2, 2.4 |
| 3.2 模块可见性规则 | 3天 | 2.1, 2.3 |
| 3.3 跨模块符号解析 | 4天 | 3.2 |
| 3.4 模块分区支持 | 3天 | 3.3 |
| 3.5 全局模块片段 | 2天 | 3.3 |
| **阶段3总计** | **16天** | - |

### 总时间估算

- **阶段2**: 13个工作日（约2.5周）
- **阶段3**: 16个工作日（约3周）
- **总计**: 29个工作日（约5.5周）

### 里程碑

| 里程碑 | 时间 | 交付物 |
|--------|------|--------|
| M1: 基础设施完成 | 第2.5周 | ModuleManager, BMI, 依赖图 |
| M2: 核心功能完成 | 第5.5周 | 可见性, 跨模块解析, 分区支持 |
| M3: 测试与优化 | 第6周 | 完整测试套件, 性能优化 |

---

## 附录

### A. 参考资料

1. **C++20 标准** - [P1103R3](https://wg21.link/P1103R3) Merging Modules
2. **Clang 实现** - Clang PCM (Precompiled Module) 格式
3. **GCC 实现** - GCC CMI (Compiled Module Interface) 格式
4. **MSVC 实现** - MSVC IFC (Interface File) 格式

### B. 术语表

| 术语 | 说明 |
|------|------|
| BMI | Binary Module Interface - 二进制模块接口文件 |
| Module Partition | 模块分区 - 模块的子单元 |
| Global Module Fragment | 全局模块片段 - module; 声明后的区域 |
| Private Module Fragment | 私有模块片段 - module :private; 声明后的区域 |
| Exported Symbol | 导出符号 - 被标记为 export 的符号 |
| Visible Symbol | 可见符号 - 在当前模块可访问的符号 |

### C. 文件组织

```
include/blocktype/Module/
├── ModuleManager.h
├── BMIFormat.h
├── BMIReader.h
├── BMIWriter.h
└── ModuleDependencyGraph.h

src/Module/
├── CMakeLists.txt
├── ModuleManager.cpp
├── BMIReader.cpp
├── BMIWriter.cpp
└── ModuleDependencyGraph.cpp

tests/unit/Module/
├── CMakeLists.txt
├── ModuleManagerTest.cpp
├── BMIFormatTest.cpp
└── DependencyGraphTest.cpp
```

---

**文档维护者**: BlockType 开发团队  
**最后更新**: 2026-04-21
