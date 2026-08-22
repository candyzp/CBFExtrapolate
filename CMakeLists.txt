cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(CBFExtrapolate VERSION 1.0.0)

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Geode 5.9.0's Android GNU-STL compatibility layer does not support the
# swap/move-assignment paths used by the camera extrapolation state restore
# when compiling with Clang 21 / NDK r29. Build an Android-only generated copy
# of main.cpp that keeps the same behavior while using its supported copy
# assignment paths. Other platforms keep the faster swap-based implementation.
if (ANDROID OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Android")
    set(CBF_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    file(READ "${CBF_MAIN_SOURCE}" CBF_MAIN_ANDROID)

    string(REPLACE
        "m_gameState.m_tweenActions.swap(filteredTweens);"
        "m_gameState.m_tweenActions = filteredTweens;"
        CBF_MAIN_ANDROID "${CBF_MAIN_ANDROID}")
    string(REPLACE
        "m_gameState.m_tweenActions.swap(m_fields->m_filteredTweens);"
        "m_gameState.m_tweenActions = origGameState.m_tweenActions;"
        CBF_MAIN_ANDROID "${CBF_MAIN_ANDROID}")
    string(REPLACE
        "std::swap(m_gameState, origGameState);"
        "m_gameState = origGameState;"
        CBF_MAIN_ANDROID "${CBF_MAIN_ANDROID}")

    set(CBF_ANDROID_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${CBF_ANDROID_GEN_DIR}")
    set(CBF_ANDROID_MAIN "${CBF_ANDROID_GEN_DIR}/main_android.cpp")
    file(WRITE "${CBF_ANDROID_MAIN}" "${CBF_MAIN_ANDROID}")

    list(FILTER SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")
    list(APPEND SOURCES "${CBF_ANDROID_MAIN}")
endif()

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Clang diagnoses the existing unsigned-char list initialization in main.cpp as
# a C++11 narrowing error. Disabling only that diagnostic keeps the exact same
# generated code and runtime behavior while allowing the existing expression to
# compile on the current Xcode/Clang toolchain.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-c++11-narrowing)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME})
