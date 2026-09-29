#include "src/chrome_private/runtime/discardable_memory.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "base/check.h"
#include "base/containers/linked_list.h"
#include "base/synchronization/lock.h"

namespace liew {

// ---- 账本:块与管理员共享的全部状态 ----
struct PlainMemAllocator::IdlePool {
    explicit IdlePool(std::size_t budget_bytes) : budget(budget_bytes) {}

    // 名字即逻辑:
    void NoteAllocated(std::size_t bytes) { allocated += bytes; }
    void NoteFreed(std::size_t bytes) {
        DCHECK_GE(allocated, bytes);
        allocated -= bytes;
    }
    void Park(Block& block);       // 闲块入列(尾部 = 最近使用)
    void Unpark(Block& block);     // 摘出名单
    void EvictUntilUnderBudget();  // 从头部(最久未用)扔到装得下为止

    const std::size_t budget;
    mutable base::Lock lock;
    base::LinkedList<Block> idle;  // 只装 kIdle 态的块(头部=最久未用,尾部=最近使用)
    std::size_t allocated = 0;     // 所有存活块(锁定+闲)的总字节
};

// ---- 块:归调用方所有(unique_ptr),闲时挂进账本名单 ----
class PlainMemAllocator::Block final : public base::DiscardableMemory,
                                       public base::LinkNode<Block> {
  public:
    enum class State {
        kLocked,    // 使用中,不在名单里
        kIdle,      // 挂在名单里,随时可被扔
        kDiscarded, // buffer 已释放;再 Lock() 返回 false
    };

    Block(IdlePool& pool, std::size_t size)
        : pool_(pool), size_(size),
          buffer_(std::make_unique<std::uint8_t[]>(size)) {}

    ~Block() override {
        base::AutoLock lock(pool_.lock);
        if (state_ == State::kIdle) {
            pool_.Unpark(*this);
        }
        if (state_ != State::kDiscarded) {
            pool_.NoteFreed(size_);
        }
    }

    // base::DiscardableMemory:
    bool Lock() override {
        base::AutoLock lock(pool_.lock);
        if (state_ == State::kDiscarded) {
            return false;  // 契约核心:内容没了,调用方重建
        }
        if (state_ == State::kIdle) {
            pool_.Unpark(*this);
        }
        state_ = State::kLocked;
        return true;
    }

    void Unlock() override {
        base::AutoLock lock(pool_.lock);
        DCHECK_EQ(state_, State::kLocked);
        state_ = State::kIdle;
        pool_.Park(*this);
    }

    void* data() const override {
        DCHECK_EQ(state_, State::kLocked);  // 仅锁定期间有效(同旧实现)
        return buffer_.get();
    }

    void DiscardForTesting() override {
        base::AutoLock lock(pool_.lock);
        Discard();
    }

    base::trace_event::MemoryAllocatorDump* CreateMemoryAllocatorDump(
        const char*, base::trace_event::ProcessMemoryDump*) const override {
        return nullptr;  // 暂不接 trace(与旧实现一致)
    }

    // 须在 pool_.lock 内调用;使用中的块不许扔。
    void Discard() {
        DCHECK_NE(state_, State::kLocked);
        if (state_ == State::kDiscarded) {
            return;
        }
        if (state_ == State::kIdle) {
            pool_.Unpark(*this);
        }
        buffer_.reset();
        pool_.NoteFreed(size_);
        state_ = State::kDiscarded;
    }

  private:
    IdlePool& pool_;
    const std::size_t size_;
    std::unique_ptr<std::uint8_t[]> buffer_;
    State state_ = State::kLocked;  // 分配即锁定
};

// ---- 账本的方法(类外定义:需要 Block 完整类型) ----

void PlainMemAllocator::IdlePool::Park(Block& block) {
    idle.Append(&block);  // 尾部 = 最近使用
}

void PlainMemAllocator::IdlePool::Unpark(Block& block) {
    block.RemoveFromList();
}

void PlainMemAllocator::IdlePool::EvictUntilUnderBudget() {
    while (allocated > budget && !idle.empty()) {
        idle.head()->value()->Discard();  // 头部 = 最久未用
    }
}

// ---- PlainMemAllocator ----

PlainMemAllocator::PlainMemAllocator(std::size_t budget_bytes)
    : pool_(std::make_unique<IdlePool>(budget_bytes)) {}

PlainMemAllocator::~PlainMemAllocator() {
    // 块与账本共享状态;正常 teardown 中已发块都已析构。
    // 名单里还有人 → "块不会活过管理员"的前提被破坏,当场炸醒后人。
    base::AutoLock lock(pool_->lock);
    DCHECK(pool_->idle.empty());
}

std::unique_ptr<base::DiscardableMemory>
PlainMemAllocator::AllocateLockedDiscardableMemory(std::size_t size) {
    auto block = std::make_unique<Block>(*pool_, size);
    base::AutoLock lock(pool_->lock);
    pool_->NoteAllocated(size);
    pool_->EvictUntilUnderBudget();  // 新块是 kLocked 不在名单里,不会被误扔
    return block;
}

std::size_t PlainMemAllocator::GetBytesAllocated() const {
    base::AutoLock lock(pool_->lock);
    return pool_->allocated;
}

void PlainMemAllocator::ReleaseFreeMemory() {
    base::AutoLock lock(pool_->lock);
    while (!pool_->idle.empty()) {
        pool_->idle.head()->value()->Discard();
    }
}

}  // namespace liew
