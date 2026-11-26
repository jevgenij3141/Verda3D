cmake_minimum_required(VERSION 3.20)
project(ThreeD1Editor VERSION 0.4.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "3D1 Editor uses Win32 to open its window.")
endif()
enable_language(RC)
configure_file(src/startup_art.rc.in startup_art.rc @ONLY)

add_executable(3D1Editor WIN32 src/main.cpp src/material_editor.cpp src/global_renderer.cpp
    src/lighting_dialog.cpp src/shadow_map.cpp src/scene_mesh.cpp src/material_library.cpp src/scene_io.cpp
    src/obj_io.cpp src/image_output.cpp src/animation.cpp src/mesh_modifiers.cpp src/mesh_cut.cpp
    src/video_export.cpp src/video_dialog.cpp src/texture_mesh_editor.cpp src/smart_unfold.cpp
    src/graphics_api.cpp src/startup_splash.cpp src/dynamics.cpp src/fluid.cpp src/extra_primitives.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/startup_art.rc")
set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/startup_art.rc" APPEND PROPERTY
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/startup-art.png")
set_target_properties(3D1Editor PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_definitions(3D1Editor PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VERDA_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(3D1Editor PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(3D1Editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(3D1Editor PRIVATE gdi32 user32 comdlg32 comctl32 ole32 windowscodecs uuid
    mfplat mfreadwrite mfuuid)

option(THREED1_BUNDLE_VULKAN "Bundle the Mesa Zink Vulkan backend" ON)
if(THREED1_BUNDLE_VULKAN)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(mesa_architecture x64)
        set(mesa_icd lvp_icd.x86_64.json)
    else()
        set(mesa_architecture x86)
        set(mesa_icd lvp_icd.x86.json)
    endif()
    set(mesa_directory "${CMAKE_CURRENT_BINARY_DIR}/vulkan")
    add_custom_command(
        OUTPUT "${mesa_directory}/opengl32.dll" "${mesa_directory}/libgallium_wgl.dll"
            "${mesa_directory}/vulkan_lvp.dll" "${mesa_directory}/${mesa_icd}" "${mesa_directory}/readme.txt"
        COMMAND "${CMAKE_COMMAND}" "-DDESTINATION=${mesa_directory}" "-DARCHITECTURE=${mesa_architecture}"
            "-DICD_MANIFEST=${mesa_icd}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleVulkan.cmake"
        COMMENT "Bundling the Mesa Zink Vulkan renderer"
        VERBATIM)
    add_custom_target(3D1VulkanRuntime DEPENDS
        "${mesa_directory}/opengl32.dll" "${mesa_directory}/libgallium_wgl.dll"
        "${mesa_directory}/vulkan_lvp.dll" "${mesa_directory}/${mesa_icd}" "${mesa_directory}/readme.txt")
    add_dependencies(3D1Editor 3D1VulkanRuntime)
    if(CMAKE_CONFIGURATION_TYPES)
        add_custom_command(TARGET 3D1Editor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${mesa_directory}" "$<TARGET_FILE_DIR:3D1Editor>/vulkan")
    endif()
endif()
