# Scope 统一重构方案

## 1. 现状问题

当前 Parser 和 Sema **各自维护独立的 `CurrentScope`**，形成双重 Scope 系统：

```
Parser::CurrentScope ←→ Sema::CurrentScope   （两条独立的 Scope 链，互不通信）
```

| 问题 | 详情 |
|------|------|
| 双重 Scope 链 | `parseTranslationUnit` 中 Parser 创建 TU Scope（line 35），Sema 构造函数也创建一个（line 33） |
| 双重声明注册 | Parser 调 `CurrentScope->addDecl()`（8处），Sema 调 `Symbols.addDecl()`（17处），同一声明注册到两处 |
| Parser 绕过 Sema 做 lookup | Parser 中有 17 处 `CurrentScope->lookup()` 调用 |
| Parser 不通知 Sema 进出 scope | 类体/模板的 `pushScope/popScope` 只影响 Parser 的 Scope |
| Sema Scope 数据不完整 | Sema 的 ActOn 方法只注册到 `Symbols`（全局表），不注册到 `CurrentScope`（词法链） |

## 2. 影响范围统计

### Parser 侧 — `CurrentScope->lookup()` 调用（17 处）

| 文件 | 调用数 | 行号 | 用途 |
|------|--------|------|------|
| `ParseExpr.cpp` | 7 | 928, 945, 988, 1005, 1030, 1056, 1069 | 标识符/模板名/限定名查找 |
| `ParseType.cpp` | 2 | 109, 167 | 类型名查找（模板/类型声明） |
| `ParseStmt.cpp` | 1 | 72 | 声明语句判定 |
| `ParseTemplate.cpp` | 7 | 88, 108, 132, 211, 245, 642, 845 | 模板主模板查找/Concept 查找 |

### Parser 侧 — `CurrentScope->addDecl()` 调用（8 处）

| 文件 | 行号 | 注册变量 | 语义 |
|------|------|----------|------|
| `Parser.cpp` | 50 | `ND` | 顶层声明注册 |
| `ParseClass.cpp` | 80 | `Class` | class 声明 |
| `ParseClass.cpp` | 158 | `Struct` | struct 声明 |
| `ParseClass.cpp` | 209 | `Union` | union 声明 |
| `ParseClass.cpp` | 622 | `Method` | 成员方法 |
| `ParseClass.cpp` | 654 | `VD` | 静态成员变量 |
| `ParseClass.cpp` | 827 | `Ctor` | 构造函数 |
| `ParseClass.cpp` | 860 | `Dtor` | 析构函数 |

### Parser 侧 — `addDeclAllowRedeclaration()` 调用（1 处）

| 文件 | 行号 | 注册变量 | 语义 |
|------|------|----------|------|
| `ParseTemplate.cpp` | 171 | `P`（模板参数） | 模板参数允许重声明 |

### Sema 侧 — `Symbols.addDecl()` 调用（17 处）

| 行号 | 变量 | 声明类型 | 所在函数 |
|------|------|---------|---------|
| 119 | `VD` | VarDecl | `ActOnVarDecl` |
| 133 | `FD` | FunctionDecl | `ActOnFunctionDecl` |
| 183 | `ECD` | EnumConstantDecl | `ActOnEnumConstantDecl` |
| 211 | `TAD` | TypeAliasDecl | `ActOnTypeAliasDecl` |
| 234 | `NS` | NamespaceDecl | `ActOnNamespaceDecl` |
| 263 | `NAD` | NamespaceAliasDecl | `ActOnNamespaceAliasDecl` |
| 292 | `ED` | EnumDecl | `ActOnEnumDecl` |
| 300 | `TD` | TypedefDecl | `ActOnTypedefDecl` |
| 354 | `VD` | VarDecl | `ActOnVarDeclFull` |
| 367 | `FD` | FunctionDecl | `ActOnFunctionDeclFull` |
| 378 | `RD` | RecordDecl | `ActOnCXXRecordDecl` |
| 384 | `MD` | CXXMethodDecl | `ActOnCXXMethodDecl` |
| 389 | `FD` | FieldDecl | `ActOnFieldDecl` |
| 398 | `CD` | CXXConstructorDecl | `ActOnCXXConstructorDecl` |
| 403 | `DD` | CXXDestructorDecl | `ActOnCXXDestructorDecl` |
| 605 | `FD` | FieldDecl | `ActOnFieldDeclFactory` |
| 1382 | `LD` | LabelDecl | `ActOnLabelStmt` |

### Scope 生命周期管理

| 位置 | 操作 | 说明 |
|------|------|------|
| `Parser.cpp:35` | `pushScope(TU)` | Parser 创建 TU scope（Sema 构造函数已创建一个，重复） |
| `Parser.cpp:62` | `popScope()` | Parser 弹出 TU scope |
| `Parser.cpp:100` | `pushScope(ClassScope)` | class 体解析 |
| `Parser.cpp:102` | `popScope()` | class 体结束 |
| `Sema.cpp:33` | `PushScope(TU)` | Sema 构造函数创建 TU scope |
| `Sema.cpp:143` | `PushScope(FunctionBody)` | 函数体开始 |
| `Sema.cpp:156` | `PopScope()` | 函数体结束 |

## 3. 重构策略

采用 5 阶段增量迁移：先让 Sema 的 Scope 链具备完整数据（Phase 1），再将 Parser 的查找/注册逐步重定向到 Sema（Phase 2-3），然后统一 Scope 生命周期（Phase 4），最后清理 Parser::CurrentScope（Phase 5）。

每个阶段独立编译测试，确保 662 个测试全部通过后才进入下一阶段。

## 4. 分阶段实施

### Phase 1: Sema 填充 CurrentScope（registerDecl）

**目标**：Sema 的 `CurrentScope->lookup()` 能找到所有声明。

**方案**：添加辅助方法 `registerDecl()`，同时注册到 Scope 链和 SymbolTable：

```cpp
// Sema.h 新增
private:
  void registerDecl(NamedDecl *ND) {
    if (CurrentScope) CurrentScope->addDecl(ND);
    Symbols.addDecl(ND);
  }
  void registerDeclAllowRedecl(NamedDecl *ND) {
    if (CurrentScope) CurrentScope->addDeclAllowRedeclaration(ND);
    Symbols.addDecl(ND);
  }
```

**修改 Sema.cpp 中 17 处** `if (CurrentScope) Symbols.addDecl(X)` → `registerDecl(X)`。

**注意**：`Scope::addDecl()` 在同名冲突时返回 false 且不添加，模板参数需要使用 `registerDeclAllowRedecl()`。

**验证**：编译 + 662 测试。

### Phase 2: Sema::LookupName + 替换 Parser 查找

**目标**：Parser 所有名称查找通过 Sema 接口。

**方案**：

1. 新增 `Sema::LookupName()` 方法：

```cpp
// Sema.h
NamedDecl *LookupName(llvm::StringRef Name) const;

// Sema.cpp
NamedDecl *Sema::LookupName(llvm::StringRef Name) const {
  // 1. 先查 Scope 链（递归查父作用域）
  if (CurrentScope) {
    if (NamedDecl *D = CurrentScope->lookup(Name))
      return D;
  }
  // 2. 回退到 SymbolTable（覆盖前向声明等边缘情况）
  auto Decls = Symbols.lookup(Name);
  return Decls.empty() ? nullptr : Decls.front();
}
```

2. 替换 Parser 中 17 处 `CurrentScope->lookup(X)` → `Actions.LookupName(X)`：

| 文件 | 原调用 | 替换 |
|------|--------|------|
| ParseExpr.cpp:928 | `CurrentScope->lookup(Name)` | `Actions.LookupName(Name)` |
| ParseExpr.cpp:945 | `CurrentScope->lookup(Name)` | `Actions.LookupName(Name)` |
| ParseExpr.cpp:988 | `CurrentScope->lookup(FullName)` | `Actions.LookupName(FullName)` |
| ParseExpr.cpp:1005 | `CurrentScope->lookup(FullName)` | `Actions.LookupName(FullName)` |
| ParseExpr.cpp:1030 | `CurrentScope->lookup(TemplateName)` | `Actions.LookupName(TemplateName)` |
| ParseExpr.cpp:1056 | `CurrentScope->lookup(TemplateName)` | `Actions.LookupName(TemplateName)` |
| ParseExpr.cpp:1069 | `CurrentScope->lookup(TemplateName)` | `Actions.LookupName(TemplateName)` |
| ParseType.cpp:109 | `CurrentScope->lookup(TypeName)` | `Actions.LookupName(TypeName)` |
| ParseType.cpp:167 | `CurrentScope->lookup(TypeName)` | `Actions.LookupName(TypeName)` |
| ParseStmt.cpp:72 | `CurrentScope->lookup(Tok.getText())` | `Actions.LookupName(Tok.getText())` |
| ParseTemplate.cpp:88 | `CurrentScope->lookup(...)` | `Actions.LookupName(...)` |
| ParseTemplate.cpp:108 | `CurrentScope->lookup(...)` | `Actions.LookupName(...)` |
| ParseTemplate.cpp:132 | `CurrentScope->lookup(...)` | `Actions.LookupName(...)` |
| ParseTemplate.cpp:211 | `CurrentScope->lookup(...)` | `Actions.LookupName(...)` |
| ParseTemplate.cpp:245 | `CurrentScope->lookup(...)` | `Actions.LookupName(...)` |
| ParseTemplate.cpp:642 | `CurrentScope->lookup(TemplateName)` | `Actions.LookupName(TemplateName)` |
| ParseTemplate.cpp:845 | `CurrentScope->lookup(ConceptName)` | `Actions.LookupName(ConceptName)` |

同时简化调用处的 `if (CurrentScope)` 守卫，因为 `Actions.LookupName()` 内部已处理空 scope。

**验证**：编译 + 662 测试。

### Phase 3: 移除 Parser addDecl 调用

**目标**：Parser 不再直接向 Scope 注册声明。

**方案**：删除 Parser 中 8 处 `CurrentScope->addDecl()` 和 1 处 `addDeclAllowRedeclaration()`：

| 文件 | 行号 | 删除的代码 |
|------|------|-----------|
| Parser.cpp | 49-51 | `if (CurrentScope) { CurrentScope->addDecl(ND); }` |
| ParseClass.cpp | 79-81 | `if (CurrentScope) { CurrentScope->addDecl(Class); }` |
| ParseClass.cpp | 157-159 | `if (CurrentScope) { CurrentScope->addDecl(Struct); }` |
| ParseClass.cpp | 208-210 | `if (CurrentScope) { CurrentScope->addDecl(Union); }` |
| ParseClass.cpp | 621-623 | `if (CurrentScope) { CurrentScope->addDecl(Method); }` |
| ParseClass.cpp | 653-655 | `if (CurrentScope) { CurrentScope->addDecl(VD); }` |
| ParseClass.cpp | 826-828 | `if (CurrentScope) { CurrentScope->addDecl(Ctor); }` |
| ParseClass.cpp | 859-861 | `if (CurrentScope) { CurrentScope->addDecl(Dtor); }` |
| ParseTemplate.cpp | 171 | `CurrentScope->addDeclAllowRedeclaration(P)` → `Actions.registerDeclAllowRedecl(P)` |

**注意**：
- `ParseClass.cpp:80/158/209` 的 class/struct/union addDecl 发生在解析类体**之前**，用于自引用。需要确保 Sema 的 ActOnCXXRecordDeclFactory 已在 Phase 1 中注册，或改用 `Actions.getCurrentScope()->addDecl()` 暂时保留。
- `ParseTemplate.cpp:171` 模板参数注册需要 Sema 暴露 `registerDeclAllowRedecl` 公有方法。

**验证**：编译 + 662 测试。

### Phase 4: 统一 Scope 生命周期

**目标**：Parser 的 pushScope/popScope 委托给 Sema，消除重复 TU scope。

**方案**：

1. 修改 Parser 的 `pushScope/popScope`：

```cpp
void Parser::pushScope(ScopeFlags Flags) {
  Actions.PushScope(Flags);
  CurrentScope = Actions.getCurrentScope(); // 同步
}
void Parser::popScope() {
  Actions.PopScope();
  CurrentScope = Actions.getCurrentScope(); // 同步
}
```

2. 修改 `parseTranslationUnit()`：
   - 移除 `pushScope(TU)` 和 `popScope()` 调用（Sema 构造函数已创建 TU scope）
   - 移除 `CurrentScope->addDecl(ND)` 注册（已在 Phase 3 移除）

3. 模板参数解析中的 `pushScope(TemplateScope)` / `popScope()`（ParseTemplate.cpp:169）同样委托给 Sema。

**验证**：编译 + 662 测试。

### Phase 5: 移除 Parser::CurrentScope

**目标**：Parser 完全通过 `Actions` 接口访问 Scope。

**方案**：

1. 删除 `Parser::CurrentScope` 成员（Parser.h:92）
2. 删除 `Parser::getCurrentScope()` 方法（Parser.h:243）
3. Parser.h 中 `pushScope/popScope` 改为纯委托（不维护 CurrentScope）
4. Parser.h 不再需要 `#include "blocktype/Sema/Scope.h"`（如果仍有间接依赖可通过 fwd decl 解决）

**验证**：编译 + 662 测试。

## 5. 文件修改清单

| 文件 | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Phase 5 |
|------|---------|---------|---------|---------|---------|
| `Sema.h` | 新增 `registerDecl`/`LookupName` 声明 | — | 暴露 `registerDeclAllowRedecl` | — | — |
| `Sema.cpp` | 17 处 `Symbols.addDecl` → `registerDecl` | 实现 `LookupName` | — | — | — |
| `Parser.h` | — | — | — | 修改 `pushScope/popScope` | 移除 `CurrentScope` |
| `Parser.cpp` | — | — | 删除 addDecl | 移除 TU push/pop | 简化 push/pop |
| `ParseExpr.cpp` | — | 7 处替换 | — | — | — |
| `ParseType.cpp` | — | 2 处替换 | — | — | — |
| `ParseStmt.cpp` | — | 1 处替换 | — | — | — |
| `ParseClass.cpp` | — | — | 7 处删除 | — | — |
| `ParseTemplate.cpp` | — | 7 处替换 | 1 处替换 | 委托 push/pop | — |

## 6. 风险与注意事项

- **Phase 3 关键**：`ParseClass.cpp:80/158/209` 的 class/struct/union addDecl 在解析类体**之前**执行，用于自引用（如 `class X { X* next; }`）。如果 Sema 的 `ActOnCXXRecordDeclFactory` 已经注册到 Scope（Phase 1），则可安全删除。否则需保留或改为调用 `Actions.registerDecl()`。
- **Phase 4 关键**：`parseTranslationUnit()` 不能再 push TU scope，因为 Sema 构造函数已创建。需直接使用 Sema 现有的 TU scope。
- **性能**：`Sema::LookupName()` 先查 Scope 链再回退 SymbolTable，与当前 Parser 行为一致，无性能退化。
- **爆炸半径**：每个 Phase 独立编译测试，任一阶段失败不进入下一阶段。
