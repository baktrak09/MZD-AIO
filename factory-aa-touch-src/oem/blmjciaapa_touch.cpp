// Touchscreen-only OEM resolver for Mazda Connect 70.00.335C.
// Firmware-70 symbol values with runtime executable-segment checks.
// Binary identity and function signatures still require validation.

#define LOG_TAG "BLM_TOUCH"
#include "../log.h"
#include "blmjciaapa.h"

#include <stdint.h>
#include <link.h>
#include <string.h>

namespace {

namespace fw70 {
constexpr uintptr_t Singleton_AapProc_GetInstance = 0x0005D154;
constexpr uintptr_t AapProc_GetVideoManager       = 0x000788E4;
constexpr uintptr_t AapProc_GetRaceAap            = 0x00078890;
constexpr uintptr_t VideoManager_IsAAVideoInFocus = 0x000ABDA8;
constexpr uintptr_t RaceAap_SendTouchInput        = 0x00086BBC;
}

struct LibraryInfo {
    uintptr_t symbol_value;
    uintptr_t address;
};

int find_blm(struct dl_phdr_info *info, size_t, void *data)
{
    if (!info || !info->dlpi_name)
        return 0;

    if (strcmp(info->dlpi_name,
               "/jci/aapa/blmjciaapa.so") != 0)
        return 0;

    LibraryInfo *result = static_cast<LibraryInfo *>(data);
    uintptr_t bias = static_cast<uintptr_t>(info->dlpi_addr);

    if (result->symbol_value > UINTPTR_MAX - bias)
        return 1;

    uintptr_t target = bias + result->symbol_value;

    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];

        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X))
            continue;

        uintptr_t vaddr = static_cast<uintptr_t>(ph.p_vaddr);
        uintptr_t memsz = static_cast<uintptr_t>(ph.p_memsz);

        if (vaddr > UINTPTR_MAX - bias)
            continue;

        uintptr_t begin = bias + vaddr;

        if (target >= begin && target - begin < memsz) {
            result->address = target;
            break;
        }
    }

    return 1;
}

void *resolve_touch_function(uintptr_t symbol_value)
{
    LibraryInfo info = {symbol_value, 0};
    dl_iterate_phdr(find_blm, &info);

    return reinterpret_cast<void *>(info.address);
}

} // namespace

void *Singleton_AapProc_GetInstance(void)
{
    using Fn = void *(*)();
    Fn fn = reinterpret_cast<Fn>(
        resolve_touch_function(fw70::Singleton_AapProc_GetInstance));
    return fn ? fn() : nullptr;
}

void *AapProc_GetVideoManager(void *self)
{
    if (!self) return nullptr;
    using Fn = void *(*)(void *);
    Fn fn = reinterpret_cast<Fn>(
        resolve_touch_function(fw70::AapProc_GetVideoManager));
    return fn ? fn(self) : nullptr;
}

void *AapProc_GetRaceAap(void *self)
{
    if (!self) return nullptr;
    using Fn = void *(*)(void *);
    Fn fn = reinterpret_cast<Fn>(
        resolve_touch_function(fw70::AapProc_GetRaceAap));
    return fn ? fn(self) : nullptr;
}

int VideoManager_IsAAVideoInFocus(void *self)
{
    if (!self) return 0;
    using Fn = int (*)(void *);
    Fn fn = reinterpret_cast<Fn>(
        resolve_touch_function(fw70::VideoManager_IsAAVideoInFocus));
    return fn ? fn(self) : 0;
}

int RaceAap_SendTouchInput(void *self, AAP_TouchEvent *evt)
{
    if (!self || !evt) return -1;
    using Fn = int (*)(void *, AAP_TouchEvent *);
    Fn fn = reinterpret_cast<Fn>(
        resolve_touch_function(fw70::RaceAap_SendTouchInput));
    return fn ? fn(self, evt) : -1;
}
