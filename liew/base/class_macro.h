#pragma once

#define LIEW_DISABLE_COPY(TypeName)     \
    TypeName(const TypeName&) = delete; \
    TypeName& operator=(const TypeName&) = delete

#define LIEW_DISABLE_COPY_MOVE(TypeName)           \
    TypeName(const TypeName&) = delete;            \
    TypeName& operator=(const TypeName&) = delete; \
    TypeName(TypeName&&) = delete;                 \
    TypeName& operator=(TypeName&&) = delete
