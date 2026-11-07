set(LIBRARY_NAME "ProtoEngine")

# 1. Locate configuration-based dependencies provided by vcpkg
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(ktx CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# 2. Handle TinyGLTF discovery manually (Header-Only package format)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

# 3. Explicit Source collection
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_library(${LIBRARY_NAME} STATIC ${ENGINE_SOURCES} )

# 4. Target Compile Definitions (Restored and modernized)
target_compile_definitions(${LIBRARY_NAME} PRIVATE
    "PROTO_PLATFORM_WINDOWS"
    "PROTO_DEBUG"
    "GLFW_INCLUDE_NONE"
    "VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS" # Handles out-of-date swapchains gracefully
)

# 5. Global Include Boundaries
target_include_directories(${LIBRARY_NAME} 
    PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/src"     # Registers the 'src' root folder for PCH context routing
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Proto"
        "${TINYGLTF_INCLUDE_DIR}"
    PRIVATE 
        ${Vulkan_INCLUDE_DIRS}
)

# 6. Precompiled Header Configuration
# Passing an absolute path guarantees the build generator discovers the header immediately
target_precompile_headers(${LIBRARY_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/protopch.hpp")

# 7. Dependency Link Mapping
target_link_libraries(${LIBRARY_NAME} PUBLIC 
    Vulkan::Vulkan
    spdlog::spdlog
    glfw
    glm::glm
    KTX::ktx
    tinyobjloader::tinyobjloader
    imgui::imgui
)

# --- Unified Multi-Shader Slang Compilation Pipeline (Dynamic Entry Points) ---
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
set(SHADER_OUT_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")

# 1. Discover all raw .slang shader assets straight from your workspace root folder
file(GLOB_RECURSE SLANG_SOURCES CONFIGURE_DEPENDS "${SHADER_SRC_DIR}/*.slang")

set(SLANG_COMPILE_COMMANDS "")

# Ensure the output folder exists before any shader tries to build
list(APPEND SLANG_COMPILE_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}")

foreach(SHADER_FILE ${SLANG_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_SPV "${SHADER_OUT_DIR}/${SHADER_NAME}.spv")
    
    # --- DYNAMIC ENTRY POINT PARSING ---
    # Read the first 10 lines of the shader file looking for entry point signatures
    file(STRINGS "${SHADER_FILE}" SHADER_HEADERS LIMIT_COUNT 10 REGEX "//[ \t]*@entry:")
    
    set(DYNAMIC_ENTRY_ARGS "")
    
    if(SHADER_HEADERS)
        foreach(HEADER_LINE ${SHADER_HEADERS})
            # Extract the entry point function name after '@entry:'
            string(REGEX REPLACE ".*@entry:[ \t]*([A-Za-z0-9_]+).*" "\\1" ENTRY_NAME "${HEADER_LINE}")
            # Strip any trailing whitespace
            string(STRIP "${ENTRY_NAME}" ENTRY_NAME)
            
            # Append it to the compilation flags array
            list(APPEND DYNAMIC_ENTRY_ARGS -entry "${ENTRY_NAME}")
        endforeach()
    else()
        # Fallback defaults if no @entry tag is provided in the file
        set(DYNAMIC_ENTRY_ARGS -entry vertMain -entry fragMain)
    endif()
    
    # Pack the compiler arguments sequence using the dynamically extracted flags
    list(APPEND SLANG_COMPILE_COMMANDS 
        COMMAND ${SLANGC_EXECUTABLE} "${SHADER_FILE}" 
                -target spirv 
                -profile spirv_1_4 
                -emit-spirv-directly 
                -fvk-use-entrypoint-name 
                ${DYNAMIC_ENTRY_ARGS}
                -o "${OUTPUT_SPV}"
    )
endforeach()

# 3. Create the build target executing all commands sequentially
add_custom_target(${LIBRARY_NAME}Shaders ALL 
    ${SLANG_COMPILE_COMMANDS}
    COMMENT "Executing Slang Pipeline: Compiling all .slang source file assets directly..."
    VERBATIM
)

# 4. Freeze your engine compilation until this exact target resolves completely
add_dependencies(${LIBRARY_NAME} ${LIBRARY_NAME}Shaders)

# --- Generate the Configuration Header File ---
# This takes your template file and builds the concrete header file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/src/Proto/ShaderConfig.hpp"
)

# Inject the generated folder location into the target's include collection
target_include_directories(${LIBRARY_NAME} PRIVATE 
    "${CMAKE_CURRENT_BINARY_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/src/Proto"
)

# Optional: Add this at the bottom of ProtoEngine/CMakeLists.txt 
# if you want to visually browse your assets inside Visual Studio
# file(GLOB_RECURSE VISUAL_ASSETS 
#    "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/*.obj"
#    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.slang"
# )
# source_group("Assets" FILES ${VISUAL_ASSETS})