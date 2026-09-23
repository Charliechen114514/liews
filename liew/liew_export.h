#pragma once

#if defined(_WIN32)
#    if defined(LIEW_IMPLEMENTATION)
#        define LIEW_API __declspec(dllexport)
#    else
#        define LIEW_API __declspec(dllimport)
#    endif
#else
#    define LIEW_API __attribute__((visibility("default")))
#endif
