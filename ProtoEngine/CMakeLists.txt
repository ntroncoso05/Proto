set (LIBRARY_NAME "ProtoEngine")

file(GLOB_RECURSE Proto_SOURCES CONFIGURE_DEPENDS "*")

# Filters
#source_group(TREE "${CMAKE_SOURCE_DIR}/${LIBRARY_NAME}" PREFIX "Source Files" FILES ${Proto_SOURCES})
source_group(TREE "${CMAKE_SOURCE_DIR}/${LIBRARY_NAME}" FILES ${Proto_SOURCES})

add_library(${LIBRARY_NAME} STATIC ${Proto_SOURCES})


# ProtoEngine sources models(.obj)
file(GLOB_RECURSE OBJ_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/*.obj"
)
# Mark the .obj files so Visual Studio sets "Exclude From Build" to Yes
set_source_files_properties(${OBJ_SOURCES} PROPERTIES 
    HEADER_FILE_ONLY TRUE
)

# Copy the entire assets directory to the build directory after compiling
add_custom_command(TARGET ${LIBRARY_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_BINARY_DIR}/assets"
)

# Set up include boundaries
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(${LIBRARY_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

# Link against the primary glslang targets
# Note: Newer versions of glslang have consolidated many components into these main targets
#target_link_libraries(MyTarget PRIVATE glslang::glslang glslang::SPIRV)

# Link your dependencies to the library context
target_link_libraries(${LIBRARY_NAME} PUBLIC 
    Vulkan::Vulkan 
    glfw 
    glm::glm
    tinyobjloader::tinyobjloader
    #stb::image
)

find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(TEMPLATE_DIR ${CMAKE_BINARY_DIR}/${LIBRARY_NAME})
set_target_properties (${LIBRARY_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEMPLATE_DIR})
# Slang shader
set (SHADER_TARGET ${LIBRARY_NAME}Shader)
set (SHADERS_DIR ${TEMPLATE_DIR}/assets/shaders)
set (SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/base.slang")
set (ENTRY_POINTS -entry vertMain -entry fragMain)
add_custom_command(OUTPUT ${SHADERS_DIR} COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR})
add_custom_command(
  OUTPUT  ${SHADERS_DIR}/slang.spv
  COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o ${SHADERS_DIR}/slang.spv
  WORKING_DIRECTORY ${SHADERS_DIR}
  DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
  COMMENT "Compiling Slang Shaders"
  VERBATIM
)
add_custom_target(${SHADER_TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
add_dependencies(${LIBRARY_NAME} ${SHADER_TARGET})

# Generate the configuration header file
# This takes a template file and creates a real header in the binary directory
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/ShaderConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ShaderConfig.hpp"
)
# Include the binary directory so ProtoEngine can find "ShaderConfig.h"
target_include_directories(${LIBRARY_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})