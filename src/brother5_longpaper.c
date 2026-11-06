#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

/* SANE type definitions */
typedef int SANE_Status;
typedef int SANE_Word;
typedef int SANE_Int;
typedef char SANE_Char;
typedef const SANE_Char* SANE_String_Const;
typedef void* SANE_Handle;
typedef int SANE_Bool;
typedef int SANE_Unit;
typedef int SANE_Value_Type;
typedef int SANE_Constraint_Type;
typedef int SANE_Action;

typedef struct {
    SANE_Word min;
    SANE_Word max;
    SANE_Word quant;
} SANE_Range;

typedef struct {
    SANE_String_Const name;
    SANE_String_Const vendor;
    SANE_String_Const model;
    SANE_String_Const type;
} SANE_Device;

typedef struct {
    int format;
    int last_frame;
    int bytes_per_line;
    int pixels_per_line;
    int lines;
    int depth;
} SANE_Parameters;

typedef struct {
    SANE_String_Const name;
    SANE_String_Const title;
    SANE_String_Const desc;
    SANE_Value_Type type;
    SANE_Unit unit;
    SANE_Int size;
    SANE_Int cap;
    SANE_Constraint_Type constraint_type;
    union {
        const SANE_String_Const *string_list;
        const SANE_Word *word_list;
        const SANE_Range *range;
    } constraint;
} SANE_Option_Descriptor;

#define SANE_STATUS_GOOD 0
#define SANE_FIX_MM(mm) ((SANE_Word)((mm) * 65536.0))
#define LONG_PAPER_MAX_MM 1828.8 /* Hardware limit: 72 inches = 1828.8 mm */
#define LONG_PAPER_MAX_HUNDREDTH_MM 182880 /* 1828.8 mm in units of 0.01 mm */

static void *g_real_lib_handle = NULL;
static void *g_core_lib_handle = NULL;
static int g_debug = -1;
static int g_long_paper_active = 0;

static int is_debug_enabled(void) {
    if (g_debug == -1) {
        g_debug = (getenv("BROTHER5_DEBUG") != NULL) ? 1 : 0;
    }
    return g_debug;
}

static void *get_real_symbol(const char *name) {
    void *sym = dlsym(RTLD_NEXT, name);
    if (!sym) {
        if (!g_real_lib_handle) {
            const char *paths[] = {
                "/usr/lib/sane/libsane-brother5-real.so.1",
                "/usr/lib/sane/libsane-brother5-real.so",
                "/usr/lib/sane/libsane-brother5.so.1",
                "/usr/lib/sane/libsane-brother5.so",
                NULL
            };
            for (int i = 0; paths[i]; i++) {
                g_real_lib_handle = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL);
                if (g_real_lib_handle) {
                    if (is_debug_enabled()) {
                        fprintf(stderr, "[brother5-longpaper] Loaded real backend from %s\n", paths[i]);
                    }
                    break;
                }
            }
        }
        if (g_real_lib_handle) {
            sym = dlsym(g_real_lib_handle, name);
        }
    }
    return sym;
}

static void *get_core_symbol(const char *name) {
    if (!g_core_lib_handle) {
        g_core_lib_handle = dlopen("libLxBsScanCoreApi.so.3", RTLD_NOLOAD | RTLD_LAZY);
        if (!g_core_lib_handle) {
            g_core_lib_handle = dlopen("/usr/lib/libLxBsScanCoreApi.so.3", RTLD_NOLOAD | RTLD_LAZY);
        }
    }
    if (g_core_lib_handle) {
        return dlsym(g_core_lib_handle, name);
    }
    return NULL;
}

static void patch_range_y(void) {
    void *handle = g_real_lib_handle;
    if (!handle) {
        handle = dlopen("libsane-brother5-real.so.1", RTLD_NOLOAD | RTLD_GLOBAL);
    }
    if (!handle) {
        handle = dlopen("/usr/lib/sane/libsane-brother5-real.so.1", RTLD_NOLOAD | RTLD_GLOBAL);
    }
    if (!handle) {
        handle = dlopen("/usr/lib/sane/libsane-brother5.so.1", RTLD_NOLOAD | RTLD_GLOBAL);
    }

    if (handle) {
        SANE_Range *rY = (SANE_Range *)dlsym(handle, "rangeYmm");
        if (rY) {
            SANE_Word new_max = SANE_FIX_MM(LONG_PAPER_MAX_MM);
            if (rY->max != new_max) {
                if (is_debug_enabled()) {
                    fprintf(stderr, "[brother5-longpaper] Patching rangeYmm->max: %.2f mm -> %.2f mm\n",
                            (double)rY->max / 65536.0, LONG_PAPER_MAX_MM);
                }
                rY->max = new_max;
            }
        }
    }
}

static void patch_dev_caps(SANE_Handle handle) {
    if (!handle) return;
    char *pDev = (char *)handle;
    char *pCaps = *(char **)(pDev + 0x450);
    if (pCaps) {
        int *h_adf = (int *)(pCaps + 0x58c);
        if (*h_adf < LONG_PAPER_MAX_HUNDREDTH_MM) {
            if (is_debug_enabled()) {
                fprintf(stderr, "[brother5-longpaper] Patching adf_max_height: %d -> %d\n",
                        *h_adf, LONG_PAPER_MAX_HUNDREDTH_MM);
            }
            *h_adf = LONG_PAPER_MAX_HUNDREDTH_MM;
        }

        /* Patch LongPaper capabilities in pCaps:
         * 0x124: longPaperSupport (2 = SUPPORTED)
         * 0x12c: longPaperMaxLength simplex (182880 = 1828.8 mm)
         * 0x134: longPaperMaxLength duplex (182880)
         */
        int *pLongSupport = (int *)(pCaps + 0x124);
        int *pLongMaxSimplex = (int *)(pCaps + 0x12c);
        int *pLongMaxDuplex = (int *)(pCaps + 0x134);

        if (*pLongSupport != 2 || *pLongMaxSimplex < LONG_PAPER_MAX_HUNDREDTH_MM) {
            if (is_debug_enabled()) {
                fprintf(stderr, "[brother5-longpaper] Patching pCaps LongPaper caps: support %d->2, maxSimplex %d->%d\n",
                        *pLongSupport, *pLongMaxSimplex, LONG_PAPER_MAX_HUNDREDTH_MM);
            }
            *pLongSupport = 2;
            *pLongMaxSimplex = LONG_PAPER_MAX_HUNDREDTH_MM;
            *pLongMaxDuplex = LONG_PAPER_MAX_HUNDREDTH_MM;
        }
    }
}

/*
 * Hooks for Brother Core API (libLxBsScanCoreApi.so) and bssdkwrapper
 */
typedef int (*bssdkwrapper_StartScanning_fn)(void *pScanParam);
typedef int (*IsLongPaperScanSetting_fn)(void *thiz, int paperType);
typedef void (*MakeLongPaperModeString_fn)(void *result, void *thiz, int enable);
typedef void (*ChangeLongPaperIfNeeded_fn)(void *pScanAreaInfo, const unsigned int *res);

static bssdkwrapper_StartScanning_fn g_orig_bssdkwrapper_StartScanning = NULL;
static IsLongPaperScanSetting_fn g_orig_IsLongPaperScanSetting = NULL;
static MakeLongPaperModeString_fn g_orig_MakeLongPaperModeString = NULL;
static ChangeLongPaperIfNeeded_fn g_orig_ChangeLongPaperIfNeeded = NULL;

static int my_bssdkwrapper_StartScanning(void *pScanParam) {
    if (is_debug_enabled()) {
        fprintf(stderr, "[brother5-longpaper] bssdkwrapper_StartScanning intercepted (%p)\n", pScanParam);
    }
    if (pScanParam) {
        uint32_t *pPaperType = (uint32_t *)((char *)pScanParam + 0xc);
        uint32_t *pSizeX = (uint32_t *)((char *)pScanParam + 0x10);
        uint32_t *pSizeY = (uint32_t *)((char *)pScanParam + 0x14);
        uint8_t *pAutoDeskew = (uint8_t *)((char *)pScanParam + 0x49);
        uint8_t *pAutoCrop = (uint8_t *)((char *)pScanParam + 0x4a);

        if (is_debug_enabled()) {
            fprintf(stderr, "[brother5-longpaper] Original scanParam: papertype=%u, size=(%u, %u), deskew=%u, crop=%u\n",
                    *pPaperType, *pSizeX, *pSizeY, *pAutoDeskew, *pAutoCrop);
        }

        /* Check if long paper scan requested (height > 356mm / 35600 or paperType == 0x17) */
        if (*pSizeY > 35600 || *pPaperType == 0x17) {
            g_long_paper_active = 1;
            *pPaperType = 0x17; /* LONGPAPER_NARROW */
            *pAutoDeskew = 0;
            *pAutoCrop = 0;
            *((uint8_t *)pScanParam + 0x58) = 0;
            *((uint8_t *)pScanParam + 0xbc) = 0;
            *((uint8_t *)pScanParam + 0xb0) = 0;
            *((uint8_t *)pScanParam + 0xc4) = 0;
            *((uint8_t *)pScanParam + 0xfc) = 0;
            *((uint8_t *)pScanParam + 0x100) = 0;

            if (is_debug_enabled()) {
                fprintf(stderr, "[brother5-longpaper] Activated LONGPAPER mode: papertype=0x17, sizeY=%u, deskew/crop disabled\n", *pSizeY);
            }
        } else {
            g_long_paper_active = 0;
        }
    }

    if (!g_orig_bssdkwrapper_StartScanning) {
        g_orig_bssdkwrapper_StartScanning = (bssdkwrapper_StartScanning_fn)get_real_symbol("bssdkwrapper_StartScanning");
    }
    return g_orig_bssdkwrapper_StartScanning ? g_orig_bssdkwrapper_StartScanning(pScanParam) : 0;
}

static int my_IsLongPaperScanSetting(void *thiz, int paperType) {
    if (g_long_paper_active || paperType == 0x17 || paperType == 0x18) {
        if (is_debug_enabled()) {
            fprintf(stderr, "[brother5-longpaper] IsLongPaperScanSetting(paperType=%d, active=%d) -> returning 1 (true)\n",
                    paperType, g_long_paper_active);
        }
        return 1;
    }
    if (!g_orig_IsLongPaperScanSetting) {
        g_orig_IsLongPaperScanSetting = (IsLongPaperScanSetting_fn)get_core_symbol("_ZN6bscore10CScanParam22IsLongPaperScanSettingENS_9PAPERTYPEE");
    }
    return g_orig_IsLongPaperScanSetting ? g_orig_IsLongPaperScanSetting(thiz, paperType) : 0;
}

static void my_MakeLongPaperModeString(void *result, void *thiz, int enable) {
    int target_enable = g_long_paper_active ? 1 : enable;
    if (is_debug_enabled()) {
        fprintf(stderr, "[brother5-longpaper] MakeLongPaperModeString(enable=%d) -> forcing enable=%d (%s)\n",
                enable, target_enable, target_enable ? "LONG=ON" : "LONG=OFF");
    }
    if (!g_orig_MakeLongPaperModeString) {
        g_orig_MakeLongPaperModeString = (MakeLongPaperModeString_fn)get_core_symbol("_ZN6bscore13CCmdString_v223MakeLongPaperModeStringEb");
    }
    if (g_orig_MakeLongPaperModeString) {
        g_orig_MakeLongPaperModeString(result, thiz, target_enable);
    }
}

static void my_ChangeLongPaperIfNeeded(void *pScanAreaInfo, const unsigned int *res) {
    if (is_debug_enabled()) {
        fprintf(stderr, "[brother5-longpaper] ChangeLongPaperIfNeeded intercepted (%p)\n", pScanAreaInfo);
    }
    if (g_long_paper_active && pScanAreaInfo) {
        uint32_t *pPaperType = (uint32_t *)pScanAreaInfo;
        uint32_t *pSizeX = (uint32_t *)((char *)pScanAreaInfo + 0x4);
        uint32_t *pSizeY = (uint32_t *)((char *)pScanAreaInfo + 0x8);
        if (is_debug_enabled()) {
            fprintf(stderr, "[brother5-longpaper] ChangeLongPaper orig: papertype=%u, size=(%u, %u)\n",
                    *pPaperType, *pSizeX, *pSizeY);
        }
        *pPaperType = 0x17;
        *pSizeX = 0;
        *pSizeY = 0;
        if (is_debug_enabled()) {
            fprintf(stderr, "[brother5-longpaper] ChangeLongPaper forced: papertype=0x17, size=(0, 0)\n");
        }
        return;
    }
    if (!g_orig_ChangeLongPaperIfNeeded) {
        g_orig_ChangeLongPaperIfNeeded = (ChangeLongPaperIfNeeded_fn)get_core_symbol("_ZN6bscore21CReadPreparation_base23ChangeLongPaperIfNeededERNS_15tagSCANAREAINFOERKj");
    }
    if (g_orig_ChangeLongPaperIfNeeded) {
        g_orig_ChangeLongPaperIfNeeded(pScanAreaInfo, res);
    }
}

/* Also export symbols in case any caller resolves globally */
int bssdkwrapper_StartScanning(void *pScanParam) {
    return my_bssdkwrapper_StartScanning(pScanParam);
}

int _ZN6bscore10CScanParam22IsLongPaperScanSettingENS_9PAPERTYPEE(void *thiz, int paperType) {
    return my_IsLongPaperScanSetting(thiz, paperType);
}

void _ZN6bscore13CCmdString_v223MakeLongPaperModeStringEb(void *result, void *thiz, int enable) {
    my_MakeLongPaperModeString(result, thiz, enable);
}

void _ZN6bscore21CReadPreparation_base23ChangeLongPaperIfNeededERNS_15tagSCANAREAINFOERKj(void *pScanAreaInfo, const unsigned int *res) {
    my_ChangeLongPaperIfNeeded(pScanAreaInfo, res);
}

/*
 * Dynamic GOT Patcher using ELF program headers
 */
static void patch_got_slot(uintptr_t slot_addr, void *new_target) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_addr = slot_addr & ~(page_size - 1);
    void **slot = (void **)slot_addr;

    if (*slot == new_target) {
        return; /* Already patched */
    }

    if (mprotect((void *)page_addr, page_size, PROT_READ | PROT_WRITE) == 0) {
        *slot = new_target;
        if (is_debug_enabled()) {
            fprintf(stderr, "[brother5-longpaper] Patched GOT slot at %p -> %p\n",
                    (void *)slot_addr, new_target);
        }
    } else {
        if (is_debug_enabled()) {
            perror("[brother5-longpaper] mprotect failed");
        }
    }
}

static void inspect_and_patch_elf(ElfW(Addr) base_addr, const ElfW(Phdr) *phdr_ptr, ElfW(Half) phnum, const char *lib_name) {
    const ElfW(Phdr) *dyn_ph = NULL;
    for (ElfW(Half) i = 0; i < phnum; i++) {
        if (phdr_ptr[i].p_type == PT_DYNAMIC) {
            dyn_ph = &phdr_ptr[i];
            break;
        }
    }
    if (!dyn_ph) return;

    ElfW(Dyn) *dyn = (ElfW(Dyn) *)(base_addr + dyn_ph->p_vaddr);
    ElfW(Addr) jmprel = 0;
    ElfW(Word) pltrelsz = 0;
    ElfW(Addr) symtab = 0;
    ElfW(Addr) strtab = 0;

    for (ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_JMPREL:   jmprel = d->d_un.d_ptr; break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_SYMTAB:   symtab = d->d_un.d_ptr; break;
            case DT_STRTAB:   strtab = d->d_un.d_ptr; break;
        }
    }

    if (!jmprel || !pltrelsz || !symtab || !strtab) return;

    ElfW(Rela) *rela = (ElfW(Rela) *)jmprel;
    size_t count = pltrelsz / sizeof(ElfW(Rela));
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab;
    const char *strings = (const char *)strtab;

    int is_brother5_real = (strstr(lib_name, "brother5-real") != NULL);
    int is_scancore = (strstr(lib_name, "LxBsScanCore") != NULL);

    for (size_t i = 0; i < count; i++) {
        ElfW(Word) sym_idx = ELF64_R_SYM(rela[i].r_info);
        const char *name = strings + syms[sym_idx].st_name;
        uintptr_t slot_addr = (uintptr_t)(base_addr + rela[i].r_offset);

        if (is_brother5_real && strcmp(name, "bssdkwrapper_StartScanning") == 0) {
            patch_got_slot(slot_addr, (void *)my_bssdkwrapper_StartScanning);
        } else if (is_scancore) {
            if (strcmp(name, "_ZN6bscore10CScanParam22IsLongPaperScanSettingENS_9PAPERTYPEE") == 0) {
                patch_got_slot(slot_addr, (void *)my_IsLongPaperScanSetting);
            } else if (strcmp(name, "_ZN6bscore13CCmdString_v223MakeLongPaperModeStringEb") == 0) {
                patch_got_slot(slot_addr, (void *)my_MakeLongPaperModeString);
            } else if (strcmp(name, "_ZN6bscore21CReadPreparation_base23ChangeLongPaperIfNeededERNS_15tagSCANAREAINFOERKj") == 0) {
                patch_got_slot(slot_addr, (void *)my_ChangeLongPaperIfNeeded);
            }
        }
    }
}

static int dl_patch_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    (void)data;
    if (info->dlpi_name && info->dlpi_name[0] != '\0') {
        if (strstr(info->dlpi_name, "brother5-real") || strstr(info->dlpi_name, "LxBsScanCore")) {
            inspect_and_patch_elf(info->dlpi_addr, info->dlpi_phdr, info->dlpi_phnum, info->dlpi_name);
        }
    }
    return 0;
}

static void apply_got_patches(void) {
    dl_iterate_phdr(dl_patch_callback, NULL);
}

/* SANE API Forwarders & Hooks */

SANE_Status sane_brother5_init(SANE_Int *version_code, void *authorize) {
    SANE_Status (*real_fn)(SANE_Int *, void *) = get_real_symbol("sane_brother5_init");
    if (!real_fn) real_fn = get_real_symbol("sane_init");
    SANE_Status res = real_fn ? real_fn(version_code, authorize) : SANE_STATUS_GOOD;
    patch_range_y();
    apply_got_patches();
    return res;
}

SANE_Status sane_init(SANE_Int *version_code, void *authorize) {
    return sane_brother5_init(version_code, authorize);
}

void sane_brother5_exit(void) {
    void (*real_fn)(void) = get_real_symbol("sane_brother5_exit");
    if (!real_fn) real_fn = get_real_symbol("sane_exit");
    if (real_fn) real_fn();
}

void sane_exit(void) {
    sane_brother5_exit();
}

SANE_Status sane_brother5_get_devices(const SANE_Device ***device_list, SANE_Bool local_only) {
    SANE_Status (*real_fn)(const SANE_Device ***, SANE_Bool) = get_real_symbol("sane_brother5_get_devices");
    if (!real_fn) real_fn = get_real_symbol("sane_get_devices");
    SANE_Status res = real_fn ? real_fn(device_list, local_only) : SANE_STATUS_GOOD;
    apply_got_patches();
    return res;
}

SANE_Status sane_get_devices(const SANE_Device ***device_list, SANE_Bool local_only) {
    return sane_brother5_get_devices(device_list, local_only);
}

SANE_Status sane_brother5_open(SANE_String_Const name, SANE_Handle *handle) {
    SANE_Status (*real_fn)(SANE_String_Const, SANE_Handle *) = get_real_symbol("sane_brother5_open");
    if (!real_fn) real_fn = get_real_symbol("sane_open");
    if (!real_fn) return 4; /* SANE_STATUS_INVAL */

    SANE_Status res = real_fn(name, handle);
    if (res == SANE_STATUS_GOOD && handle && *handle) {
        patch_range_y();
        patch_dev_caps(*handle);
        apply_got_patches();
    }
    return res;
}

SANE_Status sane_open(SANE_String_Const name, SANE_Handle *handle) {
    return sane_brother5_open(name, handle);
}

void sane_brother5_close(SANE_Handle handle) {
    void (*real_fn)(SANE_Handle) = get_real_symbol("sane_brother5_close");
    if (!real_fn) real_fn = get_real_symbol("sane_close");
    if (real_fn) real_fn(handle);
}

void sane_close(SANE_Handle handle) {
    sane_brother5_close(handle);
}

const SANE_Option_Descriptor *sane_brother5_get_option_descriptor(SANE_Handle handle, SANE_Int option) {
    const SANE_Option_Descriptor *(*real_fn)(SANE_Handle, SANE_Int) = get_real_symbol("sane_brother5_get_option_descriptor");
    if (!real_fn) real_fn = get_real_symbol("sane_get_option_descriptor");
    patch_range_y();
    if (handle) patch_dev_caps(handle);
    apply_got_patches();
    return real_fn ? real_fn(handle, option) : NULL;
}

const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle handle, SANE_Int option) {
    return sane_brother5_get_option_descriptor(handle, option);
}

SANE_Status sane_brother5_control_option(SANE_Handle handle, SANE_Int option, SANE_Action action, void *value, SANE_Int *info) {
    SANE_Status (*real_fn)(SANE_Handle, SANE_Int, SANE_Action, void *, SANE_Int *) = get_real_symbol("sane_brother5_control_option");
    if (!real_fn) real_fn = get_real_symbol("sane_control_option");
    patch_range_y();
    if (handle) patch_dev_caps(handle);
    apply_got_patches();
    return real_fn ? real_fn(handle, option, action, value, info) : 4;
}

SANE_Status sane_control_option(SANE_Handle handle, SANE_Int option, SANE_Action action, void *value, SANE_Int *info) {
    return sane_brother5_control_option(handle, option, action, value, info);
}

SANE_Status sane_brother5_get_parameters(SANE_Handle handle, SANE_Parameters *params) {
    SANE_Status (*real_fn)(SANE_Handle, SANE_Parameters *) = get_real_symbol("sane_brother5_get_parameters");
    if (!real_fn) real_fn = get_real_symbol("sane_get_parameters");
    return real_fn ? real_fn(handle, params) : SANE_STATUS_GOOD;
}

SANE_Status sane_get_parameters(SANE_Handle handle, SANE_Parameters *params) {
    return sane_brother5_get_parameters(handle, params);
}

SANE_Status sane_brother5_start(SANE_Handle handle) {
    SANE_Status (*real_fn)(SANE_Handle) = get_real_symbol("sane_brother5_start");
    if (!real_fn) real_fn = get_real_symbol("sane_start");
    patch_range_y();
    if (handle) patch_dev_caps(handle);
    apply_got_patches();
    return real_fn ? real_fn(handle) : SANE_STATUS_GOOD;
}

SANE_Status sane_start(SANE_Handle handle) {
    return sane_brother5_start(handle);
}

SANE_Status sane_brother5_read(SANE_Handle handle, void *data, SANE_Int max_length, SANE_Int *length) {
    SANE_Status (*real_fn)(SANE_Handle, void *, SANE_Int, SANE_Int *) = get_real_symbol("sane_brother5_read");
    if (!real_fn) real_fn = get_real_symbol("sane_read");
    return real_fn ? real_fn(handle, data, max_length, length) : SANE_STATUS_GOOD;
}

SANE_Status sane_read(SANE_Handle handle, void *data, SANE_Int max_length, SANE_Int *length) {
    return sane_brother5_read(handle, data, max_length, length);
}

void sane_brother5_cancel(SANE_Handle handle) {
    void (*real_fn)(SANE_Handle) = get_real_symbol("sane_brother5_cancel");
    if (!real_fn) real_fn = get_real_symbol("sane_cancel");
    if (real_fn) real_fn(handle);
}

void sane_cancel(SANE_Handle handle) {
    sane_brother5_cancel(handle);
}

SANE_Status sane_brother5_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking) {
    SANE_Status (*real_fn)(SANE_Handle, SANE_Bool) = get_real_symbol("sane_brother5_set_io_mode");
    if (!real_fn) real_fn = get_real_symbol("sane_set_io_mode");
    return real_fn ? real_fn(handle, non_blocking) : SANE_STATUS_GOOD;
}

SANE_Status sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking) {
    return sane_brother5_set_io_mode(handle, non_blocking);
}

SANE_Status sane_brother5_get_select_fd(SANE_Handle handle, SANE_Int *fd) {
    SANE_Status (*real_fn)(SANE_Handle, SANE_Int *) = get_real_symbol("sane_brother5_get_select_fd");
    if (!real_fn) real_fn = get_real_symbol("sane_get_select_fd");
    return real_fn ? real_fn(handle, fd) : SANE_STATUS_GOOD;
}

SANE_Status sane_get_select_fd(SANE_Handle handle, SANE_Int *fd) {
    return sane_brother5_get_select_fd(handle, fd);
}

SANE_String_Const sane_strstatus(SANE_Status status) {
    SANE_String_Const (*real_fn)(SANE_Status) = get_real_symbol("sane_strstatus");
    return real_fn ? real_fn(status) : "Unknown status";
}
