#ifndef TEST_DMDRVI_H
#define TEST_DMDRVI_H
#include <stdint.h>
#include "dmini.h"
typedef struct dmdrvi_context *dmdrvi_context_t;
typedef int64_t dmdrvi_ssize_t;
typedef int64_t dmdrvi_offset_t;
#define DMDRVI_NUM_MINOR 2
#define DMDRVI_NUM_ALT_NAME 4
#define DMDRVI_ALT_NAME_MAX_LEN 32
typedef struct { uint8_t major, minor, flags; char alt_name[33]; } dmdrvi_dev_num_t;
typedef struct { uint64_t size; uint32_t mode; } dmdrvi_stat_t;
#define dmod_dmdrvi_dif_api_declaration(v,m,r,n,a) r driver##n a
#endif
