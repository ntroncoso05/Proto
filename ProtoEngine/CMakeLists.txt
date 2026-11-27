set(LIBRARY_NAME "ProtoEngine")

# 1. Locate configuration-based dependencies provided by vcpkg
find_package(Vulkan REQUIRED)

if(ANDROID)
    find_package(game-activity REQUIRED CONFIG)

    include(FetchContent)
    FetchContent_Declare(vulkan_headers URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/heads/main.zip)
    FetchContent_Declare(glm URL https://github.com/g-truc/glm/releases/download/1.0.3/glm-1.0.3.zip)
    FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
    FetchContent_Declare(spdlog URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.zip)
    FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb GIT_TAG master)
    FetchContent_Declare(tinygltf URL https://github.com/syoyo/tinygltf/archive/refs/tags/v2.9.3.zip)
    FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.0-docking.zip)
    FetchContent_Declare(imguizmo URL https://github.com/CedricGuillemet/ImGuizmo/archive/refs/tags/1.10.zip)
    FetchContent_Declare(Jolt URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.2.0.zip SOURCE_SUBDIR Build)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)

    FetchContent_GetProperties(vulkan_headers)
    if(NOT vulkan_headers_POPULATED)
        FetchContent_Populate(vulkan_headers)
    endif()    

    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
    add_library(imgui::imgui ALIAS imgui)
    # Expose imgui_SOURCE_DIR to ImGuizmo's CMakeLists.txt before MakeAvailable!
    include_directories(${imgui_SOURCE_DIR})

    set(USE_AVX OFF CACHE BOOL "" FORCE)
    set(USE_AVX2 OFF CACHE BOOL "" FORCE)
    set(USE_AVX512 OFF CACHE BOOL "" FORCE)
    set(USE_SSE4_1 OFF CACHE BOOL "" FORCE)
    set(USE_SSE4_2 OFF CACHE BOOL "" FORCE)
    set(USE_F16C OFF CACHE BOOL "" FORCE)
    set(USE_LZCNT OFF CACHE BOOL "" FORCE)
    set(USE_TZCNT OFF CACHE BOOL "" FORCE)
    set(USE_FMADD OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glm nlohmann_json spdlog stb tinygltf imguizmo Jolt)
    
    if(TARGET Jolt AND NOT TARGET Jolt::Jolt)
        add_library(Jolt::Jolt ALIAS Jolt)
    endif()

    if(TARGET imguizmo AND NOT TARGET imguizmo::imguizmo)
        target_include_directories(imguizmo PUBLIC ${imguizmo_SOURCE_DIR} ${imgui_SOURCE_DIR})
        target_link_libraries(imguizmo PUBLIC imgui)
        add_library(imguizmo::imguizmo ALIAS imguizmo)
    endif()
else()
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(ktx CONFIG REQUIRED)
    find_package(tinyobjloader CONFIG REQUIRED)
    find_package(imguizmo CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(Jolt CONFIG REQUIRED)
    find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)
    # 2. Handle TinyGLTF discovery manually (Header-Only package format)
    find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)
endif()


# 3. Explicit Source collection
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

if(ANDROID)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/Platform/Windows/.*")
else()
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/Platform/Android/.*")
endif()

add_library(${LIBRARY_NAME} STATIC ${ENGINE_SOURCES})

# 4. Target Compile Definitions (Restored and modernized)
target_compile_definitions(${LIBRARY_NAME} PUBLIC
    "$<$<PLATFORM_ID:Windows>:PROTO_PLATFORM_WINDOWS;GLFW_INCLUDE_NONE>"
    "$<$<PLATFORM_ID:Android>:PROTO_PLATFORM_ANDROID;VK_USE_PLATFORM_ANDROID_KHR>"
    "$<$<CONFIG:Debug>:PROTO_DEBUG>"
    "VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS" # Handles out-of-date swapchains gracefully
)

# --- Generate the Configuration Header File ---
# This takes your template file and builds the concrete header file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/src/Proto/ShaderConfig.hpp"
)

# 5. Global Include Boundaries (Unified)
if(ANDROID)
    set(TINYGLTF_INCLUDE_DIR "${tinygltf_SOURCE_DIR}")
    set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()

target_include_directories(${LIBRARY_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Proto"
        "${TINYGLTF_INCLUDE_DIR}"
        $<$<BOOL:${ANDROID}>:${STB_INCLUDE_DIR};${vulkan_headers_SOURCE_DIR}/include>
    PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        "${CMAKE_CURRENT_BINARY_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/src/Proto"
)

# 6. Precompiled Header Configuration
# Passing an absolute path guarantees the build generator discovers the header immediately
target_precompile_headers(${LIBRARY_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/protopch.hpp")

# 7. Dependency Link Mapping
target_link_libraries(${LIBRARY_NAME} PUBLIC
    Vulkan::Vulkan
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    glm::glm
    imguizmo::imguizmo
    imgui::imgui
    Jolt::Jolt
)

if(ANDROID)
    target_link_libraries(${LIBRARY_NAME} PRIVATE game-activity::game-activity_static android log)
else()
    target_link_libraries(${LIBRARY_NAME} PUBLIC glfw KTX::ktx tinyobjloader::tinyobjloader)
endif()

if(NOT ANDROID)
    # --- Unified Multi-Shader Slang Compilation Pipeline (Dynamic Entry Points)(Incremental) ---
    find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

    set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
    set(SHADER_OUT_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")

    file(GLOB_RECURSE SLANG_SOURCES CONFIGURE_DEPENDS "${SHADER_SRC_DIR}/*.slang")
    set(ALL_COMPILED_SPVS "")

    foreach(SHADER_FILE ${SLANG_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
        set(OUTPUT_SPV "${SHADER_OUT_DIR}/${SHADER_NAME}.spv")

        # Parse @entry: tags from the top 10 lines of the shader
        file(STRINGS "${SHADER_FILE}" SHADER_HEADERS LIMIT_COUNT 10 REGEX ".*//[ \t]*@entry:")
        set(DYNAMIC_ENTRY_ARGS "")

        if(SHADER_HEADERS)
            foreach(HEADER_LINE ${SHADER_HEADERS})
                string(REGEX REPLACE ".*@entry:[ \t]*([A-Za-z0-9_]+).*" "\\1" ENTRY_NAME "${HEADER_LINE}")
                string(STRIP "${ENTRY_NAME}" ENTRY_NAME)
                list(APPEND DYNAMIC_ENTRY_ARGS -entry "${ENTRY_NAME}")
            endforeach()
        else()
            set(DYNAMIC_ENTRY_ARGS -entry vertMain -entry fragMain)
        endif()

        # Build rule tracked per .spv output file
        add_custom_command(
            OUTPUT "${OUTPUT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
            COMMAND ${SLANGC_EXECUTABLE} "${SHADER_FILE}"
                    -target spirv
                    -profile spirv_1_5
                    -emit-spirv-directly
                    -fvk-use-entrypoint-name
                    ${DYNAMIC_ENTRY_ARGS}
                    -o "${OUTPUT_SPV}"
            DEPENDS "${SHADER_FILE}"
            COMMENT "Compiling Slang shader: ${SHADER_NAME}.slang -> ${SHADER_NAME}.spv"
            VERBATIM
        )

        list(APPEND ALL_COMPILED_SPVS "${OUTPUT_SPV}")
    endforeach()

    # Target depends on the list of .spv files; Ninja only runs rules whose .slang is newer than its .spv
    add_custom_target(${LIBRARY_NAME}Shaders ALL DEPENDS ${ALL_COMPILED_SPVS})
    add_dependencies(${LIBRARY_NAME} ${LIBRARY_NAME}Shaders)
endif()