file(MAKE_DIRECTORY "${DESTINATION}")
set(archive "${DESTINATION}/mesa3d-25.1.9-release-msvc.7z")
file(DOWNLOAD
    "https://github.com/pal1000/mesa-dist-win/releases/download/25.1.9/mesa3d-25.1.9-release-msvc.7z"
    "${archive}"
    EXPECTED_HASH SHA256=dd83c3c4cc4d7706c9e21b277388447a2fc5367b0912f9ee45621e36aeb3b7d2
    TLS_VERIFY ON TIMEOUT 180)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${archive}"
        "${ARCHITECTURE}/opengl32.dll" "${ARCHITECTURE}/libgallium_wgl.dll"
        "${ARCHITECTURE}/vulkan_lvp.dll" "${ARCHITECTURE}/${ICD_MANIFEST}" readme.txt
    WORKING_DIRECTORY "${DESTINATION}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not extract the Mesa renderer")
endif()
foreach(runtime opengl32.dll libgallium_wgl.dll vulkan_lvp.dll "${ICD_MANIFEST}")
    file(RENAME "${DESTINATION}/${ARCHITECTURE}/${runtime}" "${DESTINATION}/${runtime}")
endforeach()
file(REMOVE "${archive}")
