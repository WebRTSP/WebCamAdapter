cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 20)

project(WebCamAdapter)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(LIBCONFIG REQUIRED libconfig)
pkg_search_module(LIBUSBGX REQUIRED libusbgx)

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cmake
    *.h
    *.cpp
    *.conf.sample
    README.md
)

add_executable(${PROJECT_NAME} ${SOURCES} ${SNAP})
target_include_directories(${PROJECT_NAME} PUBLIC
    ${GLIB_INCLUDE_DIRS}
    ${LIBCONFIG_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIRS}
    ${LIBUSBGX_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME}
    ${GLIB_LDFLAGS}
    ${LIBCONFIG_LDFLAGS}
    ${SPDLOG_LDFLAGS}
    ${LIBUSBGX_LDFLAGS}
)
