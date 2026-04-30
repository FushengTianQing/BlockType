//===--- CodeViewEmitterTest.cpp - CodeView Emitter Tests -*- C++ -*-===//

#include <gtest/gtest.h>
#include <string>
#include "blocktype/Backend/DebugInfoEmitter.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/ADT/raw_ostream.h"

using namespace blocktype;
using namespace blocktype::backend;
using namespace blocktype::ir;

TEST(CodeViewEmitterTest, EmitReturnsFalse) {
  // CodeView is not yet implemented, emit() should return false
  IRTypeContext Ctx;
  IRModule M("test", Ctx);
  CodeViewEmitter Emitter;
  std::string Output;
  raw_string_ostream OS(Output);
  EXPECT_FALSE(Emitter.emit(M, OS));
}

TEST(CodeViewEmitterTest, EmitCodeViewReturnsFalse) {
  IRTypeContext Ctx;
  IRModule M("test", Ctx);
  CodeViewEmitter Emitter;
  std::string Output;
  raw_string_ostream OS(Output);
  EXPECT_FALSE(Emitter.emitCodeView(M, OS));
}

TEST(CodeViewEmitterTest, EmitDWARF5ReturnsFalse) {
  IRTypeContext Ctx;
  IRModule M("test", Ctx);
  CodeViewEmitter Emitter;
  std::string Output;
  raw_string_ostream OS(Output);
  EXPECT_FALSE(Emitter.emitDWARF5(M, OS));
}

TEST(CodeViewEmitterTest, EmitDWARF4ReturnsFalse) {
  IRTypeContext Ctx;
  IRModule M("test", Ctx);
  CodeViewEmitter Emitter;
  std::string Output;
  raw_string_ostream OS(Output);
  EXPECT_FALSE(Emitter.emitDWARF4(M, OS));
}

TEST(CodeViewEmitterTest, InheritsDebugInfoEmitter) {
  // Verify CodeViewEmitter is a subclass of DebugInfoEmitter
  DebugInfoEmitter* Base = new CodeViewEmitter();
  EXPECT_NE(Base, nullptr);
  delete Base;
}
