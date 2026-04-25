#include "blocktype/IR/ADT/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace blocktype {
namespace ir {

raw_ostream& raw_ostream::write_escaped(std::string_view Str, bool UseHexEscapes) {
  for (char C : Str) {
    switch (C) {
    case '\\': *this << "\\\\"; break;
    case '\n': *this << "\\n"; break;
    case '\t': *this << "\\t"; break;
    case '\r': *this << "\\r"; break;
    case '\0': *this << "\\0"; break;
    default:
      if (static_cast<unsigned char>(C) < 0x20 && UseHexEscapes) {
        char Buf[8];
        snprintf(Buf, sizeof(Buf), "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(C)));
        *this << Buf;
      } else {
        *this << C;
      }
      break;
    }
  }
  return *this;
}

raw_ostream& raw_ostream::operator<<(float Val) {
  char Buf[64];
  snprintf(Buf, sizeof(Buf), "%g", static_cast<double>(Val));
  size_t Len = strlen(Buf);
  write_impl(Buf, Len);
  Pos += Len;
  return *this;
}

raw_ostream& raw_ostream::operator<<(double Val) {
  char Buf[64];
  snprintf(Buf, sizeof(Buf), "%g", Val);
  size_t Len = strlen(Buf);
  write_impl(Buf, Len);
  Pos += Len;
  return *this;
}

raw_ostream& raw_ostream::operator<<(const void* Ptr) {
  char Buf[32];
  snprintf(Buf, sizeof(Buf), "%p", Ptr);
  size_t Len = strlen(Buf);
  write_impl(Buf, Len);
  Pos += Len;
  return *this;
}

raw_fd_ostream::raw_fd_ostream(int fd, bool shouldClose)
    : FD(fd), ShouldClose(shouldClose) {}

raw_fd_ostream::~raw_fd_ostream() {
  flush();
  if (ShouldClose && FD >= 0) close(FD);
}

void raw_fd_ostream::write_impl(const char* Ptr, size_t Size) {
  if (FD >= 0) ::write(FD, Ptr, static_cast<unsigned>(Size));
}

void raw_fd_ostream::flush() {
}

raw_ostream& outs() {
  static raw_os_ostream S(std::cout);
  return S;
}

raw_ostream& errs() {
  static raw_os_ostream S(std::cerr);
  return S;
}

} // namespace ir
} // namespace blocktype
