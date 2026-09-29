#pragma once
// PlainMemAllocator — 简单可用的 heap discardable 分配器。
// 策略:总预算 + LRU 淘汰未锁定块。"丢弃" = 释放堆内存;
// 被丢过的块再 Lock() 返回 false,调用方据此重建内容(如重新解码)。

#include <cstddef>
#include <memory>

#include "base/memory/discardable_memory.h"
#include "base/memory/discardable_memory_allocator.h"

#include "liew/base/class_macro.h"

namespace liew {

class PlainMemAllocator final : public base::DiscardableMemoryAllocator {
  public:
    // budget_bytes: 所有存活块的总字节上限。单块超过预算也照常分配
    // (清空 LRU 仍超支时不硬失败,下一轮淘汰补回)。
    explicit PlainMemAllocator(std::size_t budget_bytes);
    ~PlainMemAllocator() override;

    LIEW_DISABLE_COPY_MOVE(PlainMemAllocator);

    // base::DiscardableMemoryAllocator:
    std::unique_ptr<base::DiscardableMemory> AllocateLockedDiscardableMemory(
        std::size_t size) override;
    std::size_t GetBytesAllocated() const override;
    void ReleaseFreeMemory() override;

  private:
    // 账本:块与管理员共享的状态(锁/预算/LRU 名单/记账),细节在 .cc。
    // 生命周期前提:allocator 是进程级单例(上游 SetInstance 语义),
    // 已发出去的块不会活过它。
    struct IdlePool;
    class Block;

    std::unique_ptr<IdlePool> pool_;
};

}  // namespace liew
