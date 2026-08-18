cmake_minimum_required(VERSION 3.16)
project(wbwopenglapi LANGUAGES C CXX)

# 字体后端选项:
#   ON  -> FreeType 矢量字体（Linux 必需；Windows 可选，需 third_party/freetype）
#   OFF -> Windows 使用系统 GDI 矢量字体（Linux 下配置为 ON 是强制的）
option(WBWOPENGAL_API_FONT_FREETYPE "使用 FreeType 字体后端" OFF)

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
        set_target_properties(freetype PROPERTIES
            IMPORTED_LOCATION ${WBWOPENGAL_ROOT}/third_party/freetype/lib/libfreetype.dll.a)
        set(WBWOPENGAL_FREETYPE_INCLUDE ${WBWOPENGAL_ROOT}/third_party/freetype/include/freetype2)
        set(WBWOPENGAL_FREETYPE_DLL ${WBWOPENGAL_ROOT}/third_party/freetype/bin/libfreetype-6.dll)
        set(WBWOPENGAL_FREETYPE_TARGET freetype)
    else()
        find_package(Freetype REQUIRED)
        set(WBWOPENGAL_FREETYPE_INCLUDE ${FREETYPE_INCLUDE_DIRS})
        list(APPEND WBWOPENGAL_FONT_LIBS ${FREETYPE_LIBRARIES})
    endif()
elseif(WIN32)
    # Windows 默认 GDI 后端, 需要链接 gdi32
    list(APPEND WBWOPENGAL_FONT_LIBS gdi32)
else()
    message(FATAL_ERROR "非 Windows 平台必须启用 FreeType 后端: cmake -DWBWOPENGAL_API_FONT_FREETYPE=ON")
endif()

# ---------------- 示例 ----------------
file(GLOB WBWOPENGAL_EXAMPLES ${WBWOPENGAL_ROOT}/examples/*.cpp)
foreach(src ${WBWOPENGAL_EXAMPLES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE
        ${WBWOPENGAL_ROOT}/include
        ${WBWOPENGAL_GLFW_INCLUDE}
        ${WBWOPENGAL_FREETYPE_INCLUDE})
    target_link_libraries(${name} PRIVATE glad ${WBWOPENGAL_GLFW_TARGET} ${WBWOPENGAL_FONT_LIBS})
    target_compile_definitions(${name} PRIVATE ${WBWOPENGAL_FONT_DEFINES})
    if(WIN32)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${WBWOPENGAL_GLFW_DLL} $<TARGET_FILE_DIR:${name}>)
        if(WBWOPENGAL_API_FONT_FREETYPE)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${WBWOPENGAL_FREETYPE_DLL} $<TARGET_FILE_DIR:${name}>)
        endif()
    endif()
endforeach()