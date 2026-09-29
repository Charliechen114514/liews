#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// Consumer TUs compile against the tree's libc++ headers but link only
// liew.lib, while libc++'s static runtime lives inside liew.dll. This
// definition satisfies the assert-support symbol referenced by consumer
// code. It is exported from liew.dll via linker /EXPORT entries in
// src/CMakeLists.txt (libc++-provided symbols like bad_function_call are
// exported the same way, straight from the closure).
namespace std {
inline namespace __Cr {

[[noreturn]] void __libcpp_verbose_abort(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::abort();
}

}  // namespace __Cr
}  // namespace std
