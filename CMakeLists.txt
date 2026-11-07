cmake_minimum_required(VERSION 3.30)

project(Proto VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Enable solution folders for IDE organization
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Include subdirectories
add_subdirectory(ProtoEngine)
add_subdirectory(ProtoApp)

# Set default startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ProtoApp)