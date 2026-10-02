#ifndef TEST_DMOD_H
#define TEST_DMOD_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint64_t Dmod_Timestamp_t;
typedef struct { int unused; } Dmod_Config_t;
Dmod_Timestamp_t Dmod_GetUptime(void);
void Dmod_EnterCritical(void);
void Dmod_ExitCritical(void);
void *Dmod_Malloc(size_t size);
void Dmod_Free(void *ptr);
#endif
