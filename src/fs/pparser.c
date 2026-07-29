#include "pparser.h"
#include "string/string.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "status.h"

static int pathparser_path_valid_format(const char *filename)
{
    int len = strnlen(filename, MARROWOS_MAX_PATH);
    return (len >= 3 && isDigit(filename[0]) && memcmp((void *)&filename[1], ":/", 2) == 0);
}

static int pathparser_get_drive_by_path(const char **path)
{
    if (!pathparser_path_valid_format(*path))
    {
        return -EBADPATH;
    }

    int drive_no = tonumericdigit(*path[0]);

    // Add 2 bytes to skip drive number 0:/ 1:/ 2:/
    *path += 3;

    return drive_no;
}

static struct path_root *pathparser_create_root(int drive_number)
{
    struct path_root *path_r = kzalloc(sizeof(struct path_root));
    path_r->drive_no = drive_number;
    path_r->first = 0;
    return path_r;
}

static const char *pathparser_get_path_part(const char **path)
{

    char *result_path_part = kzalloc(MARROWOS_MAX_PATH);
    int i = 0;
    while (**path != '/' && **path != 0x00)
    {
        result_path_part[i] = **path;
        *path += 1;
        i++;
    }
    if (**path == '/')
    {
        // Skip the forward slash to avoid loop
        *path += 1;
    }
    if (i == 0)
    {
        kfree(result_path_part);
        result_path_part = 0;
    }
    return result_path_part;
}

struct path_part *pathparser_parser_path_part(struct path_part *last_part, const char **path)
{
    const char *path_part_str = pathparser_get_path_part(path);
    if (!path_part_str)
    {
        return 0;
    }
    struct path_part *part = kzalloc(sizeof(struct path_part));
    part->part = path_part_str;
    part->next = 0x00;

    if (last_part)
    {
        last_part->next = part;
    }
    return part;
}

void pathparser_free(struct path_root *root)
{
    struct path_part *part = root->first;
    while (part)
    {
        struct path_part *next_part = part->next;
        kfree((void *)part->part);
        kfree(part);
        part = next_part;
    }
    kfree(root);
}

struct path_root *pathparser_parse(const char *path, const char *current_directory_path)
{
    int res = 0;
    const char *temp_path = path;
    struct path_root *path_root = 0;
    if (strlen(path) > MARROWOS_MAX_PATH)
    {
        goto out;
    }
    res = pathparser_get_drive_by_path(&temp_path);
    if (res < 0)
    {
        goto out;
    }

    path_root = pathparser_create_root(res);
    if (!path_root)
    {
        goto out;
    }

    struct path_part *first_path = pathparser_parser_path_part(NULL, &temp_path);
    if (!first_path)
    {
        goto out;
    }

    path_root->first = first_path;
    struct path_part *part = pathparser_parser_path_part(first_path, &temp_path);

    while (part)
    {
        part = pathparser_parser_path_part(part, &temp_path);
    }

out:
    return path_root;
}
