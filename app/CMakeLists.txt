# ProtoApp executable
add_executable(ProtoApp)

# ProtoApp sources and headers
file(GLOB_RECURSE Proto_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)

target_sources(ProtoApp PRIVATE ${Proto_SOURCES})

# ProtoApp include dirs
target_include_directories(ProtoApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

# Preprocessor defines
#target_compile_definitions(ProtoApp PRIVATE
#    MY_TEST_DEFINE
#)

# Add directories with any random loose .dlls or .libs here
#target_link_directories(ProtoApp PRIVATE
  
#)

# Set configuration properties for ProtoApp
#if(MSVC)
#    target_compile_options(ProtoApp PRIVATE
#        /W3
#        /MP # multithreaded build
#        /WX # warnings as errors
#        /ZI # program database for edit and continue
#    )
#else()
#    target_compile_options(ProtoApp PRIVATE 
#        -Wall 
#        -Wextra 
#        -Wpedantic
#    )
#endif()

# Filters
source_group(TREE "${CMAKE_SOURCE_DIR}/app" PREFIX "Source Files" FILES ${Proto_SOURCES})

# Enable solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Links application to the internal engine layer
target_link_libraries(ProtoApp PRIVATE ProtoEngine)