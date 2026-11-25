set(APPLICATION_NAME "ProtoApp")

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

if(ANDROID)
    find_package(game-activity REQUIRED CONFIG)
    add_library(${APPLICATION_NAME} SHARED ${APP_SOURCES})
    target_link_libraries(${APPLICATION_NAME} PRIVATE
        ProtoEngine
        -Wl,--whole-archive
        game-activity::game-activity_static
        -Wl,--no-whole-archive
        android
        log
    )
else()
    add_executable(${APPLICATION_NAME} ${APP_SOURCES})
    target_link_libraries(${APPLICATION_NAME} PRIVATE ProtoEngine)
    if(MSVC)
        set_target_properties(${APPLICATION_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()
endif()

target_include_directories(${APPLICATION_NAME} PRIVATE app)

# Restored and unified preprocessor defines
target_compile_definitions(${APPLICATION_NAME} PRIVATE
    "$<$<PLATFORM_ID:Windows>:PROTO_PLATFORM_WINDOWS>" # Activates the #ifdef macro layout block
    "$<$<PLATFORM_ID:Android>:PROTO_PLATFORM_ANDROID>"
)

# Forces IDEs to treat the root project folder as the active sandbox working directory
# Force the MSVC toolchain linker to extract the entry point out of the static library graph
if(MSVC)
    set_target_properties(${APPLICATION_NAME} PROPERTIES 
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    
endif()