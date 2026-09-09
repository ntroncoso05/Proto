cmake_minimum_required(VERSION 3.30)

# The toolchain file setup should ideally be in a CMakePresets.json file,
# but we'll keep it here for simplicity
#set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    #CACHE STRING "Vcpkg toolchain file")

project(Proto VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# No C++20 modules
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) 

# Proto executable
#add_executable(Proto)

# Proto sources and headers
#file(GLOB_RECURSE Proto_SOURCES CONFIGURE_DEPENDS 
#    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
#    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
#    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
#    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.inl"
#)

#target_sources(Proto PRIVATE ${Proto_SOURCES})

# Proto include dirs
#target_include_directories(Proto PRIVATE
#    ${CMAKE_CURRENT_SOURCE_DIR}/src
#)

# Preprocessor defines
#target_compile_definitions(Proto PRIVATE
#    MY_TEST_DEFINE
#)

# Automatically links to these libs
#target_link_libraries(Proto PRIVATE
    #SDL3::SDL3
    #GLEW::GLEW
#)

# Add directories with any random loose .dlls or .libs here
#target_link_directories(Proto PRIVATE
  
#)

# Set configuration properties for Proto
#if(MSVC)
#    target_compile_options(Proto PRIVATE
#        /W3
#        /MP # multithreaded build
#        /WX # warnings as errors
#        /ZI # program database for edit and continue
#    )
#else()
#    target_compile_options(Proto PRIVATE 
#        -Wall 
#        -Wextra 
#        -Wpedantic
#    )
#endif()

# Set Proto as the startup project
#set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Proto)

# Filters
#source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${Proto_SOURCES})

# Enable solution folders
#set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Include subdirectories
add_subdirectory(engine)
add_subdirectory(app)

# Set ProtoApp as the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ProtoApp)