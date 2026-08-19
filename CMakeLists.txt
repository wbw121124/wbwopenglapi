cmake_minimum_required(VERSION 3.16)
project(wbwopenglapi LANGUAGES C CXX)

# 字体后端选项:
#   ON  -> FreeType 矢量字体（Linux 必需；Windows 可选，需 third_party/freetype）
#   OFF -> Windows 使用系统 GDI 矢量字体（Linux 下配置为 ON 是强制的）
option(WBWOPENGAL_API_FONT_FREETYPE "使用 FreeType 字体后端" OFF)
# 可选: 在 FreeType 后端基础上启用 HarfBuzz 整形（连体/OpenType 特性，需 third_party/harfbuzz）
option(WBWOPENGAL_API_FONT_HARFBUZZ "使用 HarfBuzz 文本整形" OFF)
# DirectWrite 矢量字体后端（Windows 8.1+，系统库 dwrite 无第三方依赖；与 FreeType 互斥）
option(WBWOPENGAL_API_FONT_DWRITE "使用 DirectWrite 字体后端（仅 Windows）" OFF)
if(WBWOPENGAL_API_FONT_DWRITE AND NOT WIN32)
    message(FATAL_ERROR "WBWOPENGAL_API_FONT_DWRITE 仅支持 Windows 平台")
endif()
if(WBWOPENGAL_API_FONT_DWRITE AND WBWOPENGAL_API_FONT_FREETYPE)
    message(FATAL_ERROR "WBWOPENGAL_API_FONT_DWRITE 与 WBWOPENGAL_API_FONT_FREETYPE 互斥，只能启用其一")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(WBWOPENGAL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------- GLAD (glad2, gl:core=3.3) ----------------
add_library(glad STATIC ${WBWOPENGAL_ROOT}/third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC ${WBWOPENGAL_ROOT}/third_party/glad/include)

# ---------------- GLFW ----------------
if(WIN32)
    add_library(glfw3 UNKNOWN IMPORTED)
    set_target_properties(glfw3 PROPERTIES
        IMPORTED_LOCATION ${WBWOPENGAL_ROOT}/third_party/glfw/libglfw3dll.a)
    set(WBWOPENGAL_GLFW_INCLUDE ${WBWOPENGAL_ROOT}/third_party/glfw/include)
    set(WBWOPENGAL_GLFW_DLL ${WBWOPENGAL_ROOT}/third_party/glfw/glfw3.dll)
    set(WBWOPENGAL_GLFW_TARGET glfw3)
else()
    find_package(glfw3 REQUIRED)
    set(WBWOPENGAL_GLFW_INCLUDE "")
    set(WBWOPENGAL_GLFW_TARGET glfw)
endif()

# ---------------- 字体后端 ----------------
set(WBWOPENGAL_FONT_DEFINES "")
set(WBWOPENGAL_FONT_LIBS "")
if(WBWOPENGAL_API_FONT_FREETYPE)
    list(APPEND WBWOPENGAL_FONT_DEFINES WBWOPENGAL_API_FONT_FREETYPE)
    if(WIN32)
        add_library(freetype UNKNOWN IMPORTED)
        # 直接导入 DLL（MinGW 可链接 MSVC 编译的 freetype.dll）
        set_target_properties(freetype PROPERTIES
            IMPORTED_LOCATION ${WBWOPENGAL_ROOT}/third_party/freetype/bin/freetype.dll)
        set(WBWOPENGAL_FREETYPE_INCLUDE ${WBWOPENGAL_ROOT}/third_party/freetype/include)
        set(WBWOPENGAL_FREETYPE_DLL ${WBWOPENGAL_ROOT}/third_party/freetype/bin/freetype.dll)
        set(WBWOPENGAL_FREETYPE_TARGET freetype)
        if(WBWOPENGAL_API_FONT_HARFBUZZ)
            list(APPEND WBWOPENGAL_FONT_DEFINES WBWOPENGAL_API_FONT_HARFBUZZ)
            add_library(harfbuzz UNKNOWN IMPORTED)
            set_target_properties(harfbuzz PROPERTIES
                IMPORTED_LOCATION ${WBWOPENGAL_ROOT}/third_party/harfbuzz/bin/libharfbuzz-0.dll)
            set(WBWOPENGAL_HARFBUZZ_INCLUDE ${WBWOPENGAL_ROOT}/third_party/harfbuzz/include)
            set(WBWOPENGAL_HARFBUZZ_DLL ${WBWOPENGAL_ROOT}/third_party/harfbuzz/bin/libharfbuzz-0.dll)
            set(WBWOPENGAL_HARFBUZZ_TARGET harfbuzz)
        endif()
    else()
        find_package(Freetype REQUIRED)
        set(WBWOPENGAL_FREETYPE_INCLUDE ${FREETYPE_INCLUDE_DIRS})
        list(APPEND WBWOPENGAL_FONT_LIBS ${FREETYPE_LIBRARIES})
        if(WBWOPENGAL_API_FONT_HARFBUZZ)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
            list(APPEND WBWOPENGAL_FONT_DEFINES WBWOPENGAL_API_FONT_HARFBUZZ)
        endif()
    endif()
elseif(WBWOPENGAL_API_FONT_DWRITE)
    # DirectWrite（Windows 已在上方强制校验）
    list(APPEND WBWOPENGAL_FONT_DEFINES WBWOPENGAL_API_FONT_DWRITE)
    # 过渡期: 步 2 接入头文件 DWrite 分支前, 宏未生效时回落 GDI 后端, 保留 gdi32
    list(APPEND WBWOPENGAL_FONT_LIBS dwrite gdi32)
elseif(WIN32)
    # Windows 默认 GDI 后端, 需要链接 gdi32
    list(APPEND WBWOPENGAL_FONT_LIBS gdi32)
else()
    message(FATAL_ERROR "非 Windows 平台必须启用 FreeType 后端: cmake -DWBWOPENGAL_API_FONT_FREETYPE=ON")
endif()

# ---------------- Node-API 插件（可选） ----------------
# 启用后 napi/ 子目录以常规 CMake 模式构建（Linux/macOS 需系统 glfw/freetype；
# Windows 请在 napi/ 下用 cmake-js 构建，见 napi/CMakeLists.txt 顶部说明）
option(WBWOPENGAL_BUILD_NODE "构建 napi/ Node-API 插件（Linux/macOS）" OFF)
if(WBWOPENGAL_BUILD_NODE)
    add_subdirectory(napi)
endif()

# ---------------- Skia 可选后端（软件栅格化, 集成不替换） ----------------
# 两种引入方式（二选一）：
#   A. vcpkg skia 端口:  cmake -DWBWOPENGAL_API_SKIA=ON
#      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#   B. GN+LLVM 自建产物直连（google/skia chrome/m* 等: include/ + *.lib）:
#      cmake -DWBWOPENGAL_API_SKIA=ON -DWBWOPENGAL_API_SKIA_DIR=<gn out 目录>
# 启用后额外构建示例 14_skia（无窗口, 仅链接 skia, 见下文；icudtl.dat 需自拷运行）
option(WBWOPENGAL_API_SKIA "使用 Skia 软件栅格化后端（vcpkg skia 端口或 GN 产物直连）" OFF)
set(WBWOPENGAL_API_SKIA_DIR "" CACHE PATH "GN/LLVM 自建 skia 产物目录（含 include/ 与 *.lib/*.a，跳过 vcpkg）")
if(WBWOPENGAL_API_SKIA)
    add_executable(14_skia ${WBWOPENGAL_ROOT}/examples/14_skia.cpp)
    target_include_directories(14_skia PRIVATE ${WBWOPENGAL_ROOT}/include)
    target_compile_definitions(14_skia PRIVATE WBWOPENGAL_API_SKIA)
    if(WBWOPENGAL_API_SKIA_DIR)
        target_include_directories(14_skia PRIVATE ${WBWOPENGAL_API_SKIA_DIR}/include)
        if(WIN32)
            file(GLOB WBWOPENGAL_SKIA_LIBS "${WBWOPENGAL_API_SKIA_DIR}/*.lib")
        else()
            file(GLOB WBWOPENGAL_SKIA_LIBS "${WBWOPENGAL_API_SKIA_DIR}/*.a")
        endif()
        if(NOT WBWOPENGAL_SKIA_LIBS)
            message(FATAL_ERROR "WBWOPENGAL_API_SKIA_DIR 下未找到 skia 库文件（*.lib/*.a）: ${WBWOPENGAL_API_SKIA_DIR}")
        endif()
        target_link_libraries(14_skia PRIVATE ${WBWOPENGAL_SKIA_LIBS})
    else()
        find_package(skia CONFIG REQUIRED)
        target_link_libraries(14_skia PRIVATE skia)
    endif()
endif()

# ---------------- 示例 ----------------
file(GLOB WBWOPENGAL_EXAMPLES ${WBWOPENGAL_ROOT}/examples/*.cpp)
# 14_skia 仅在 WBWOPENGAL_API_SKIA=ON 时构建（见上）
list(REMOVE_ITEM WBWOPENGAL_EXAMPLES ${WBWOPENGAL_ROOT}/examples/14_skia.cpp)
foreach(src ${WBWOPENGAL_EXAMPLES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE
        ${WBWOPENGAL_ROOT}/include
        ${WBWOPENGAL_GLFW_INCLUDE}
        ${WBWOPENGAL_FREETYPE_INCLUDE}
        ${WBWOPENGAL_HARFBUZZ_INCLUDE})
    target_link_libraries(${name} PRIVATE glad ${WBWOPENGAL_GLFW_TARGET}
        ${WBWOPENGAL_HARFBUZZ_TARGET} ${WBWOPENGAL_FONT_LIBS})
    target_compile_definitions(${name} PRIVATE ${WBWOPENGAL_FONT_DEFINES})
    if(WIN32)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${WBWOPENGAL_GLFW_DLL} $<TARGET_FILE_DIR:${name}>)
        if(WBWOPENGAL_API_FONT_FREETYPE)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${WBWOPENGAL_FREETYPE_DLL} $<TARGET_FILE_DIR:${name}>)
            if(WBWOPENGAL_API_FONT_HARFBUZZ)
                add_custom_command(TARGET ${name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${WBWOPENGAL_HARFBUZZ_DLL} $<TARGET_FILE_DIR:${name}>)
            endif()
        endif()
    endif()
endforeach()