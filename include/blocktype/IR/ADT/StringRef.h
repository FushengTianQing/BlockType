#ifndef BLOCKTYPE_IR_ADT_STRINGREF_H
#define BLOCKTYPE_IR_ADT_STRINGREF_H

#include <cstddef>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>

namespace blocktype {
namespace ir {

class StringRef {
  const char* Data = nullptr;
  size_t Len = 0;

public:
  StringRef() = default;
  StringRef(const char* Str) : Data(Str), Len(Str ? std::strlen(Str) : 0) {}
  StringRef(const char* Str, size_t L) : Data(Str), Len(L) {}
  StringRef(const std::string& Str) : Data(Str.data()), Len(Str.size()) {}
  StringRef(std::string_view Sv) : Data(Sv.data()), Len(Sv.size()) {}

  const char* data() const { return Data; }
  size_t size() const { return Len; }
  bool empty() const { return Len == 0; }

  char operator[](size_t i) const { return Data[i]; }
  char front() const { return Data[0]; }
  char back() const { return Data[Len - 1]; }

  const char* begin() const { return Data; }
  const char* end() const { return Data + Len; }

  std::string str() const { return std::string(Data, Len); }
  operator std::string_view() const { return std::string_view(Data, Len); }

  StringRef slice(size_t Start, size_t Length) const {
    return StringRef(Data + Start, Length);
  }

  StringRef slice(size_t Start) const {
    return StringRef(Data + Start, Len - Start);
  }

  bool startswith(StringRef Prefix) const {
    return Len >= Prefix.Len && std::memcmp(Data, Prefix.Data, Prefix.Len) == 0;
  }

  bool endswith(StringRef Suffix) const {
    return Len >= Suffix.Len &&
           std::memcmp(Data + Len - Suffix.Len, Suffix.Data, Suffix.Len) == 0;
  }

  bool contains(char C) const {
    for (size_t i = 0; i < Len; ++i)
      if (Data[i] == C) return true;
    return false;
  }

  bool contains(StringRef Other) const {
    if (Other.Len > Len) return false;
    for (size_t i = 0; i <= Len - Other.Len; ++i)
      if (std::memcmp(Data + i, Other.Data, Other.Len) == 0) return true;
    return false;
  }

  size_t find(char C, size_t From = 0) const {
    for (size_t i = From; i < Len; ++i)
      if (Data[i] == C) return i;
    return npos;
  }

  size_t rfind(char C) const {
    for (size_t i = Len; i > 0; --i)
      if (Data[i - 1] == C) return i - 1;
    return npos;
  }

  StringRef trim() const {
    size_t Start = 0;
    while (Start < Len && (Data[Start] == ' ' || Data[Start] == '\t' ||
                           Data[Start] == '\n' || Data[Start] == '\r'))
      ++Start;
    size_t End = Len;
    while (End > Start && (Data[End - 1] == ' ' || Data[End - 1] == '\t' ||
                           Data[End - 1] == '\n' || Data[End - 1] == '\r'))
      --End;
    return StringRef(Data + Start, End - Start);
  }

  bool operator==(StringRef Other) const {
    if (Len != Other.Len) return false;
    return std::memcmp(Data, Other.Data, Len) == 0;
  }

  bool operator!=(StringRef Other) const { return !(*this == Other); }

  bool operator<(StringRef Other) const {
    size_t MinLen = Len < Other.Len ? Len : Other.Len;
    int Cmp = std::memcmp(Data, Other.Data, MinLen);
    if (Cmp != 0) return Cmp < 0;
    return Len < Other.Len;
  }

  bool operator<=(StringRef Other) const { return !(Other < *this); }
  bool operator>(StringRef Other) const { return Other < *this; }
  bool operator>=(StringRef Other) const { return !(*this < Other); }

  static constexpr size_t npos = static_cast<size_t>(-1);
};

inline std::ostream& operator<<(std::ostream& OS, StringRef S) {
  OS.write(S.data(), S.size());
  return OS;
}

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_STRINGREF_H
