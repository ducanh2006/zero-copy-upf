cmake_minimum_required(VERSION 3.16)
project(skyt_upf_zerocopy C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-O3 -Wall -Wextra -pthread)
find_package(Threads REQUIRED)

enable_testing()

option(USE_DPDK "Enable DPDK real adapter (requires PKG_CONFIG_PATH=third_party/dpdk-24.11/build/meson-uninstalled)" OFF)

# ---- mbuf library (collect sources that exist) ----
set(MBUF_LIB_SOURCES "")
foreach(f src/mbuf_model.c src/outer_header.c src/deepcopy.c src/zerocopy.c)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
    list(APPEND MBUF_LIB_SOURCES ${f})
  endif()
endforeach()

if(MBUF_LIB_SOURCES)
  add_library(mbuf STATIC ${MBUF_LIB_SOURCES})
  target_include_directories(mbuf PUBLIC src)
  target_link_libraries(mbuf PUBLIC Threads::Threads)
else()
  # Placeholder interface lib so build passes on WT0 skeleton
  add_library(mbuf INTERFACE)
  target_include_directories(mbuf INTERFACE src)
endif()

# ---- DPDK (optional) ----
if(USE_DPDK)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DPDK REQUIRED libdpdk)
  message(STATUS "DPDK found: ${DPDK_VERSION} cflags=${DPDK_CFLAGS} ldflags=${DPDK_LDFLAGS}")
  add_compile_definitions(USE_DPDK)
  if(TARGET mbuf AND NOT MBUF_LIB_SOURCES STREQUAL "")
    # mbuf already defined as STATIC; attach DPDK flags via interface propagation below
  endif()
endif()

# ---- bench executable ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/bench.c")
  add_executable(bench_upf bench/bench.c)
  if(MBUF_LIB_SOURCES)
    target_link_libraries(bench_upf PRIVATE mbuf Threads::Threads m)
  else()
    target_link_libraries(bench_upf PRIVATE Threads::Threads m)
  endif()
  if(USE_DPDK)
    target_include_directories(bench_upf PRIVATE ${DPDK_INCLUDE_DIRS})
    target_compile_options(bench_upf PRIVATE ${DPDK_CFLAGS})
    target_link_libraries(bench_upf PRIVATE ${DPDK_LDFLAGS})
  endif()
endif()

# ---- tests ----
function(skyt_add_test name srcfile)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${srcfile}")
    add_executable(${name} ${srcfile})
    if(MBUF_LIB_SOURCES)
      target_link_libraries(${name} PRIVATE mbuf Threads::Threads)
    else()
      target_link_libraries(${name} PRIVATE Threads::Threads)
    endif()
    if(USE_DPDK)
      target_include_directories(${name} PRIVATE ${DPDK_INCLUDE_DIRS})
      target_compile_options(${name} PRIVATE ${DPDK_CFLAGS})
      target_link_libraries(${name} PRIVATE ${DPDK_LDFLAGS})
    endif()
    add_test(NAME ${name} COMMAND ${name})
  endif()
endfunction()

skyt_add_test(test_chain tests/test_chain.c)
skyt_add_test(test_lifecycle tests/test_lifecycle.c)
skyt_add_test(test_header tests/test_header.c)
skyt_add_test(test_zerocopy tests/test_zerocopy.c)
