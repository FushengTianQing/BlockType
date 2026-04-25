#ifndef BLOCKTYPE_IR_ADT_RAW_OSTREAM_H
#define BLOCKTYPE_IR_ADT_RAW_OSTREAM_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace blocktype {
namespace ir {

class raw_ostream {
public:
  enum class Colors : uint8_t {
    BLACK = 0,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    SAVEDCOLOR,
  };

private:
  virtual void write_impl(const char* Ptr, size_t Size) = 0;

protected:
  size_t Pos = 0;

public:
  raw_ostream() = default;
  virtual ~raw_ostream() = default;

  raw_ostream(const raw_ostream&) = delete;
  raw_ostream& operator=(const raw_ostream&) = delete;

  raw_ostream& write(const char* Ptr, size_t Size) {
    write_impl(Ptr, Size);
    Pos += Size;
    return *this;
  }

  raw_ostream& write_escaped(std::string_view Str, bool UseHexEscapes = false);

  raw_ostream& operator<<(char C) {
    write_impl(&C, 1);
    ++Pos;
    return *this;
  }

  raw_ostream& operator<<(unsigned char C) {
    write_impl(reinterpret_cast<const char*>(&C), 1);
    ++Pos;
    return *this;
  }

  raw_ostream& operator<<(std::string_view Str) {
    write_impl(Str.data(), Str.size());
    Pos += Str.size();
    return *this;
  }

  raw_ostream& operator<<(const char* Str) {
    size_t Len = std::char_traits<char>::length(Str);
    write_impl(Str, Len);
    Pos += Len;
    return *this;
  }

  raw_ostream& operator<<(const std::string& Str) {
    write_impl(Str.data(), Str.size());
    Pos += Str.size();
    return *this;
  }

  raw_ostream& operator<<(int Val) { return write_integer(Val); }
  raw_ostream& operator<<(unsigned Val) { return write_unsigned(Val); }
  raw_ostream& operator<<(long Val) { return write_integer(Val); }
  raw_ostream& operator<<(unsigned long Val) { return write_unsigned(Val); }
  raw_ostream& operator<<(long long Val) { return write_integer(Val); }
  raw_ostream& operator<<(unsigned long long Val) { return write_unsigned(Val); }

  raw_ostream& operator<<(bool Val) {
    *this << (Val ? "true" : "false");
    return *this;
  }

  raw_ostream& operator<<(float Val);
  raw_ostream& operator<<(double Val);

  raw_ostream& operator<<(const void* Ptr);

  virtual void flush() {}

  virtual size_t tell() const { return Pos; }

  virtual bool is_displayed() const { return false; }

  virtual bool has_colors() const { return false; }

  virtual void changeColor(Colors, bool = false, bool = false) {}
  virtual void resetColor() {}

private:
  raw_ostream& write_integer(long long Val) {
    char Buf[32];
    int Len = snprintf(Buf, sizeof(Buf), "%lld", Val);
    write_impl(Buf, static_cast<size_t>(Len));
    Pos += static_cast<size_t>(Len);
    return *this;
  }

  raw_ostream& write_unsigned(unsigned long long Val) {
    char Buf[32];
    int Len = snprintf(Buf, sizeof(Buf), "%llu", Val);
    write_impl(Buf, static_cast<size_t>(Len));
    Pos += static_cast<size_t>(Len);
    return *this;
  }
};

class raw_fd_ostream : public raw_ostream {
  int FD;
  bool ShouldClose;

  void write_impl(const char* Ptr, size_t Size) override;

public:
  explicit raw_fd_ostream(const char* Filename, std::error_code& EC);
  explicit raw_fd_ostream(int fd, bool shouldClose);
  ~raw_fd_ostream() override;

  void flush() override;
};

class raw_string_ostream : public raw_ostream {
  std::string& OS;

  void write_impl(const char* Ptr, size_t Size) override {
    OS.append(Ptr, Size);
  }

public:
  explicit raw_string_ostream(std::string& O) : OS(O) {}

  std::string& str() { return OS; }
  void flush() override {}
};

class raw_os_ostream : public raw_ostream {
  std::ostream& OS;

  void write_impl(const char* Ptr, size_t Size) override {
    OS.write(Ptr, static_cast<std::streamsize>(Size));
  }

public:
  explicit raw_os_ostream(std::ostream& O) : OS(O) {}
  void flush() override { OS.flush(); }
};

extern raw_ostream& outs();
extern raw_ostream& errs();

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_ADT_RAW_OSTREAM_H
