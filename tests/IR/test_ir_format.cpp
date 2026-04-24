#include <cassert>
#include <cstdint>

#include "blocktype/IR/IRFormat.h"

using namespace blocktype::ir;

int main() {
  auto Cur = IRFormatVersion::Current();
  assert(Cur.Major == 1);
  assert(Cur.Minor == 0);
  assert(Cur.Patch == 0);

  IRFormatVersion V100 = {1, 0, 0};
  IRFormatVersion V200 = {2, 0, 0};
  IRFormatVersion V150 = {1, 5, 0};
  IRFormatVersion Reader100 = {1, 0, 0};

  assert(V100.isCompatibleWith(Reader100) == true);
  assert(V200.isCompatibleWith(Reader100) == false);
  assert(V150.isCompatibleWith(Reader100) == false);

  static_assert(sizeof(IRFileHeader) == 4 + 6 + 4 + 4 + 4 + 4, "IRFileHeader size mismatch");

  IRFileHeader H;
  assert(H.Magic[0] == 'B' && H.Magic[1] == 'T' && H.Magic[2] == 'I' && H.Magic[3] == 'R');
  assert(H.Version.Major == 1 && H.Version.Minor == 0 && H.Version.Patch == 0);
  assert(H.Flags == 0);
  assert(H.ModuleOffset == 0);
  assert(H.StringTableOffset == 0);
  assert(H.StringTableSize == 0);

  assert(Cur.toString() == "1.0.0");

  return 0;
}
