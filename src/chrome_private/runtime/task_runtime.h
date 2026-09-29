#pragma once
// TaskRuntime — liew 框架的任务设施模块(L1)。
// UI 主线程消息泵 + 线程池(adopt-or-create)。
// 构造须在将要运行 UI 的线程;同线程已有默认 task executor 则 CHECK 拒装。
//
// 有意不对称(不要"补全"):线程池不 Shutdown,线程活到进程退出(现状行为)。

#include <memory>

namespace liew::internal {

class TaskRuntime final {
  public:
    TaskRuntime();
    TaskRuntime(const TaskRuntime&) = delete;
    TaskRuntime& operator=(const TaskRuntime&) = delete;
    ~TaskRuntime();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace liew::internal
