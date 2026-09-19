// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/mojom/buffer_types_mojom_traits.h"

#include "build/blink_buildflags.h"
#include "build/build_config.h"
#include "ui/gfx/mojom/native_handle_types.mojom-shared.h"

namespace mojo {

// 刀16: USE_BLINK 门摘除 — gfx.mojom typemap 生成代码非 blink 也引用.
gfx::GpuMemoryBufferHandle& StructTraits<
    gfx::mojom::GpuMemoryBufferHandleDataView,
    gfx::GpuMemoryBufferHandle>::platform_handle(gfx::GpuMemoryBufferHandle&
                                                     handle) {
  return handle;
}

bool StructTraits<gfx::mojom::GpuMemoryBufferHandleDataView,
                  gfx::GpuMemoryBufferHandle>::
    Read(gfx::mojom::GpuMemoryBufferHandleDataView data,
         gfx::GpuMemoryBufferHandle* out) {
  out->offset = data.offset();
  out->stride = data.stride();

  return data.ReadPlatformHandle(out);
}

}  // namespace mojo
