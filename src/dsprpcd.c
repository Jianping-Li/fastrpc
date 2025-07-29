// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO 0

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "verify.h"
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

typedef enum {
    DSP_TYPE_ADSP,
    DSP_TYPE_CDSP,
    DSP_TYPE_SDSP,
    DSP_TYPE_INVALID
} dsp_type_t;

#ifndef ADSP_DEFAULT_LISTENER_NAME
#define ADSP_DEFAULT_LISTENER_NAME "libadsp_default_listener.so"
#endif
#ifndef CDSP_DEFAULT_LISTENER_NAME
#define CDSP_DEFAULT_LISTENER_NAME "libcdsp_default_listener.so"
#endif
#ifndef SDSP_DEFAULT_LISTENER_NAME
#define SDSP_DEFAULT_LISTENER_NAME "libsdsp_default_listener.so"
#endif
#ifndef LIBHIDL_NAME
#define LIBHIDL_NAME "libhidlbase.so"
#endif

typedef struct {
    const char* name;
    const char* lib_name;
    unsigned int retry_interval_us;
    int exit_on_conn_refused;
    int needs_hidl;
} dsp_config_t;

static const dsp_config_t dsp_configs[] = {
    [DSP_TYPE_ADSP] = {
        .name = "adsp",
        .lib_name = ADSP_DEFAULT_LISTENER_NAME,
        .retry_interval_us = 25000,
        .exit_on_conn_refused = 0,
        .needs_hidl = 1
    },
    [DSP_TYPE_CDSP] = {
        .name = "cdsp",
        .lib_name = CDSP_DEFAULT_LISTENER_NAME,
        .retry_interval_us = 100000,
        .exit_on_conn_refused = 1,
        .needs_hidl = 1
    },
    [DSP_TYPE_SDSP] = {
        .name = "sdsp",
        .lib_name = SDSP_DEFAULT_LISTENER_NAME,
        .retry_interval_us = 100000,
        .exit_on_conn_refused = 0,
        .needs_hidl = 0
    }
};

typedef int (*dsp_default_listener_start_t)(int argc, char *argv[]);

void print_usage(const char* program_name) {
    VERIFY_IPRINTF("Usage: %s <dsp_type>", program_name);
    VERIFY_IPRINTF("Available DSP type:");
    VERIFY_IPRINTF("  %s - Audio DSP", dsp_configs[0].name);
    VERIFY_IPRINTF("  %s - Compute DSP", dsp_configs[1].name);
    VERIFY_IPRINTF("  %s - Sensor DSP", dsp_configs[2].name); 
}

dsp_type_t parse_dsp_type(const char* type_str) {
    for (int i = 0; i < DSP_TYPE_INVALID; i++) {
        if (strcmp(type_str, dsp_configs[i].name) == 0) {
            return i;
        }
    }
    return DSP_TYPE_INVALID;
}

int main(int argc, char *argv[]) {
  int nErr = 0;
  void *dsphandler = NULL;
#ifndef NO_HAL
  void *libhidlbaseHandler = NULL;
#endif
  dsp_default_listener_start_t listener_start;
  dsp_type_t dsp_type = DSP_TYPE_INVALID;

  if (argc < 2) {
      print_usage(argv[0]);
      goto bail;
  }

  dsp_type = parse_dsp_type(argv[1]);
  if (dsp_type == DSP_TYPE_INVALID) {
    VERIFY_IPRINTF("Error: Invalid DSP type %s", argv[1]);
    print_usage(argv[0]);
    goto bail;
  }

  const dsp_config_t *config = &dsp_configs[dsp_type];
  VERIFY_IPRINTF("%s daemon starting", config->name);

#ifndef NO_HAL
  if (config->needs_hidl) {
    if (NULL == (libhidlbaseHandler = dlopen(LIBHIDL_NAME, RTLD_NOW))) {
        VERIFY_IPRINTF("Failed to load %s: %s", LIBHIDL_NAME, dlerror());
        goto bail;
    }
  }
#endif
  
  while (1) {
        if (NULL != (dsphandler = dlopen(config->lib_name,RTLD_NOW))) {
            if (NULL != (listener_start = (dsp_default_listener_start_t)dlsym(
                              dsphandler, "adsp_default_listener_start"))) {
                VERIFY_IPRINTF("adsp_default_listener_start called");
                nErr = listener_start(argc, argv);
            }
            if (0 != dlclose(dsphandler)) {
              VERIFY_IPRINTF("dlclose failed for %s", config->lib_name);
            }
        } else {
            VERIFY_IPRINTF("%s daemon error %s", config->name, dlerror());
        }

        if (nErr == AEE_ECONNREFUSED) {
            if (config->exit_on_conn_refused) {
                VERIFY_IPRINTF("fastRPC device driver is disabled, daemon exiting...");
                break;
            } else {
                VERIFY_IPRINTF("fastRPC device driver is disabled, retrying...");
            }
        }

        VERIFY_IPRINTF("%s daemon will restart after %dms...", config->name, config->retry_interval_us / 1000);
        usleep(config->retry_interval_us);
  }

#ifndef NO_HAL
  if (config->needs_hidl && libhidlbaseHandler != NULL) {
    if (0 != dlclose(libhidlbaseHandler)) {
        VERIFY_IPRINTF("libhidlbase dlclose failed");
    }
  }
#endif

  bail:
    VERIFY_IPRINTF("%s daemon exiting %x", config->name, nErr);
    return nErr;
}
