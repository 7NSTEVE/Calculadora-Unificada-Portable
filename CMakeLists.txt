cmake_minimum_required(VERSION 3.20)
project(CalculadoraUnificada LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CG50 engine switch: "gcal" (default Win32) | "native" (x64/Win32 ROM-native)
set(CG50_ENGINE "gcal" CACHE STRING "Motor CG50: gcal | native")
set_property(CACHE CG50_ENGINE PROPERTY STRINGS "gcal" "native")
message(STATUS "Motor CG50 seleccionado: ${CG50_ENGINE}")

# GCalAppIF exports stdcall names like _GCAI_CreateEmulator@28 (Win32 / i386).
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CG50_ENGINE STREQUAL "native")
    message(WARNING
        "Esta configuración es x64. GCal es 32-bit: usa -A Win32 (compilar.bat) o -DCG50_ENGINE=native. "
        "nX-U8 puede enlazar; el backend CG50 no cargará GCalAppIF.")
endif()

if(MSVC)
    if(CASIO_ENABLE_ASAN)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

set(EMU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CalcEmuCore")
option(CASIO_ALLOW_EXTERNAL_FALLBACK "Permitir respaldo fuera del repositorio para CalcEmuCore/ROMs" OFF)
option(CASIO_ENABLE_ASAN "Compilar pruebas/binarios con AddressSanitizer en MSVC" OFF)

if(CASIO_ENABLE_ASAN AND MSVC)
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
    message(STATUS "AddressSanitizer (MSVC /fsanitize=address) ACTIVADO")
endif()

if(NOT EXISTS "${EMU_ROOT}/include/calcemu.h" AND CASIO_ALLOW_EXTERNAL_FALLBACK)
    set(EMU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../CuadernU/CalcEmuCore")
endif()
if(NOT EXISTS "${EMU_ROOT}/include/calcemu.h")
    message(FATAL_ERROR
        "No se encontró calcemu.h. Vendée CalcEmuCore en "
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CalcEmuCore "
        "(include/calcemu.h).")
endif()

set(EMU_SOURCES
    "${EMU_ROOT}/src/calcemu.cpp"
    "${EMU_ROOT}/src/Emulator.cpp"
    "${EMU_ROOT}/src/Logger.cpp"
    "${EMU_ROOT}/src/Chipset/Chipset.cpp"
    "${EMU_ROOT}/src/Chipset/CPU.cpp"
    "${EMU_ROOT}/src/Chipset/CPUArithmetic.cpp"
    "${EMU_ROOT}/src/Chipset/CPUControl.cpp"
    "${EMU_ROOT}/src/Chipset/CPULoadStore.cpp"
    "${EMU_ROOT}/src/Chipset/CPUPushPop.cpp"
    "${EMU_ROOT}/src/Chipset/InterruptSource.cpp"
    "${EMU_ROOT}/src/Chipset/MMU.cpp"
    "${EMU_ROOT}/src/Chipset/MMURegion.cpp"
    "${EMU_ROOT}/src/Chipset/Snapshot.cpp"
    "${EMU_ROOT}/src/Data/ModelInfo.cpp"
    "${EMU_ROOT}/src/Peripheral/BatteryBackedRAM.cpp"
    "${EMU_ROOT}/src/Peripheral/Keyboard.cpp"
    "${EMU_ROOT}/src/Peripheral/Miscellaneous.cpp"
    "${EMU_ROOT}/src/Peripheral/Peripheral.cpp"
    "${EMU_ROOT}/src/Peripheral/ROMWindow.cpp"
    "${EMU_ROOT}/src/Peripheral/Screen.cpp"
    "${EMU_ROOT}/src/Peripheral/StandbyControl.cpp"
    "${EMU_ROOT}/src/Peripheral/Timer.cpp"
)

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${EMU_ROOT}/include"
    "${EMU_ROOT}/src"
    "${EMU_ROOT}/compat"
)

# ---- ROMs. ROMS_DIR o carpeta local roms/ ----
set(_roms_search "")
if(DEFINED ENV{ROMS_DIR} AND NOT "$ENV{ROMS_DIR}" STREQUAL "")
    list(APPEND _roms_search "$ENV{ROMS_DIR}")
endif()
if(ROMS_DIR)
    list(APPEND _roms_search "${ROMS_DIR}")
endif()
list(APPEND _roms_search
    "${CMAKE_CURRENT_SOURCE_DIR}/roms"
)
if(CASIO_ALLOW_EXTERNAL_FALLBACK)
    list(APPEND _roms_search
        "${CMAKE_CURRENT_SOURCE_DIR}/../CuadernU/tinta-ux-toolbar/Roms"
        "${CMAKE_CURRENT_SOURCE_DIR}/../CuadernU/CalculadorasCasio/Recursos"
    )
endif()

set(ROM_CW_FILE "")
set(ROM_ES_FILE "")
foreach(_dir IN LISTS _roms_search)
    if(ROM_CW_FILE STREQUAL "" AND EXISTS "${_dir}/fx991lax.rom")
        set(ROM_CW_FILE "${_dir}/fx991lax.rom")
    endif()
    if(ROM_CW_FILE STREQUAL "" AND EXISTS "${_dir}/fx991lax.bin")
        set(ROM_CW_FILE "${_dir}/fx991lax.bin")
    endif()
    if(ROM_ES_FILE STREQUAL "" AND EXISTS "${_dir}/fx570vnplus.rom")
        set(ROM_ES_FILE "${_dir}/fx570vnplus.rom")
    endif()
    if(ROM_ES_FILE STREQUAL "" AND EXISTS "${_dir}/fx570vnplus.bin")
        set(ROM_ES_FILE "${_dir}/fx570vnplus.bin")
    endif()
endforeach()

set(GENERATED_ROMS_RC "${CMAKE_CURRENT_BINARY_DIR}/generated_roms.rc")
file(WRITE "${GENERATED_ROMS_RC}" "/* generated_roms.rc */\n")
if(ROM_CW_FILE)
    file(TO_NATIVE_PATH "${ROM_CW_FILE}" _rom_cw_native)
    string(REPLACE "\\" "\\\\" _rom_cw_native "${_rom_cw_native}")
    file(APPEND "${GENERATED_ROMS_RC}" "3001 RCDATA \"${_rom_cw_native}\"\n")
    message(STATUS "ROM ClassWiz: ${ROM_CW_FILE}")
else()
    message(WARNING
        "ROM ClassWiz ausente (fx991lax.rom / .bin). Copia a roms/ o define ROMS_DIR. "
        "La app compila, pero fx-570LA X no arrancará el núcleo nX-U8.")
endif()
if(ROM_ES_FILE)
    file(TO_NATIVE_PATH "${ROM_ES_FILE}" _rom_es_native)
    string(REPLACE "\\" "\\\\" _rom_es_native "${_rom_es_native}")
    file(APPEND "${GENERATED_ROMS_RC}" "3002 RCDATA \"${_rom_es_native}\"\n")
    message(STATUS "ROM Natural ES: ${ROM_ES_FILE}")
else()
    message(WARNING
        "ROM Natural ES ausente (fx570vnplus.rom / .bin). Copia a roms/ o define ROMS_DIR.")
endif()

if(CG50_ENGINE STREQUAL "native")
    set(CG50_BACKEND_SOURCES
        third_party/rom-native-reference/rama-cg50_backend.cpp
        third_party/rom-native-reference/cg50_native_core.cpp
        third_party/rom-native-reference/cg50_native_runtime.cpp
        third_party/rom-native-reference/cg50_core/r61524.cpp
        third_party/rom-native-reference/cg50_core/cg50_keyboard_matrix.cpp
        third_party/rom-native-reference/cg50_core/cg50_peripheral_bus.cpp
        third_party/rom-native-reference/cg50_core/sh_exception.cpp
        third_party/rom-native-reference/cg50_core/sh_interrupt.cpp
        third_party/rom-native-reference/cg50_backend_file.cpp
    )
    set(CG50_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rom-native-reference"
    )
    set(CG50_CONTRACT_TEST_SOURCE
        third_party/rom-native-reference/rama-tests-cg50_backend_contract_test.cpp
    )
    set(CG50_CORE_TEST_SOURCES
        third_party/rom-native-reference/cg50_native_core.cpp
        third_party/rom-native-reference/cg50_native_runtime.cpp
        third_party/rom-native-reference/cg50_core/r61524.cpp
        third_party/rom-native-reference/cg50_core/cg50_keyboard_matrix.cpp
        third_party/rom-native-reference/cg50_core/cg50_peripheral_bus.cpp
        third_party/rom-native-reference/cg50_core/sh_exception.cpp
        third_party/rom-native-reference/cg50_core/sh_interrupt.cpp
    )
else()
    set(CG50_BACKEND_SOURCES
        cg50_backend.cpp
    )
    set(CG50_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set(CG50_CONTRACT_TEST_SOURCE
        tests/cg50_backend_contract_test.cpp
    )
endif()

add_executable(CalculadoraUnificada WIN32
    main.cpp
    fx991cw_host.cpp
    ${CG50_BACKEND_SOURCES}
    ${EMU_SOURCES}
    app.rc
    ${GENERATED_ROMS_RC}
)

target_compile_definitions(CalculadoraUnificada PRIVATE UNICODE _UNICODE)
target_include_directories(CalculadoraUnificada PRIVATE ${CG50_INCLUDE_DIRS})

if(MSVC)
    target_compile_options(CalculadoraUnificada PRIVATE /utf-8 /W4)
endif()

target_link_libraries(CalculadoraUnificada PRIVATE
    user32
    gdi32
    shell32
    comctl32
    shlwapi
    msimg32
    psapi
    gdiplus
    comdlg32
)

set_target_properties(CalculadoraUnificada PROPERTIES
    LINK_FLAGS "/MANIFEST:NO"
)

# Copy GCal runtime next to the exe when a local folder exists.
set(GCAL_RUNTIME_CANDIDATES
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/Release/runtime"
    "${CMAKE_CURRENT_BINARY_DIR}/Release/runtime"
)
if(DEFINED ENV{GCAL_RUNTIME_DIR})
    list(PREPEND GCAL_RUNTIME_CANDIDATES "$ENV{GCAL_RUNTIME_DIR}")
endif()
set(GCAL_RUNTIME_DIR "")
foreach(_cand IN LISTS GCAL_RUNTIME_CANDIDATES)
    if(EXISTS "${_cand}/GCalAppIF.dll")
        set(GCAL_RUNTIME_DIR "${_cand}")
        break()
    endif()
endforeach()
if(GCAL_RUNTIME_DIR)
    add_custom_command(TARGET CalculadoraUnificada POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:CalculadoraUnificada>/runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GCAL_RUNTIME_DIR}"
            "$<TARGET_FILE_DIR:CalculadoraUnificada>/runtime"
        COMMENT "Copiando runtime GCal junto al exe"
    )
    message(STATUS "Runtime GCal: ${GCAL_RUNTIME_DIR}")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/fx991cw/emulator/casioemu.exe")
    add_custom_command(TARGET CalculadoraUnificada POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:CalculadoraUnificada>/runtime/fx991cw"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/fx991cw"
            "$<TARGET_FILE_DIR:CalculadoraUnificada>/runtime/fx991cw"
        COMMENT "Copiando runtime fx-991CW (CasioEmuX) junto al exe"
    )
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/catalog")
add_custom_command(TARGET CalculadoraUnificada POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:CalculadoraUnificada>/catalog"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
        "$<TARGET_FILE_DIR:CalculadoraUnificada>/catalog"
    COMMENT "Copiando catalogo determinista CASIO junto al exe"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/manuales")
    add_custom_command(TARGET CalculadoraUnificada POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:CalculadoraUnificada>/manuales"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/manuales"
            "$<TARGET_FILE_DIR:CalculadoraUnificada>/manuales"
        COMMENT "Copiando manuales oficiales PDF junto al exe para version portable"
    )
endif()

foreach(_studio_bin IN ITEMS DocumentStudio.exe z.dll)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build-studio-x64/Release/${_studio_bin}")
        add_custom_command(TARGET CalculadoraUnificada POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/build-studio-x64/Release/${_studio_bin}"
                "$<TARGET_FILE_DIR:CalculadoraUnificada>/${_studio_bin}"
            COMMENT "Copiando ${_studio_bin} (x64) junto al exe para version portable"
        )
    endif()
endforeach()

file(GLOB _studio_crt_dlls "${CMAKE_CURRENT_SOURCE_DIR}/build-studio-x64/Release/*140*.dll")
foreach(_crt_bin IN LISTS _studio_crt_dlls)
    add_custom_command(TARGET CalculadoraUnificada POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_crt_bin}"
            "$<TARGET_FILE_DIR:CalculadoraUnificada>"
        COMMENT "Copiando runtime CRT x64 de DocumentStudio junto al exe portable"
    )
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(Cg50BackendContractTest
        ${CG50_CONTRACT_TEST_SOURCE}
        ${CG50_BACKEND_SOURCES}
    )
    target_include_directories(Cg50BackendContractTest PRIVATE ${CG50_INCLUDE_DIRS})
    target_compile_definitions(Cg50BackendContractTest PRIVATE UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(Cg50BackendContractTest PRIVATE /utf-8 /W4)
    endif()
    target_link_libraries(Cg50BackendContractTest PRIVATE user32 psapi)
    add_test(NAME Cg50BackendContract COMMAND Cg50BackendContractTest)

    add_executable(UiCoreModulesTest
        tests/ui_core_modules_test.cpp
        ${EMU_SOURCES}
    )
    target_compile_definitions(UiCoreModulesTest PRIVATE
        UNICODE
        _UNICODE
        CASIO_REPO_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(UiCoreModulesTest PRIVATE /utf-8 /W4)
    endif()
    target_link_libraries(UiCoreModulesTest PRIVATE user32 gdi32 gdiplus shlwapi ole32 shell32 comdlg32)
    add_test(NAME UiCoreModules COMMAND UiCoreModulesTest)

    add_executable(PdfViewerStressTest
        tests/pdf_viewer_stress_test.cpp
    )
    target_compile_definitions(PdfViewerStressTest PRIVATE
        UNICODE
        _UNICODE
        CASIO_REPO_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(PdfViewerStressTest PRIVATE /utf-8 /W4)
    endif()
    target_link_libraries(PdfViewerStressTest PRIVATE user32 gdi32 gdiplus shlwapi ole32 shell32 comdlg32 psapi)
    add_test(NAME PdfViewerStress COMMAND PdfViewerStressTest)

    # Unit tests for the native SH7305 core: FPU (banco F completo), INTC/TMU/
    # RTC/WDT, UTLB/alineación y save-state + R61524. Solo en el motor nativo.
    if(CG50_ENGINE STREQUAL "native")
        foreach(_test
                cg50_native_fpu_test
                cg50_native_intc_tmu_rtc_test
                cg50_native_state_test
                cg50_native_r61524_test
                cg50_native_exceptions_test
                cg50_native_power_test)
            add_executable(${_test} tests/${_test}.cpp ${CG50_CORE_TEST_SOURCES})
            target_include_directories(${_test} PRIVATE ${CG50_INCLUDE_DIRS})
            if(MSVC)
                target_compile_options(${_test} PRIVATE /utf-8 /W4)
            endif()
            add_test(NAME ${_test} COMMAND ${_test})
        endforeach()
    endif()

    # test_cpu_step.cpp talks to GCal DLLs; it is not a hermetic unit test.
    add_executable(WindowCommand tools/window-command/main.cpp)
    if(MSVC)
        target_compile_options(WindowCommand PRIVATE /utf-8 /W4)
    endif()
    target_link_libraries(WindowCommand PRIVATE user32)

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(GCalHostProbe tools/gcal-host-probe/main.cpp)
        if(MSVC)
            target_compile_options(GCalHostProbe PRIVATE /utf-8 /W4)
            target_link_options(GCalHostProbe PRIVATE "/MAP")
        endif()
    endif()

    add_executable(InspectGui tools/inspect_gui.cpp)
    if(MSVC)
        target_compile_options(InspectGui PRIVATE /utf-8 /W4)
    endif()
    target_link_libraries(InspectGui PRIVATE user32 gdi32 gdiplus)
endif()
