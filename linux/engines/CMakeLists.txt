cmake_minimum_required(VERSION 3.16)
project(CasioLinuxEngines LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(CASIO_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    ABSOLUTE
)
set(CALCEMU_DIR "${CASIO_REPO_ROOT}/third_party/CalcEmuCore")
set(CASIO_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native")

if(NOT EXISTS "${CALCEMU_DIR}/include/calcemu.h")
    message(FATAL_ERROR "Cannot find in-repo CalcEmuCore/include/calcemu.h at ${CALCEMU_DIR}")
endif()
if(NOT EXISTS "${CASIO_NATIVE_DIR}/casio_ffi.cpp")
    message(FATAL_ERROR "Cannot find vendored native/casio_ffi.cpp at ${CASIO_NATIVE_DIR}")
endif()

find_package(Threads REQUIRED)

set(CALCEMU_SOURCES
    "${CALCEMU_DIR}/src/calcemu.cpp"
    "${CALCEMU_DIR}/src/Data/ModelInfo.cpp"
    "${CALCEMU_DIR}/src/Emulator.cpp"
    "${CALCEMU_DIR}/src/Logger.cpp"
    "${CALCEMU_DIR}/src/Chipset/Chipset.cpp"
    "${CALCEMU_DIR}/src/Chipset/CPU.cpp"
    "${CALCEMU_DIR}/src/Chipset/CPUArithmetic.cpp"
    "${CALCEMU_DIR}/src/Chipset/CPUControl.cpp"
    "${CALCEMU_DIR}/src/Chipset/CPULoadStore.cpp"
    "${CALCEMU_DIR}/src/Chipset/CPUPushPop.cpp"
    "${CALCEMU_DIR}/src/Chipset/InterruptSource.cpp"
    "${CALCEMU_DIR}/src/Chipset/MMU.cpp"
    "${CALCEMU_DIR}/src/Chipset/MMURegion.cpp"
    "${CALCEMU_DIR}/src/Chipset/Snapshot.cpp"
    "${CALCEMU_DIR}/src/Peripheral/BatteryBackedRAM.cpp"
    "${CALCEMU_DIR}/src/Peripheral/Keyboard.cpp"
    "${CALCEMU_DIR}/src/Peripheral/Miscellaneous.cpp"
    "${CALCEMU_DIR}/src/Peripheral/Peripheral.cpp"
    "${CALCEMU_DIR}/src/Peripheral/ROMWindow.cpp"
    "${CALCEMU_DIR}/src/Peripheral/Screen.cpp"
    "${CALCEMU_DIR}/src/Peripheral/StandbyControl.cpp"
    "${CALCEMU_DIR}/src/Peripheral/Timer.cpp"
)

set(CG50_SOURCES
    "${CASIO_NATIVE_DIR}/cg50/cg50_core/r61524.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_core/cg50_keyboard_matrix.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_core/cg50_peripheral_bus.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_core/sh_exception.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_core/sh_interrupt.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_native_core.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_native_runtime.cpp"
    "${CASIO_NATIVE_DIR}/cg50/cg50_trace.cpp"
)

add_library(CasioLinuxEngines STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/unified_engines.cpp"
    "${CASIO_NATIVE_DIR}/casio_ffi.cpp"
    ${CALCEMU_SOURCES}
    ${CG50_SOURCES}
)

target_include_directories(CasioLinuxEngines
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CASIO_NATIVE_DIR}"
    PRIVATE
        "${CALCEMU_DIR}/include"
        "${CALCEMU_DIR}/compat"
        "${CALCEMU_DIR}/src"
        "${CASIO_NATIVE_DIR}/cg50"
)

target_compile_definitions(CasioLinuxEngines
    PRIVATE
        CASIO_SOURCE_REPO_DIR="${CASIO_REPO_ROOT}"
)

target_compile_features(CasioLinuxEngines PUBLIC cxx_std_17)

target_link_libraries(CasioLinuxEngines
    PUBLIC
        Threads::Threads
)

# Keep third-party emulator sources clean under GCC 15 without editing external files,
# while keeping strict warnings enabled on our own unified_engines.cpp.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/unified_engines.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic"
    )
    set_source_files_properties(
        ${CALCEMU_SOURCES} ${CG50_SOURCES} "${CASIO_NATIVE_DIR}/casio_ffi.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-function;-Wno-unused-result"
    )
endif()

option(CASIO_ENGINES_BUILD_TESTS "Build CasioLinuxEngines unit & integration tests" ON)
if(CASIO_ENGINES_BUILD_TESTS AND (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR BUILD_TESTING))
    include(CTest)
    enable_testing()
    add_executable(test_linux_engines
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_linux_engines.cpp"
    )
    target_link_libraries(test_linux_engines PRIVATE CasioLinuxEngines)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_linux_engines PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME test_linux_engines COMMAND test_linux_engines)
endif()
