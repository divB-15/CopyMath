# ============================================================================
#  tests — 单元测试（CTest 驱动）
#
#  这里只测**不碰界面、不碰全局变量**的纯逻辑（util 里的换行规范化、路径工具），
#  所以测试可以直接链 util.cpp 编成一个控制台程序，不需要 GDI+ 与窗口。
#  以后要加 Catch2 / GoogleTest，往 tests/ 里放即可，顶层 CMakeLists 不用改。
# ============================================================================
add_executable(test_util
    test_util.cpp
    ${CMAKE_SOURCE_DIR}/src/util/util.cpp
)

target_include_directories(test_util PRIVATE ${CMAKE_SOURCE_DIR}/src/util)

add_test(NAME util COMMAND test_util)
