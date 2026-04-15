#include <gtest/gtest.h>
#include "blocktype/Basic/SourceLocation.h"

using namespace blocktype;

TEST(SourceLocationTest, DefaultInvalid) {
  SourceLocation Loc;
  EXPECT_FALSE(Loc.isValid());
  EXPECT_TRUE(Loc.isInvalid());
}

TEST(SourceLocationTest, ValidLocation) {
  SourceLocation Loc(1);
  EXPECT_TRUE(Loc.isValid());
  EXPECT_EQ(Loc.getRawEncoding(), 1u);
}

TEST(SourceLocationTest, Comparison) {
  SourceLocation Loc1(1);
  SourceLocation Loc2(2);
  SourceLocation Loc3(1);
  
  EXPECT_TRUE(Loc1 == Loc3);
  EXPECT_TRUE(Loc1 != Loc2);
  EXPECT_TRUE(Loc1 < Loc2);
}