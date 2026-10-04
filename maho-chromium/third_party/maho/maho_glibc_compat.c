// Copyright 2026 Maho Browser. All rights reserved.
//
// glibc symbol-version compatibility shim (Linux only).
//
// The prebuilt maho FFI static libs (libmaho_ffi.a / libmaho_mail_ffi.a) bundle
// vendored C dependencies (OpenSSL / aws-lc / SQLCipher) that are compiled on
// the build host against glibc >= 2.38. Since glibc 2.38 the C library emits
// C23-aware symbol versions (`__isoc23_strtol`, `__isoc23_sscanf`, ...) for the
// integer-parsing / scanf family, and 64-bit `fcntl64`. Chromium, however,
// links against the Debian Bullseye sysroot (glibc 2.31), which does not export
// those symbols, so the final link fails with "undefined symbol: __isoc23_*".
//
// These `__isoc23_*` variants differ from the classic functions only in that
// they accept C23 binary (`0b`) integer literals; for Maho's usage forwarding
// to the classic implementations (which exist in the 2.31 sysroot) is correct.
// The definitions are weak so that, if a newer sysroot ever provides the real
// symbols, those take precedence and this shim becomes inert.

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define MAHO_WEAK __attribute__((weak))

MAHO_WEAK long __isoc23_strtol(const char* s, char** e, int base) {
  return strtol(s, e, base);
}
MAHO_WEAK unsigned long __isoc23_strtoul(const char* s, char** e, int base) {
  return strtoul(s, e, base);
}
MAHO_WEAK long long __isoc23_strtoll(const char* s, char** e, int base) {
  return strtoll(s, e, base);
}
MAHO_WEAK unsigned long long __isoc23_strtoull(const char* s, char** e,
                                               int base) {
  return strtoull(s, e, base);
}
MAHO_WEAK intmax_t __isoc23_strtoimax(const char* s, char** e, int base) {
  return strtoimax(s, e, base);
}
MAHO_WEAK uintmax_t __isoc23_strtoumax(const char* s, char** e, int base) {
  return strtoumax(s, e, base);
}

MAHO_WEAK int __isoc23_vsscanf(const char* s, const char* fmt, va_list ap) {
  return vsscanf(s, fmt, ap);
}
MAHO_WEAK int __isoc23_sscanf(const char* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsscanf(s, fmt, ap);
  va_end(ap);
  return r;
}
MAHO_WEAK int __isoc23_vfscanf(FILE* f, const char* fmt, va_list ap) {
  return vfscanf(f, fmt, ap);
}
MAHO_WEAK int __isoc23_fscanf(FILE* f, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfscanf(f, fmt, ap);
  va_end(ap);
  return r;
}
MAHO_WEAK int __isoc23_vscanf(const char* fmt, va_list ap) {
  return vscanf(fmt, ap);
}
MAHO_WEAK int __isoc23_scanf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vscanf(fmt, ap);
  va_end(ap);
  return r;
}

// On 64-bit Linux fcntl64 is ABI-identical to fcntl; the vendored SQLite build
// references the explicit 64-bit name.
MAHO_WEAK int fcntl64(int fd, int cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  void* arg = va_arg(ap, void*);
  va_end(ap);
  return fcntl(fd, cmd, arg);
}
