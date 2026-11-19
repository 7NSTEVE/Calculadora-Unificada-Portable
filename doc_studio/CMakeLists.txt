cmake_minimum_required(VERSION 3.20)
project(CuadernU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Unified pure C++ engine harvested from sibling CuadernU/nativo/tinta/nucleo
add_library(tinta_nucleo STATIC
    tinta_core/tinta.cpp
    tinta_core/elementos.cpp
    tinta_core/documento.cpp
    tinta_core/edicion.cpp
    tinta_core/formas.cpp
    tinta_core/archivo.cpp
    tinta_core/grabacion.cpp
    tinta_core/reproduccion.cpp
    tinta_core/lienzo_avanzado.cpp
    tinta_core/ingenieria.cpp
    tinta_core/circuitos.cpp
    tinta_core/matematicas_avanzadas.cpp
    tinta_core/importar.cpp
    tinta_core/ajustes.cpp
    tinta_core/api.cpp
    tinta_core/almacen.cpp
    tinta_core/buscador.cpp
    tinta_core/clip.cpp
    tinta_core/estado_sesion.cpp
    tinta_core/exportador.cpp
    tinta_core/foco.cpp
    tinta_core/ia_local.cpp
    tinta_core/matematicas.cpp
    tinta_core/pdf.cpp
    tinta_core/perspectiva.cpp
    tinta_core/simulador.cpp
    tinta_core/unidades.cpp
)
target_compile_features(tinta_nucleo PRIVATE cxx_std_20)
target_include_directories(tinta_nucleo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tinta_core")
if(MSVC)
    target_compile_options(tinta_nucleo PRIVATE /utf-8 /EHsc /W2)
endif()

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
    interop/notebook_migration_engine.cpp
    interop/printout_engine.cpp
    render/d2d_renderer.cpp
)

add_library(DocumentCore STATIC ${DOC_CORE_SOURCES})
target_compile_definitions(DocumentCore PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(DocumentCore PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(DocumentCore PUBLIC
    tinta_nucleo
    ZLIB::ZLIB
    nlohmann_json::nlohmann_json
    d2d1
    dwrite
    windowscodecs
    crypt32
    ole32
    oleaut32
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

set(CUADERNU_APP_SOURCES app/main_studio.cpp)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/cuadernu/cuadernu.rc")
    list(APPEND CUADERNU_APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/cuadernu/cuadernu.rc")
endif()

add_executable(CuadernU WIN32 ${CUADERNU_APP_SOURCES})
target_compile_definitions(CuadernU PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN DOC_STUDIO_HAS_ACADEMIC=1)
if(MSVC)
    target_compile_options(CuadernU PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(CuadernU PRIVATE
    DocumentCore
    academic_tec_digital
    academic_horarios_tec
    comctl32
    gdiplus
)

# Produce DocumentStudio.exe compatibility alias alongside official CuadernU.exe
add_custom_command(TARGET CuadernU POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:CuadernU>"
        "$<TARGET_FILE_DIR:CuadernU>/DocumentStudio.exe"
)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
foreach(_crt_dll IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    if(_crt_dll MATCHES ".*(msvcp140|vcruntime140).*\\.dll$")
        add_custom_command(TARGET CuadernU POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_crt_dll}"
                "$<TARGET_FILE_DIR:CuadernU>"
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tinta_core/tests/pruebas.cpp")
    add_executable(TintaNucleoTest tinta_core/tests/pruebas.cpp)
    target_link_libraries(TintaNucleoTest PRIVATE tinta_nucleo)
    if(MSVC)
        target_compile_options(TintaNucleoTest PRIVATE /utf-8 /EHsc /W2)
    endif()
    add_test(NAME TintaNucleoTest COMMAND TintaNucleoTest)
endif()

add_executable(CuadernuInteropTest tests/cuadernu_interop_test.cpp)
target_compile_definitions(CuadernuInteropTest PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(CuadernuInteropTest PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(CuadernuInteropTest PRIVATE DocumentCore tinta_nucleo)
add_test(NAME CuadernuInteropTest COMMAND CuadernuInteropTest)

add_executable(PrintoutTest tests/printout_test.cpp)
target_compile_definitions(PrintoutTest PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(PrintoutTest PRIVATE /W4 /utf-8 /EHsc)
endif()
target_link_libraries(PrintoutTest PRIVATE DocumentCore tinta_nucleo)
add_test(NAME PrintoutTest COMMAND PrintoutTest "${CMAKE_CURRENT_BINARY_DIR}/printout_fixtures")

