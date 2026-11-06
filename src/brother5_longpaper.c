#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

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

static void *g_real_lib_handle = NULL;
static int g_debug = -1;

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

static void patch_range_y(void) {
    void *handle = g_real_lib_handle;
    if (!handle) {
        handle = dlopen("libsane-brother5.so.1", RTLD_NOLOAD | RTLD_GLOBAL);
    }
    if (!handle) {
        handle = dlopen("/usr/lib/sane/libsane-brother5.so.1", RTLD_NOLOAD | RTLD_GLOBAL);
    }
    if (!handle) {
        handle = dlopen("/usr/lib/sane/libsane-brother5-real.so.1", RTLD_NOLOAD | RTLD_GLOBAL);
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

/* SANE API Forwarders & Hooks */

SANE_Status sane_brother5_init(SANE_Int *version_code, void *authorize) {
    SANE_Status (*real_fn)(SANE_Int *, void *) = get_real_symbol("sane_brother5_init");
    if (!real_fn) real_fn = get_real_symbol("sane_init");
    SANE_Status res = real_fn ? real_fn(version_code, authorize) : SANE_STATUS_GOOD;
    patch_range_y();
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
    return real_fn ? real_fn(device_list, local_only) : SANE_STATUS_GOOD;
}

SANE_Status sane_get_devices(const SANE_Device ***device_list, SANE_Bool local_only) {
    return sane_brother5_get_devices(device_list, local_only);
}

SANE_Status sane_brother5_open(SANE_String_Const name, SANE_Handle *handle) {
    SANE_Status (*real_fn)(SANE_String_Const, SANE_Handle *) = get_real_symbol("sane_brother5_open");
    if (!real_fn) real_fn = get_real_symbol("sane_open");
    if (!real_fn) return 4; /* SANE_STATUS_INVAL */

    SANE_Status res = real_fn(name, handle);
    if (res == SANE_STATUS_GOOD) {
        patch_range_y();
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
    return real_fn ? real_fn(handle, option) : NULL;
}

const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle handle, SANE_Int option) {
    return sane_brother5_get_option_descriptor(handle, option);
}

SANE_Status sane_brother5_control_option(SANE_Handle handle, SANE_Int option, SANE_Action action, void *value, SANE_Int *info) {
    SANE_Status (*real_fn)(SANE_Handle, SANE_Int, SANE_Action, void *, SANE_Int *) = get_real_symbol("sane_brother5_control_option");
    if (!real_fn) real_fn = get_real_symbol("sane_control_option");
    patch_range_y();
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
