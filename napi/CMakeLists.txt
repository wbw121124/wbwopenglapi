# napi/CMakeLists.txt - wbwopenglapi 的 Node-API 插件构建
#   模式 A（默认，cmake-js）：cd napi && npm run build
#     cmake-js 注入：CMAKE_JS_INC（Node 头）、CMAKE_JS_LIB（node.lib）、
#     CMAKE_JS_SRC（win_delay_load_hook.cc）、CMAKE_JS_NODELIB_*（MSVC 专用）
#   模式 B（常规 CMake 子目录）：根 CMakeLists.txt -DWBWOPENGAL_BUILD_NODE=ON
#     add_subdirectory(napi) 引入，仅限 Linux/macOS（Windows 请用 cmake-js）
#   约定：target 名 = project 名 = wbwopenglapi（cmake-js 按 project 名查找 addon）
#   后端：Windows 默认 GDI；GLFW 隐藏窗口无头渲染
cmake_minimum_required(VERSION 3.16)
project(wbwopenglapi LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# N-API 版本 8：Node.js LTS（>= 16.11）ABI 稳定
# NAPI_HAS_THREADS=0：本机 MinGW-W64 8.1 无 winpthreads，std::mutex 不可用；
# 本插件为单线程同步渲染，不依赖 ThreadSafeFunction/AsyncWorker，
# 关闭后 node-addon-api 走无线程路径（与官方 wasm 构建相同的官方开关）
add_compile_definitions(NAPI_VERSION=8 NAPI_HAS_THREADS=0)

# 字体后端：Windows 默认 GDI；Linux/macOS 必须 FreeType（wbwopenglapi.hpp 约束）
if(NOT WIN32)
    find_package(Freetype REQUIRED)
endif()

# 常规模式下（非 cmake-js）需要 node_api.h；NODE_INCLUDE_DIR 可经
# -DNODE_INCLUDE_DIR=... 或环境变量 NODE_INCLUDE_DIR 传入，否则在
# 常见包管理器路径查找（apt nodejs-dev / dnvm / nvm 等不在默认路径时须显式指定）
if(NOT DEFINED CMAKE_JS_INC)
    if(WIN32)
        message(FATAL_ERROR
            "napi 常规构建仅支持 Linux/macOS；Windows 请在 napi/ 目录执行 npm run build（cmake-js）")
    endif()
    if(NOT TARGET glad)
        message(FATAL_ERROR
            "napi 常规模式须由根 CMakeLists.txt 引入：cmake -DWBWOPENGAL_BUILD_NODE=ON")
    endif()
    find_path(NODE_INCLUDE_DIR node_api.h
        HINTS ENV NODE_INCLUDE_DIR
        PATHS /usr/include/node /usr/local/include/node)
    if(NOT NODE_INCLUDE_DIR)
        message(FATAL_ERROR
            "未找到 node_api.h：apt install nodejs-dev（或设置 -DNODE_INCLUDE_DIR=<Node 头目录>）")
    endif()
endif()

# cmake-js 在 Windows 上无条件注入 MSVC 专用 /DELAYLOAD:NODE.EXE，GNU ld 不识别，非 MSVC 时剥离
if(WIN32 AND NOT MSVC)
    string(REPLACE "/DELAYLOAD:NODE.EXE" "" CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS}")
endif()

set(WBWOPENGAL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(wbwopenglapi SHARED
    src/addon.cc
    src/renderer_wrap.cc
    src/bmp.cc
    ${CMAKE_JS_SRC}
    ${WBWOPENGAL_ROOT}/third_party/glad/src/gl.c)

set_target_properties(wbwopenglapi PROPERTIES PREFIX "" SUFFIX ".node")

target_include_directories(wbwopenglapi PRIVATE
    ${CMAKE_JS_INC}
    ${NODE_INCLUDE_DIR}
    ${WBWOPENGAL_ROOT}/include
    ${WBWOPENGAL_ROOT}/third_party/glad/include
    ${WBWOPENGAL_ROOT}/third_party/glfw/include
    ${FREETYPE_INCLUDE_DIRS})

if(WIN32)
    # MinGW 直接链接 MSVC 编译的 glfw3 导入库（仓库既有做法，见根 CMakeLists.txt）
    target_link_libraries(wbwopenglapi PRIVATE
        ${CMAKE_JS_LIB}
        ${WBWOPENGAL_ROOT}/third_party/glfw/libglfw3dll.a
        opengl32
        gdi32)
else()
    # Linux/macOS：系统 glfw + OpenGL + FreeType（napi 头由 CMAKE_JS_INC/NODE_INCLUDE_DIR 提供）
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)
    target_compile_definitions(wbwopenglapi PRIVATE WBWOPENGAL_API_FONT_FREETYPE)
    target_link_libraries(wbwopenglapi PRIVATE
        ${CMAKE_JS_LIB}
        glfw
        OpenGL::GL
        ${FREETYPE_LIBRARIES})
    if(APPLE)
        # macOS 动态加载 node 符号（napi_* 导出自 node 二进制，链接期允许未定义）
        set_target_properties(wbwopenglapi PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup")
    endif()
endif()

# PNG 支持：系统 zlib 探测（同根 build.ps1：Windows MinGW 自带 libz.a、
# Linux/macOS 系统 libz）。可用 -> 定义 WBWOPENGAL_API_PNG 并链接；
# 否则 PNG 导出（loadPNG/savePNG/toPNG）不注册，其余 API 不受影响
if(WIN32)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES z)
    check_cxx_source_compiles(
        "#include <zlib.h>\nint main(){return zlibVersion()==0;}"
        WBW_NAPI_HAVE_ZLIB)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(WBW_NAPI_HAVE_ZLIB)
        target_compile_definitions(wbwopenglapi PRIVATE WBWOPENGAL_API_PNG)
        target_link_libraries(wbwopenglapi PRIVATE z)
    endif()
else()
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_compile_definitions(wbwopenglapi PRIVATE WBWOPENGAL_API_PNG)
        target_link_libraries(wbwopenglapi PRIVATE ZLIB::ZLIB)
    endif()
endif()
message(STATUS "wbwopenglapi.node PNG(zlib): ${WBW_NAPI_HAVE_ZLIB}${ZLIB_FOUND}")

# MinGW 下从 node_api.def 生成 node.exe 导入库（MSVC 用 lib.exe；MinGW 用 dlltool）
if(WIN32 AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
    if(MSVC)
        execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF}
            /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
    else()
        execute_process(COMMAND dlltool -d ${CMAKE_JS_NODELIB_DEF}
            -D node.exe -l ${CMAKE_JS_NODELIB_TARGET}
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endif()

# 运行时 DLL 与 .node 同目录（LoadLibrary 的 DLL 搜索含模块所在目录）
if(WIN32)
    add_custom_command(TARGET wbwopenglapi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${WBWOPENGAL_ROOT}/third_party/glfw/glfw3.dll
            $<TARGET_FILE_DIR:wbwopenglapi>)
endif()