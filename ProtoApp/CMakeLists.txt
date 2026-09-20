set (APPLICATION_NAME "ProtoApp")

# ProtoApp executable
add_executable(${APPLICATION_NAME})

# ProtoApp sources and headers
file(GLOB_RECURSE Proto_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)

target_sources(${APPLICATION_NAME} PRIVATE ${Proto_SOURCES})

# ProtoApp include dirs
target_include_directories(${APPLICATION_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

# Links application to the internal engine layer
target_link_libraries(${APPLICATION_NAME} PRIVATE ProtoEngine)

# Filters
source_group(TREE "${CMAKE_SOURCE_DIR}/${APPLICATION_NAME}" FILES ${Proto_SOURCES})

# Preprocessor defines
#target_compile_definitions(ProtoApp PRIVATE
#    MY_TEST_DEFINE
#)

# Add directories with any random loose .dlls or .libs here
#target_link_directories(ProtoApp PRIVATE
  
#)
