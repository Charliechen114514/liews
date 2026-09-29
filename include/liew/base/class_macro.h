#pragma once

#define LIEW_DISABLE_COPY(TypeName)     \
    TypeName(const TypeName&) = delete; \
    TypeName& operator=(const TypeName&) = delete

#define LIEW_DISABLE_COPY_MOVE(TypeName)           \
    TypeName(const TypeName&) = delete;            \
    TypeName& operator=(const TypeName&) = delete; \
    TypeName(TypeName&&) = delete;                 \
    TypeName& operator=(TypeName&&) = delete

#define LIEW_PRIVATE_TYPE Impl

#define LIEW_PRIVATE_TYPE_NAME_DECLEAR(TypeName) \
    class Impl;                                  \
    Impl* TypeName##Impl_ = nullptr