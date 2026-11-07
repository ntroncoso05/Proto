set(APPLICATION_NAME "ProtoApp")

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(${APPLICATION_NAME} ${APP_SOURCES})

target_include_directories(${APPLICATION_NAME} PRIVATE app)
target_link_libraries(${APPLICATION_NAME} PRIVATE ProtoEngine)

# Restored and unified preprocessor defines
target_compile_definitions(${APPLICATION_NAME} PRIVATE
    "PROTO_PLATFORM_WINDOWS" # <--- CRITICAL FIX: Activates the #ifdef macro layout block
)

# Forces IDEs to treat the root project folder as the active sandbox working directory
# Force the MSVC toolchain linker to extract the entry point out of the static library graph
if(MSVC)
    set_target_properties(${APPLICATION_NAME} PROPERTIES 
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    
    # Tells the compiler to look into the ProtoEngine static library objects for main()
    #target_link_options(${APPLICATION_NAME} PRIVATE "/INCLUDE:main")
endif()

#source_group(TREE "${CMAKE_SOURCE_DIR}/${APPLICATION_NAME}" FILES ${Proto_SOURCES})