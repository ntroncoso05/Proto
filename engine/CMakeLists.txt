
# ProtoEngine sources and headers
file(GLOB_RECURSE Proto_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)

add_library(ProtoEngine STATIC ${Proto_SOURCES})

# ProtoEngine include dirs
target_include_directories(ProtoEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

# Set up include boundaries
target_include_directories(ProtoEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(ProtoEngine PRIVATE ${Vulkan_INCLUDE_DIRS})

# Link against the primary glslang targets
# Note: Newer versions of glslang have consolidated many components into these main targets
#target_link_libraries(MyTarget PRIVATE glslang::glslang glslang::SPIRV)

# Link your dependencies to the library context
target_link_libraries(ProtoEngine PUBLIC 
    Vulkan::Vulkan 
    glfw 
    glm::glm
)

# Filters
source_group(TREE "${CMAKE_SOURCE_DIR}/engine" PREFIX "Source Files" FILES ${Proto_SOURCES})

# Enable solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)