# 自举冒烟 = 导出包的第一个消费者: 与外部客户完全同构地 find_package 消费。
set(liew_DIR "${CMAKE_BINARY_DIR}/cmake")  # 仓库 configure 期生成的导出包
find_package(liew CONFIG REQUIRED)

add_executable(liew-smoke liew_smoke/liew_smoke_main.cc)
# imported target 不携带仓内构建边，自举时手动保证 DLL/import lib 先产出。
# liew 自身已经依赖 liew-tree；外部消费者由包内 fail-fast 保证 SDK 已构建。
add_dependencies(liew-smoke liew)
target_link_libraries(liew-smoke PRIVATE liew::liew)
liew_copy_runtime_assets(liew-smoke)
