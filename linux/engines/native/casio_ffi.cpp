#include "casio_ffi.h"

#include "calcemu.h"
#include "cg50_native_core.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

bool ReadBinaryFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    long len = std::ftell(fp);
    if (len <= 0 || std::fseek(fp, 0, SEEK_SET) != 0) {
        std::fclose(fp);
        return false;
    }
    out.resize(static_cast<size_t>(len));
    size_t got = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    if (got != out.size()) {
        out.clear();
        return false;
    }
    return true;
}

std::string JoinPath(const std::string& dir, const char* file)
{
    if (dir.empty()) return std::string(file);
    char back = dir.back();
    if (back == '/' || back == '\\') return dir + file;
    return dir + "/" + file;
}

uint32_t DecodeNxIndicators(int hardwareId, const uint8_t* raw, size_t len)
{
    uint32_t mask = 0;
    if (!raw) return 0;
    if (hardwareId == CALCEMU_HW_CLASSWIZ) {
        if (len < 32) return 0;
        if (raw[0x00] & 0x01u) mask |= (1u << 0);  // S
        if (raw[0x01] & 0x01u) mask |= (1u << 1);  // A
        if (raw[0x02] & 0x01u) mask |= (1u << 2);  // M
        if (raw[0x03] & 0x01u) mask |= (1u << 3);  // STO
        if (raw[0x05] & 0x01u) mask |= (1u << 4);  // Math
        if (raw[0x06] & 0x01u) mask |= (1u << 5);  // D
        if (raw[0x07] & 0x01u) mask |= (1u << 6);  // R
        if (raw[0x08] & 0x01u) mask |= (1u << 7);  // G
        if (raw[0x09] & 0x01u) mask |= (1u << 8);  // FIX
        if (raw[0x0A] & 0x01u) mask |= (1u << 9);  // SCI
        if (raw[0x11] & 0x01u) mask |= (1u << 10); // DOWN
        if (raw[0x12] & 0x01u) mask |= (1u << 11); // UP
    } else if (hardwareId == CALCEMU_HW_ES_PLUS) {
        if (len < 16) return 0;
        if (raw[0x00] & 0x10u) mask |= (1u << 0);  // S
        if (raw[0x00] & 0x04u) mask |= (1u << 1);  // A
        if (raw[0x01] & 0x10u) mask |= (1u << 2);  // M
        if (raw[0x01] & 0x02u) mask |= (1u << 3);  // STO
        if (raw[0x0A] & 0x40u) mask |= (1u << 4);  // Math
        if (raw[0x07] & 0x20u) mask |= (1u << 5);  // D
        if (raw[0x07] & 0x02u) mask |= (1u << 6);  // R
        if (raw[0x08] & 0x10u) mask |= (1u << 7);  // G
        if (raw[0x08] & 0x01u) mask |= (1u << 8);  // FIX
        if (raw[0x09] & 0x20u) mask |= (1u << 9);  // SCI
        if (raw[0x0A] & 0x08u) mask |= (1u << 10); // DOWN
        if (raw[0x0B] & 0x80u) mask |= (1u << 11); // UP
    }
    return mask;
}

inline void WriteRgb565ToRgba(uint16_t px, uint8_t* dst)
{
    uint8_t r5 = static_cast<uint8_t>((px >> 11) & 0x1Fu);
    uint8_t g6 = static_cast<uint8_t>((px >> 5) & 0x3Fu);
    uint8_t b5 = static_cast<uint8_t>(px & 0x1Fu);
    dst[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    dst[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    dst[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    dst[3] = 255u;
}

} // namespace

struct CasioNxHandle {
    CalcEmu* emu = nullptr;
    int hardwareId = 0;
    uint32_t lastIndicators = 0xFFFFFFFFu;
    int lastMode = -1;
    bool hasRenderedOnce = false;
    std::vector<uint8_t> rawBuf;
    std::vector<uint8_t> pixelBuf;
};

struct CasioCg50Handle {
    std::unique_ptr<Cg50NativeCore> core;
    std::vector<uint16_t> pressedKeys;
};

extern "C" {

void* casio_alloc(size_t size)
{
    return std::malloc(size == 0 ? 1 : size);
}

void casio_free(void* ptr)
{
    std::free(ptr);
}

CasioNxHandle* casio_nx_create(int hardware_id, const uint8_t* rom_data, size_t rom_size)
{
    if (!rom_data || rom_size == 0) return nullptr;
    CalcEmuModel model{};
    if (hardware_id == CALCEMU_HW_CLASSWIZ) {
        model.hardware_id = CALCEMU_HW_CLASSWIZ;
        model.real_hardware = 0;
        model.csr_mask = 0x0003;
        model.pd_value = 0x00;
    } else if (hardware_id == CALCEMU_HW_ES_PLUS) {
        model.hardware_id = CALCEMU_HW_ES_PLUS;
        model.real_hardware = 0;
        model.csr_mask = 0x0001;
        model.pd_value = 0x08;
    } else {
        return nullptr;
    }

    CalcEmu* emu = calcemu_create(&model, rom_data, rom_size);
    if (!emu) return nullptr;

    calcemu_power_on(emu);
    calcemu_advance(emu, 320);

    auto* h = new CasioNxHandle();
    h->emu = emu;
    h->hardwareId = hardware_id;
    h->rawBuf.resize(calcemu_screen_buffer_size(emu));
    h->pixelBuf.resize(static_cast<size_t>(calcemu_screen_width(emu)) *
                       static_cast<size_t>(calcemu_screen_rows(emu)));
    return h;
}

CasioNxHandle* casio_nx_create_from_file(int hardware_id, const char* rom_path)
{
    if (!rom_path) return nullptr;
    std::vector<uint8_t> rom;
    if (!ReadBinaryFile(rom_path, rom)) return nullptr;
    return casio_nx_create(hardware_id, rom.data(), rom.size());
}

void casio_nx_destroy(CasioNxHandle* handle)
{
    if (!handle) return;
    if (handle->emu) calcemu_destroy(handle->emu);
    delete handle;
}

void casio_nx_power_on(CasioNxHandle* handle)
{
    if (!handle || !handle->emu) return;
    calcemu_power_on(handle->emu);
    calcemu_advance(handle->emu, 120);
}

void casio_nx_advance(CasioNxHandle* handle, uint32_t ms)
{
    if (!handle || !handle->emu || ms == 0) return;
    calcemu_advance(handle->emu, ms);
}

void casio_nx_key_down(CasioNxHandle* handle, uint32_t matrix_code)
{
    if (!handle || !handle->emu) return;
    if ((matrix_code & 0xFFu) == 0xFFu) {
        calcemu_power_on(handle->emu);
        calcemu_advance(handle->emu, 80);
        return;
    }
    calcemu_key_down(handle->emu, static_cast<unsigned char>(matrix_code & 0xFFu));
}

void casio_nx_key_up(CasioNxHandle* handle)
{
    if (!handle || !handle->emu) return;
    calcemu_key_up(handle->emu);
}

int casio_nx_tap_key(CasioNxHandle* handle, uint32_t matrix_code, uint32_t hold_ms, uint32_t settle_ms)
{
    if (!handle || !handle->emu) return 0;
    if ((matrix_code & 0xFFu) == 0xFFu) {
        calcemu_power_on(handle->emu);
        if (settle_ms > 0) calcemu_advance(handle->emu, settle_ms);
        return 1;
    }
    return calcemu_tap_key(handle->emu,
                           static_cast<unsigned char>(matrix_code & 0xFFu),
                           hold_ms, settle_ms);
}

int casio_nx_screen_width(const CasioNxHandle* handle)
{
    return (handle && handle->emu) ? calcemu_screen_width(handle->emu) : 0;
}

int casio_nx_screen_height(const CasioNxHandle* handle)
{
    return (handle && handle->emu) ? calcemu_screen_rows(handle->emu) : 0;
}

int casio_nx_screen_mode(const CasioNxHandle* handle)
{
    return (handle && handle->emu) ? calcemu_screen_mode(handle->emu) : 0;
}

int casio_nx_screen_contrast(const CasioNxHandle* handle)
{
    return (handle && handle->emu) ? calcemu_screen_contrast(handle->emu) : 0;
}

int casio_nx_render_rgba(
    CasioNxHandle* handle,
    uint8_t* out_rgba,
    int out_size,
    uint32_t* out_indicators,
    int force_copy)
{
    if (!handle || !handle->emu || !out_rgba) return -1;
    const int w = calcemu_screen_width(handle->emu);
    const int h = calcemu_screen_rows(handle->emu);
    const int needed = w * h * 4;
    if (w <= 0 || h <= 0 || out_size < needed) return -1;

    if (handle->rawBuf.size() < calcemu_screen_buffer_size(handle->emu)) {
        handle->rawBuf.resize(calcemu_screen_buffer_size(handle->emu));
    }
    const size_t rawGot = calcemu_screen_buffer(
        handle->emu, handle->rawBuf.data(), handle->rawBuf.size());
    const uint32_t indMask = DecodeNxIndicators(
        handle->hardwareId, handle->rawBuf.data(), rawGot);
    if (out_indicators) *out_indicators = indMask;

    const int redraw = calcemu_take_redraw_flag(handle->emu);
    const int mode = calcemu_screen_mode(handle->emu);
    const bool changed = !handle->hasRenderedOnce ||
                         force_copy != 0 ||
                         redraw != 0 ||
                         indMask != handle->lastIndicators ||
                         mode != handle->lastMode;
    if (!changed) return 0;

    handle->hasRenderedOnce = true;
    handle->lastIndicators = indMask;
    handle->lastMode = mode;

    const size_t pxCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (handle->pixelBuf.size() < pxCount) handle->pixelBuf.resize(pxCount);
    calcemu_screen_pixels(handle->emu, handle->pixelBuf.data(), handle->pixelBuf.size());

    const uint8_t bgR = (handle->hardwareId == CALCEMU_HW_CLASSWIZ) ? 208 : 205;
    const uint8_t bgG = (handle->hardwareId == CALCEMU_HW_CLASSWIZ) ? 222 : 217;
    const uint8_t bgB = (handle->hardwareId == CALCEMU_HW_CLASSWIZ) ? 200 : 205;
    const uint8_t fgR = (handle->hardwareId == CALCEMU_HW_CLASSWIZ) ? 28 : 25;
    const uint8_t fgG = (handle->hardwareId == CALCEMU_HW_CLASSWIZ) ? 38 : 35;
    const uint8_t fgB = (handle->hardwareId == CALCEMU_HW_CLASSWIZ) ? 56 : 50;

    for (size_t i = 0; i < pxCount; ++i) {
        const bool on = handle->pixelBuf[i] != 0;
        uint8_t* p = out_rgba + i * 4;
        p[0] = on ? fgR : bgR;
        p[1] = on ? fgG : bgG;
        p[2] = on ? fgB : bgB;
        p[3] = 255u;
    }
    return 1;
}

CasioCg50Handle* casio_cg50_create_from_dir(const char* dir_path)
{
    if (!dir_path) return nullptr;
    const std::string dir(dir_path);

    std::vector<uint8_t> sysRom;
    std::vector<uint8_t> userRom;
    std::vector<uint8_t> c2Rom;
    std::vector<uint8_t> ilramSeed;

    if (!ReadBinaryFile(JoinPath(dir, "SystemROM.raw"), sysRom)) return nullptr;
    (void)ReadBinaryFile(JoinPath(dir, "UserROM.raw"), userRom);
    (void)ReadBinaryFile(JoinPath(dir, "C2ROM.raw"), c2Rom);
    (void)ReadBinaryFile(JoinPath(dir, "official-ILRAM-FD800000-4000.bin"), ilramSeed);

    auto core = std::make_unique<Cg50NativeCore>();
    if (!ilramSeed.empty()) {
        core->SetIlramSeed(ilramSeed.data(), ilramSeed.size());
    }
    if (!core->LoadFirmwareParts(
            sysRom.data(), sysRom.size(),
            userRom.empty() ? nullptr : userRom.data(), userRom.size())) {
        return nullptr;
    }
    if (!c2Rom.empty()) {
        core->LoadC2Rom(c2Rom.data(), c2Rom.size());
    }

    auto* h = new CasioCg50Handle();
    h->core = std::move(core);
    return h;
}

void casio_cg50_destroy(CasioCg50Handle* handle)
{
    delete handle;
}

int casio_cg50_step(CasioCg50Handle* handle, uint64_t run_budget, uint64_t sleep_cycles)
{
    if (!handle || !handle->core) return 0;
    auto& core = *handle->core;

    if (core.GetState() == Cg50NativeCore::State::Running && run_budget > 0) {
        core.RunInstructions(run_budget);
    }
    if (core.GetState() == Cg50NativeCore::State::Sleeping && sleep_cycles > 0) {
        constexpr uint64_t kChunk = 2048;
        uint64_t rem = sleep_cycles;
        while (rem > 0) {
            const uint64_t c = rem < kChunk ? rem : kChunk;
            if (core.AdvanceCycles(c)) break;
            rem -= c;
        }
    }
    return static_cast<int>(core.GetState());
}

int casio_cg50_key_down(CasioCg50Handle* handle, uint16_t matrix_code)
{
    if (!handle || !handle->core) return 0;
    if (!handle->core->KeyDown(matrix_code)) return 0;
    if (std::find(handle->pressedKeys.begin(), handle->pressedKeys.end(), matrix_code) ==
        handle->pressedKeys.end()) {
        handle->pressedKeys.push_back(matrix_code);
    }
    return 1;
}

void casio_cg50_key_up(CasioCg50Handle* handle, uint16_t matrix_code)
{
    if (!handle || !handle->core) return;
    handle->core->KeyUp(matrix_code);
    auto it = std::remove(handle->pressedKeys.begin(), handle->pressedKeys.end(), matrix_code);
    handle->pressedKeys.erase(it, handle->pressedKeys.end());
}

void casio_cg50_release_all_keys(CasioCg50Handle* handle)
{
    if (!handle || !handle->core) return;
    auto copy = handle->pressedKeys;
    for (uint16_t k : copy) {
        handle->core->KeyUp(k);
    }
    handle->pressedKeys.clear();
}

int casio_cg50_get_stats(
    const CasioCg50Handle* handle,
    uint64_t* out_instructions,
    uint64_t* out_cycles,
    uint32_t* out_pc,
    uint64_t* out_lcd_writes)
{
    if (!handle || !handle->core) return -1;
    const auto& s = handle->core->GetStats();
    const auto& r = handle->core->GetRegisters();
    if (out_instructions) *out_instructions = s.instructions;
    if (out_cycles) *out_cycles = s.cycles;
    if (out_pc) *out_pc = r.pc;
    if (out_lcd_writes) *out_lcd_writes = handle->core->Peripherals().Lcd().ObservedWrites().size();
    return static_cast<int>(handle->core->GetState());
}

int casio_cg50_render_rgba(
    CasioCg50Handle* handle,
    uint8_t* out_rgba,
    int out_size)
{
    if (!handle || !handle->core || !out_rgba) return -1;
    constexpr int kW = static_cast<int>(cg50::R61524::kVisibleWidth);
    constexpr int kH = static_cast<int>(cg50::R61524::kVisibleHeight);
    if (out_size < kW * kH * 4) return -1;

    const auto& lcd = handle->core->Peripherals().Lcd();
    if (lcd.FramebufferAvailable()) {
        const uint16_t* fb = lcd.FramebufferData();
        if (fb != nullptr) {
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    const uint16_t px = fb[static_cast<size_t>(kH - 1 - y) * kW + (kW - 1 - x)];
                    WriteRgb565ToRgba(px, out_rgba + (static_cast<size_t>(y) * kW + x) * 4u);
                }
            }
            return 1;
        }
    }

    // Fallback to DMA scanout VRAM buffer (SAR0 or 0x08000000 / 0x08028800) if active
    uint32_t sar0 = 0;
    uint32_t vramBase = 0x08000000u;
    if (handle->core->Read32(0xFE008020u, sar0) &&
        (sar0 == 0x08000000u || sar0 == 0x08028800u)) {
        vramBase = sar0;
    }
    uint16_t testWord = 0;
    bool hasNonEmpty = false;
    if (handle->core->Read16(vramBase, testWord)) {
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                uint16_t px = 0xFFFFu;
                (void)handle->core->Read16(
                    vramBase + (static_cast<uint32_t>(kH - 1 - y) * kW + (kW - 1 - x)) * 2u, px);
                if (px != 0u && px != 0xFFFFu) hasNonEmpty = true;
                WriteRgb565ToRgba(px, out_rgba + (static_cast<size_t>(y) * kW + x) * 4u);
            }
        }
        return hasNonEmpty ? 1 : 0;
    }

    std::memset(out_rgba, 0xFF, static_cast<size_t>(kW * kH * 4));
    return 0;
}

size_t casio_cg50_export_state(const CasioCg50Handle* handle, uint8_t* out_buf, size_t out_size)
{
    if (!handle || !handle->core) return 0;
    std::vector<uint8_t> blob;
    if (!handle->core->ExportState(blob) || blob.empty()) return 0;
    if (!out_buf || out_size < blob.size()) return blob.size();
    std::memcpy(out_buf, blob.data(), blob.size());
    return blob.size();
}

int casio_cg50_import_state(CasioCg50Handle* handle, const uint8_t* buf, size_t size)
{
    if (!handle || !handle->core || !buf || size == 0) return 0;
    return handle->core->ImportState(buf, size) ? 1 : 0;
}

} // extern "C"
