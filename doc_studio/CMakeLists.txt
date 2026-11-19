cmake_minimum_required(VERSION 3.20)
project(DocumentStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(DOC_CORE_SOURCES
    core/spatial_index.cpp
    core/page_layout.cpp
    core/zip_container.cpp
    core/document_model.cpp
    core/command_stack.cpp
    core/cudoc_storage.cpp
    core/ink_engine.cpp
    core/notebook_vault.cpp
    core/audio_timeline.cpp
    math/engineering_cas.cpp
    circuit/mna_solver.cpp
    bridge/calc_bridge.cpp
    pdf/pdf_parser.cpp
    pdf/pdf_writer.cpp
    pdf/winrt_adapters.cpp
    pdf/pdf_signer.cpp
    pdf/interop_openxml.cpp
    pdf/table_interop.cpp
    render/d2d_renderer.cpp
)

add_library(DocumentCore STATIC ${DOC_CORE_SOURCES})
target_compile_definitions(DocumentCore PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(DocumentCore PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(DocumentCore PUBLIC
    ZLIB::ZLIB
    nlohmann_json::nlohmann_json
    d2d1
    dwrite
    windowscodecs
    crypt32
    ole32
    comdlg32
    shell32
    user32
    gdi32
    advapi32
    bcrypt
    winmm
    ws2_32
    runtimeobject
)

set(ACADEMIC_ENABLE_HORARIOS_TEC ON CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../academic/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../academic" "${CMAKE_CURRENT_BINARY_DIR}/academic_build")
endif()

add_executable(DocumentStudio WIN32 app/main_studio.cpp)
target_compile_definitions(DocumentStudio PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN DOC_STUDIO_HAS_ACADEMIC=1)
if(MSVC)
    target_compile_options(DocumentStudio PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(DocumentStudio PRIVATE
    DocumentCore
    academic_tec_digital
    academic_horarios_tec
    comctl32
    gdiplus
)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
foreach(_crt_dll IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    if(_crt_dll MATCHES ".*(msvcp140|vcruntime140).*\\.dll$")
        add_custom_command(TARGET DocumentStudio POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_crt_dll}"
                "$<TARGET_FILE_DIR:DocumentStudio>"
        )
    endif()
endforeach()

enable_testing()
add_executable(DocumentCoreTest tests/document_core_test.cpp)
target_compile_definitions(DocumentCoreTest PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(DocumentCoreTest PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(DocumentCoreTest PRIVATE DocumentCore)

add_test(
    NAME DocumentCoreTest
    COMMAND DocumentCoreTest "${CMAKE_CURRENT_SOURCE_DIR}/../manuales"
)
