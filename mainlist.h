#ifndef __MAINLIST_H__
#define __MAINLIST_H__

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "custom_mutator_helpers.h"

typedef struct {
  uint16_t size;
  char cmd[13];
  uint8_t tid;
  // TODO: support string kv
  uint32_t key;
  uint32_t val;
} list_el_t;

typedef list_el_t *mainlist_t;

mainlist_t read_mlist_from_file(const char *path);
mainlist_t parse_mlist_from_file(const char *path);

typedef list_el_t *sublist_t;

sublist_t *get_sublist_from_mlist(mainlist_t mlist, size_t sublist_num);

void dump_mlist_to_file(mainlist_t mlist, const char *path);
void dump_cmd_from_mlist(mainlist_t mlist, FILE *stream);

#endif
