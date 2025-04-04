cmake_minimum_required(VERSION 3.5...4.0)
project(PTHASH)

# Add option for instrumentation
option(PTHASH_ENABLE_INSTRUMENTATION "Enable debug instrumentation and logging" OFF)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif ()
MESSAGE(STATUS "CMAKE_BUILD_TYPE: " ${CMAKE_BUILD_TYPE})

if(NOT TARGET PTHASH)
  add_library(PTHASH INTERFACE)
  target_include_directories(PTHASH INTERFACE include)
  target_include_directories(PTHASH SYSTEM INTERFACE external/fastmod)
  target_include_directories(PTHASH SYSTEM INTERFACE external/mm_file/include)
  target_include_directories(PTHASH SYSTEM INTERFACE external/xxHash)
  target_include_directories(PTHASH SYSTEM INTERFACE external/json/include)

  target_compile_features(PTHASH INTERFACE cxx_std_17)

  # Set instrumentation flag if enabled
  if (PTHASH_ENABLE_INSTRUMENTATION)
    MESSAGE(STATUS "Instrumentation is ENABLED")
    target_compile_definitions(PTHASH INTERFACE -DPTHASH_ENABLE_INSTRUMENTATION=1)
  else()
    MESSAGE(STATUS "Instrumentation is DISABLED")
    target_compile_definitions(PTHASH INTERFACE -DPTHASH_ENABLE_INSTRUMENTATION=0)
  endif()

  MESSAGE(STATUS "Compiling for processor: " ${CMAKE_HOST_SYSTEM_PROCESSOR})
  if (UNIX AND (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64"))
    MESSAGE(STATUS "Compiling with flags: -march=native -mbmi2 -msse4.2")
    target_compile_options(PTHASH INTERFACE -march=native)
    target_compile_options(PTHASH INTERFACE -mbmi2 -msse4.2) # for hardware popcount and pdep
  endif()

  # Add ARM64 specific flags
  if (UNIX AND (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64"))
    MESSAGE(STATUS "Compiling for ARM64 with appropriate flags")
    target_compile_options(PTHASH INTERFACE -march=native)
  endif()

  if (PTHASH_ENABLE_ALL_ENCODERS)
    MESSAGE(STATUS "compiling with all encoders")
    target_compile_options(PTHASH INTERFACE -DPTHASH_ENABLE_ALL_ENCODERS)
  endif()

  if (PTHASH_ENABLE_LARGE_BUCKET_ID_TYPE)
    MESSAGE(STATUS "bucket_id_type is uint64_t")
    target_compile_options(PTHASH INTERFACE -DPTHASH_ENABLE_LARGE_BUCKET_ID_TYPE)
  endif()

  find_package(Threads REQUIRED)
  target_link_libraries(PTHASH INTERFACE Threads::Threads)

  if (PTHASH_USE_SANITIZERS)
    MESSAGE(STATUS "Using sanitizers. Compiling with flags: -fsanitize=address -fno-omit-frame-pointer")
    target_compile_options(PTHASH INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  endif()

  add_subdirectory(external/bits)
  target_link_libraries(PTHASH INTERFACE BITS)
endif()

# Only add benchmarks and tests when compiling PTHash itself, not when added as a dependency
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_compile_options(PTHASH INTERFACE -Werror -Wall -Wextra -Wno-missing-braces -Wno-unknown-attributes -Wno-unused-function -ggdb)
  add_subdirectory(external/cmd_line_parser)

  add_executable(build src/build.cpp)
  target_link_libraries(build PRIVATE PTHASH CMD_LINE_PARSER)
  add_executable(example src/example.cpp)
  target_link_libraries(example PRIVATE PTHASH CMD_LINE_PARSER)
  
  add_executable(build_phf src/build_phf.cpp)
  target_link_libraries(build_phf pthash_static)
  target_include_directories(build_phf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  target_compile_features(build_phf PRIVATE cxx_std_17)
  set_target_properties(build_phf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- Add this section to create a static library for CGo linking ---
add_library(pthash_static STATIC src/pthash_dummy.cpp) # Add dummy source file

# Link this static library against the existing PTHASH INTERFACE library.
# This effectively bundles the dependencies (like BITS) and usage requirements
# (include paths, compile flags) defined by PTHASH INTERFACE into pthash_static.
target_link_libraries(pthash_static INTERFACE PTHASH)
# --- End of added section ---

add_executable(generate_test_data src/generate_test_data.cpp)
target_compile_features(generate_test_data PRIVATE cxx_std_17)
set_target_properties(generate_test_data PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

endif()
