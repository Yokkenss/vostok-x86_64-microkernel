#ifndef VOSTOK_KERNEL_RAMFS_H
#define VOSTOK_KERNEL_RAMFS_H

#include <limine.h>

#include <stddef.h>
#include <stdint.h>

enum {
    RAMFS_FILE_LIMIT = 16,
    RAMFS_PATH_LIMIT = 64,
    RAMFS_FILE_SIZE = 4096,
};

void ramfs_initialize(void);
int64_t ramfs_write(const char *path, size_t path_length,
                    const uint8_t *data, size_t length);
int64_t ramfs_read(const char *path, size_t path_length,
                   uint8_t *data, size_t capacity);
int64_t ramfs_read_id(size_t id, uint8_t *data, size_t capacity);
int64_t ramfs_lookup(const char *path, size_t path_length);
int64_t ramfs_list(char *output, size_t capacity);
const uint8_t *ramfs_image(const char *path, size_t path_length,
                           size_t *size);
const struct limine_file *bootfs_find(const char *name, size_t length);

#endif
