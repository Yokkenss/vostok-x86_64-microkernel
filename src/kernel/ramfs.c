#include "kernel/ramfs.h"

#include "boot/limine_requests.h"
#include "memory.h"
#include "print.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ramfs_file {
    bool used;
    char path[RAMFS_PATH_LIMIT];
    size_t path_length;
    uint8_t data[RAMFS_FILE_SIZE];
    size_t length;
};

static struct ramfs_file files[RAMFS_FILE_LIMIT];

static bool text_equal(const char *left, size_t left_length,
                       const char *right, size_t right_length)
{
    return left_length == right_length
        && memcmp(left, right, left_length) == 0;
}

static struct ramfs_file *find_file(const char *path, size_t length)
{
    for (size_t index = 0; index < RAMFS_FILE_LIMIT; ++index) {
        if (files[index].used
            && text_equal(files[index].path, files[index].path_length,
                          path, length)) return &files[index];
    }
    return NULL;
}

void ramfs_initialize(void)
{
    memset(files, 0, sizeof(files));
    log_info("ramfs mounted at /home: %u files, %u bytes each",
             RAMFS_FILE_LIMIT, RAMFS_FILE_SIZE);
}

int64_t ramfs_write(const char *path, size_t path_length,
                    const uint8_t *data, size_t length)
{
    if (path_length == 0 || path_length >= RAMFS_PATH_LIMIT
        || length > RAMFS_FILE_SIZE || path[0] != '/') return -1;
    struct ramfs_file *file = find_file(path, path_length);
    if (file == NULL) {
        for (size_t index = 0; index < RAMFS_FILE_LIMIT; ++index) {
            if (!files[index].used) {
                file = &files[index];
                file->used = true;
                file->path_length = path_length;
                memcpy(file->path, path, path_length);
                file->path[path_length] = '\0';
                break;
            }
        }
    }
    if (file == NULL) return -4;
    memcpy(file->data, data, length);
    file->length = length;
    log_info("ramfs: wrote %lu bytes to %s",
             (unsigned long)length, file->path);
    return (int64_t)length;
}

int64_t ramfs_read(const char *path, size_t path_length,
                   uint8_t *data, size_t capacity)
{
    struct ramfs_file *file = find_file(path, path_length);
    if (file == NULL) return -6;
    const size_t count = file->length < capacity ? file->length : capacity;
    memcpy(data, file->data, count);
    return (int64_t)count;
}

int64_t ramfs_lookup(const char *path, size_t path_length)
{
    struct ramfs_file *file = find_file(path, path_length);
    return file == NULL ? -6 : (int64_t)(file - files);
}

int64_t ramfs_read_id(size_t id, uint8_t *data, size_t capacity)
{
    if (id >= RAMFS_FILE_LIMIT || !files[id].used) return -6;
    const size_t count = files[id].length < capacity
                       ? files[id].length : capacity;
    memcpy(data, files[id].data, count);
    return (int64_t)count;
}

static bool append(char *output, size_t capacity, size_t *used,
                   const char *text, size_t length)
{
    if (length > capacity - *used) return false;
    memcpy(output + *used, text, length);
    *used += length;
    return true;
}

int64_t ramfs_list(char *output, size_t capacity)
{
    size_t used = 0;
    const struct limine_module_response *modules = boot_modules();
    if (modules != NULL) {
        for (uint64_t index = 0; index < modules->module_count; ++index) {
            const char *name = modules->modules[index]->string;
            size_t length = 0;
            while (name != NULL && name[length] != '\0') ++length;
            if (!append(output, capacity, &used, "/bin/", 5)
                || !append(output, capacity, &used, name, length)
                || !append(output, capacity, &used, "\n", 1)) return -4;
        }
    }
    for (size_t index = 0; index < RAMFS_FILE_LIMIT; ++index) {
        if (!files[index].used) continue;
        if (!append(output, capacity, &used, files[index].path,
                    files[index].path_length)
            || !append(output, capacity, &used, "\n", 1)) return -4;
    }
    return (int64_t)used;
}

const uint8_t *ramfs_image(const char *path, size_t path_length, size_t *size)
{
    struct ramfs_file *file = find_file(path, path_length);
    if (file == NULL) return NULL;
    *size = file->length;
    return file->data;
}

const struct limine_file *bootfs_find(const char *name, size_t length)
{
    const struct limine_module_response *modules = boot_modules();
    if (modules == NULL) return NULL;
    if (length > 5 && memcmp(name, "/bin/", 5) == 0) {
        name += 5;
        length -= 5;
    }
    for (uint64_t index = 0; index < modules->module_count; ++index) {
        const struct limine_file *module = modules->modules[index];
        size_t module_length = 0;
        while (module->string != NULL
               && module->string[module_length] != '\0') ++module_length;
        if (text_equal(module->string, module_length, name, length))
            return module;
    }
    return NULL;
}
