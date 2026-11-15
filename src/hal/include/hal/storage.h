#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct HalFile {
    int32_t handle;
    uint32_t size;
} HalFile;

bool hal_file_open(HalFile *file, const char *path);
bool hal_file_read(HalFile file, uint32_t offset, void *buffer, uint32_t length);

typedef enum HalReadState {
    HAL_READ_IDLE,
    HAL_READ_PENDING,
    HAL_READ_DONE,
    HAL_READ_FAILED
} HalReadState;

typedef enum HalCardSpeed {
    HAL_CARD_SPEED_DEFAULT,
    HAL_CARD_SPEED_HIGH,
    HAL_CARD_SPEED_REFUSED,
    HAL_CARD_SPEED_FELL_BACK
} HalCardSpeed;

uint32_t hal_file_read_start(HalFile file, uint32_t offset, void *buffer, uint32_t length);
HalReadState hal_file_read_poll(void);
HalCardSpeed hal_storage_card_speed(void);
uint32_t hal_storage_card_dma_failures(void);
void hal_file_close(HalFile *file);
bool hal_file_write(const char *path, const void *bytes, uint32_t length);
bool hal_file_write_at(const char *path, uint32_t offset, const void *bytes, uint32_t length);

enum {
    HAL_RESERVE_CLEARED_BYTES = 4096
};

typedef enum HalReserveResult {
    HAL_RESERVE_EXISTS,
    HAL_RESERVE_CREATED,
    HAL_RESERVE_NO_SPACE,
    HAL_RESERVE_FAILED
} HalReserveResult;

HalReserveResult hal_file_reserve(const char *path, uint32_t size);
HalReserveResult hal_file_grow(const char *path, uint32_t size);

enum {
    HAL_NAME_CAPACITY = 768
};

typedef struct HalDir {
    int handle;
} HalDir;

typedef struct HalDirEntry {
    bool directory;
    bool hidden;
    uint32_t size;
    uint32_t modified;
    char name[HAL_NAME_CAPACITY];
} HalDirEntry;

bool hal_storage_card_ready(void);
bool hal_dir_open(HalDir *dir, const char *path);
bool hal_dir_next(HalDir *dir, HalDirEntry *entry);
void hal_dir_close(HalDir *dir);
