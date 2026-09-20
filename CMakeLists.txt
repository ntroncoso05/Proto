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
find_package(Stb REQUIRED)
# Find the package provided by vcpkg
find_package(tinyobjloader CONFIG REQUIRED)

# No C++20 modules
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Enable solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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

# Include subdirectories
add_subdirectory(ProtoEngine)
add_subdirectory(ProtoApp)

# Set Playground as the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ProtoApp)

#set_target_properties (${TEMPLATE_NAME} PROPERTIES CXX_STANDARD 20)

# Define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS to treat VK_ERROR_OUT_OF_DATE_KHR as a success code
target_compile_definitions(ProtoEngine PRIVATE "VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS" )

#if(WIN32)
#  if(${CMAKE_GENERATOR} MATCHES "Visual Studio.*")
#    set_target_properties(${TEMPLATE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${TEMPLATE_DIR})
#  endif()
#endif()

