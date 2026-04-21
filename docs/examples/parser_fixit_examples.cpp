// Parser Fix-It Hints 示例实现
// 
// 这个文件展示如何在Parser中添加Fix-It hints

#include "blocktype/Parse/Parser.h"
#include "blocktype/Basic/FixItHint.h"

namespace blocktype {

// 示例1: 缺少分号的Fix-It
void Parser::emitMissingSemicolonFixIt(SourceLocation Loc) {
  // 创建插入分号的Fix-It hint
  FixItHint Hint = FixItHint::CreateInsertion(Loc, ";");
  
  // 发出错误和Fix-It
  Diags.report(Loc, DiagID::err_expected_semi, {Hint});
  ++ErrorCount;
}

// 示例2: 括号不匹配的Fix-It
void Parser::emitMismatchedBracketFixIt(SourceLocation OpenLoc, 
                                         SourceLocation CloseLoc,
                                         char Expected) {
  // 创建替换括号的Fix-It hint
  std::string Replacement(1, Expected);
  FixItHint Hint = FixItHint::CreateReplacement(
    SourceRange(CloseLoc, CloseLoc.getLocWithOffset(1)), 
    Replacement
  );
  
  // 发出错误和Fix-It
  Diags.report(CloseLoc, DiagID::err_expected, ")", "}", {Hint});
  ++ErrorCount;
}

// 示例3: 拼写错误的Fix-It
void Parser::emitTypoCorrectionFixIt(SourceLocation Loc, 
                                      llvm::StringRef Typo,
                                      llvm::StringRef Correction) {
  // 创建替换的Fix-It hint
  SourceRange Range(Loc, Loc.getLocWithOffset(Typo.size()));
  FixItHint Hint = FixItHint::CreateReplacement(Range, Correction);
  
  // 发出错误和Fix-It
  std::string Msg = "use of undeclared identifier '" + Typo.str() + 
                    "'; did you mean '" + Correction.str() + "'?";
  Diags.report(Loc, DiagLevel::Error, Msg, {Hint});
  ++ErrorCount;
}

// 示例4: 缺少命名空间的Fix-It
void Parser::emitMissingNamespaceFixIt(SourceLocation Loc,
                                        llvm::StringRef Name) {
  // 创建插入命名空间的Fix-It hint
  std::string Insertion = "std::";
  FixItHint Hint = FixItHint::CreateInsertion(Loc, Insertion);
  
  // 发出错误和Fix-It
  std::string Msg = "use of undeclared identifier '" + Name.str() + 
                    "'; did you mean 'std::" + Name.str() + "'?";
  Diags.report(Loc, DiagLevel::Error, Msg, {Hint});
  ++ErrorCount;
}

// 示例5: 类型不匹配的Fix-It
void Parser::emitTypeMismatchFixIt(SourceLocation Loc,
                                    llvm::StringRef FromType,
                                    llvm::StringRef ToType) {
  // 创建插入显式转换的Fix-It hint
  std::string Cast = "static_cast<" + ToType.str() + ">(";
  FixItHint InsertOpen = FixItHint::CreateInsertion(Loc, Cast);
  
  // 需要找到表达式结束位置来插入闭合括号
  // 这里简化处理，实际需要解析表达式
  FixItHint InsertClose = FixItHint::CreateInsertion(
    Loc.getLocWithOffset(10), // 假设表达式长度为10
    ")"
  );
  
  // 发出警告和Fix-It
  std::string Msg = "implicit conversion from '" + FromType.str() + 
                    "' to '" + ToType.str() + "' changes value";
  Diags.report(Loc, DiagLevel::Warning, Msg, {InsertOpen, InsertClose});
  ++ErrorCount;
}

} // namespace blocktype

/*
使用示例：

1. 在Parser.cpp中修改错误处理：

// 原代码：
if (!Tok.is(TokenKind::semicolon)) {
  emitError(DiagID::err_expected_semi);
  return nullptr;
}

// 修改后：
if (!Tok.is(TokenKind::semicolon)) {
  emitMissingSemicolonFixIt(Tok.getLocation());
  return nullptr;
}

2. 括号匹配：

// 原代码：
if (!Tok.is(TokenKind::r_paren)) {
  emitError(DiagID::err_expected_rparen);
  return nullptr;
}

// 修改后：
if (!Tok.is(TokenKind::r_paren)) {
  if (Tok.is(TokenKind::r_brace)) {
    // 可能是括号类型错误
    emitMismatchedBracketFixIt(OpenParenLoc, Tok.getLocation(), ')');
  } else {
    emitError(DiagID::err_expected_rparen);
  }
  return nullptr;
}

3. 拼写纠正：

// 在Sema中检查未声明的标识符
if (!Actions.LookupIdentifier(Name)) {
  // 尝试拼写纠正
  llvm::StringRef Correction = Actions.CorrectTypo(Name);
  if (!Correction.empty()) {
    emitTypoCorrectionFixIt(Loc, Name, Correction);
  } else {
    // 检查是否需要命名空间
    if (Actions.IsInStd(Name)) {
      emitMissingNamespaceFixIt(Loc, Name);
    }
  }
}

预期输出示例：

test.cpp:5:2: error: expected ';'
int x
     ^
     ;
Fix-It hints:
  Insert ';' at line 5, column 6
    ;

test.cpp:10:15: error: use of undeclared identifier 'retrun'; did you mean 'return'?
  retrun 42;
  ^~~~~~
  return
Fix-It hints:
  Replace from line 10, column 3 to line 10, column 9 with 'return'
    return

test.cpp:15:3: error: use of undeclared identifier 'cout'; did you mean 'std::cout'?
  cout << "hello";
  ^~~~
  std::cout
Fix-It hints:
  Insert 'std::' at line 15, column 3
    std::cout

test.cpp:20:9: warning: implicit conversion from 'double' to 'int' changes value
int x = 3.14;
        ^~~~
        static_cast<int>(3.14)
Fix-It hints:
  Insert 'static_cast<int>(' at line 20, column 9
    static_cast<int>(3.14)
*/
