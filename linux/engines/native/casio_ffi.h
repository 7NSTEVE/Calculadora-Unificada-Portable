#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(CASIO_NATIVE_SHARED)
#define CASIO_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CASIO_EXPORT __attribute__((visibility("default")))
#else
#define CASIO_EXPORT
#endif

CASIO_EXPORT void* casio_alloc(size_t size);
CASIO_EXPORT void casio_free(void* ptr);

// ============================================================================
// nX-U8 Emulators (fx-570LA X ClassWiz = 4, fx-570VN PLUS = 3)
// ============================================================================
typedef struct CasioNxHandle CasioNxHandle;

CASIO_EXPORT CasioNxHandle* casio_nx_create(int hardware_id, const uint8_t* rom_data, size_t rom_size);
CASIO_EXPORT CasioNxHandle* casio_nx_create_from_file(int hardware_id, const char* rom_path);
CASIO_EXPORT void casio_nx_destroy(CasioNxHandle* handle);

CASIO_EXPORT void casio_nx_power_on(CasioNxHandle* handle);
CASIO_EXPORT void casio_nx_advance(CasioNxHandle* handle, uint32_t ms);
CASIO_EXPORT void casio_nx_key_down(CasioNxHandle* handle, uint32_t matrix_code);
CASIO_EXPORT void casio_nx_key_up(CasioNxHandle* handle);
CASIO_EXPORT int casio_nx_tap_key(CasioNxHandle* handle, uint32_t matrix_code, uint32_t hold_ms, uint32_t settle_ms);

CASIO_EXPORT int casio_nx_screen_width(const CasioNxHandle* handle);
CASIO_EXPORT int casio_nx_screen_height(const CasioNxHandle* handle);
CASIO_EXPORT int casio_nx_screen_mode(const CasioNxHandle* handle);
CASIO_EXPORT int casio_nx_screen_contrast(const CasioNxHandle* handle);

// Renders top-down RGBA8888 dot matrix and outputs row-0 indicator bitmask:
// bit 0:S, 1:A, 2:M, 3:STO, 4:Math, 5:D, 6:R, 7:G, 8:FIX, 9:SCI, 10:DOWN, 11:UP
// Returns >0 if copied, 0 if unchanged, <0 on invalid args.
CASIO_EXPORT int casio_nx_render_rgba(
    CasioNxHandle* handle,
    uint8_t* out_rgba,
    int out_size,
    uint32_t* out_indicators,
    int force_copy);

// ============================================================================
// fx-CG50 PRIZM Emulator (SH7305 / SH-4A + R61524 384x216 RGB565)
// ============================================================================
typedef struct CasioCg50Handle CasioCg50Handle;

CASIO_EXPORT CasioCg50Handle* casio_cg50_create_from_dir(const char* dir_path);
CASIO_EXPORT void casio_cg50_destroy(CasioCg50Handle* handle);

// Returns current state: 0=Empty, 1=Running, 2=Sleeping, 3=Halted.
CASIO_EXPORT int casio_cg50_step(CasioCg50Handle* handle, uint64_t run_budget, uint64_t sleep_cycles);

CASIO_EXPORT int casio_cg50_key_down(CasioCg50Handle* handle, uint16_t matrix_code);
CASIO_EXPORT void casio_cg50_key_up(CasioCg50Handle* handle, uint16_t matrix_code);
CASIO_EXPORT void casio_cg50_release_all_keys(CasioCg50Handle* handle);

CASIO_EXPORT int casio_cg50_get_stats(
    const CasioCg50Handle* handle,
    uint64_t* out_instructions,
    uint64_t* out_cycles,
    uint32_t* out_pc,
    uint64_t* out_lcd_writes);

// Renders 384x216 top-down RGBA8888 framebuffer (out_size >= 331776).
// Returns 1 if active framebuffer was rendered, 0 if blank/not yet available, <0 on error.
CASIO_EXPORT int casio_cg50_render_rgba(
    CasioCg50Handle* handle,
    uint8_t* out_rgba,
    int out_size);

CASIO_EXPORT size_t casio_cg50_export_state(const CasioCg50Handle* handle, uint8_t* out_buf, size_t out_size);
CASIO_EXPORT int casio_cg50_import_state(CasioCg50Handle* handle, const uint8_t* buf, size_t size);

#ifdef __cplusplus
}
#endif
