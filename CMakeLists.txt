# ============================================================================
#  CopyMath — CMake 构建入口
#
#  用法（Windows + MinGW-w64）：
#      cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
#      cmake --build build --parallel
#      ctest --test-dir build --output-on-failure
#  或者直接双击根目录的 build.bat（等价于上面三条）。
#
#  ⚠ 版本号要同步改三处：
#      1) 本文件 project(... VERSION 1.3.0 ...)
#      2) src/app.rc 的 FILEVERSION / PRODUCTVERSION / FileVersion / ProductVersion
#      3) src/app/app.h 的 COPYMATH_VERSION（窗口标题用）
#    改完重编，在「属性 → 详细信息」核对。
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(CopyMath VERSION 1.3.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "CopyMath 是纯 Win32 程序（GDI+ / 剪贴板 / 调外部 TeX），只支持 Windows。")
endif()

# app.rc 是 UTF-8（版本信息里有中文），必须显式告诉 windres；
# -I 让它能按“相对项目根”的路径找到 assets/CopyMath.ico。
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -c 65001 -I\"${CMAKE_CURRENT_SOURCE_DIR}\"")

# ---------------------------------------------------------------------------
#  主程序：13 个 .cpp + 1 个资源脚本
#  WIN32 = 链接成 GUI 子系统程序（-mwindows），不弹控制台窗口。
# ---------------------------------------------------------------------------
add_executable(CopyMath WIN32
    src/main.cpp
    src/app/app_core.cpp
    src/app/actions.cpp
    src/app/render_controller.cpp
    src/ui/main_window.cpp
    src/ui/settings_dialog.cpp
    src/ui/preview_view.cpp
    src/ui/edit_subclass.cpp
    src/config/config.cpp
    src/render/render.cpp
    src/log/log.cpp
    src/clipboard/clipboard.cpp
    src/util/util.cpp
    src/app.rc
)

# 每个模块一个目录 → 一个 -I，源码里照旧写 #include "config.h"
target_include_directories(CopyMath PRIVATE
    src/app
    src/ui
    src/config
    src/render
    src/log
    src/clipboard
    src/util
)

target_link_libraries(CopyMath PRIVATE gdiplus shell32 shlwapi ole32)

if(MINGW)
    # 静态链接 GCC 运行库：分发的 exe 不依赖 libgcc/libstdc++ DLL
    target_link_options(CopyMath PRIVATE -static-libgcc -static-libstdc++)
endif()

# 成品统一输出到 bin\：根目录保持纯工程（源码 / 构建脚本 / 文档），
# 编译中间文件仍然全在 build\。程序运行时读写的 CopyMath.cfg、tmp\、export\
# 都在 exe 旁边（也就是 bin\ 里），所以把 bin\ 整个拷走即可使用。
set_target_properties(CopyMath PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    # 多配置生成器（MSVC）默认还会再套一层 Release\；显式钉死，
    # 免得 bin\ 里又冒出 bin\Release\CopyMath.exe。
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    OUTPUT_NAME "CopyMath")

# ---------------------------------------------------------------------------
#  测试（CTest）：目前覆盖 util 里那些纯函数（换行规范化 / 路径工具）
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
