cmake_minimum_required(VERSION 3.26)
project(Compute VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
add_subdirectory(vendored/sdl EXCLUDE_FROM_ALL)
add_subdirectory(vendored/vk-bootstrap EXCLUDE_FROM_ALL)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(SOURCES
    src/main.cpp
    src/Renderer.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/vendored/imgui
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS} Vulkan::Vulkan SDL3::SDL3 vk-bootstrap::vk-bootstrap)

