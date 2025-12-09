# CMakeLists.txt for videoTracker ImGui Dependencies
# This CMake file handles ImGui, ImPlot, and extensions via imgui_bundle
# The resulting libraries can be linked into the openFrameworks project

cmake_minimum_required(VERSION 3.15)
project(videoTracker_imgui_deps VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option 1: Use imgui_bundle via FetchContent (automatic download)
include(FetchContent)

FetchContent_Declare(
    imgui_bundle
    GIT_REPOSITORY https://github.com/pthom/imgui_bundle.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

# Make imgui_bundle available
FetchContent_MakeAvailable(imgui_bundle)

# Option 2: If you prefer to use local imgui_bundle (uncomment below)
# set(IMGUI_BUNDLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_bundle" CACHE PATH "Path to imgui_bundle")
# if(EXISTS "${IMGUI_BUNDLE_DIR}")
#     add_subdirectory(${IMGUI_BUNDLE_DIR})
# endif()

# After imgui_bundle is available, we can access its targets
# imgui_bundle provides: imgui_bundle target which includes ImGui, ImPlot, etc.

# Create a convenience target that includes all ImGui dependencies
add_library(videoTracker_imgui_deps INTERFACE)

# Link to imgui_bundle (which includes ImGui, ImPlot, and extensions)
# imgui_bundle provides all necessary includes - no need for local duplicates
target_link_libraries(videoTracker_imgui_deps INTERFACE imgui_bundle)

# Add our integration wrapper
add_subdirectory(libs/imgui_integration)

# Print configuration info
message(STATUS "=== videoTracker ImGui Dependencies Configuration ===")
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===================================================")

