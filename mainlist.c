#include "mainlist.h"

BUF_VAR(list_el_t, mlist);

mainlist_t parse_mlist_from_file(const char *path) {
  FILE *fptr;
  char *line = NULL, *cmd, *key, *val;
  size_t i = 0, n = 0, thread_cnt = 0;

  if ((fptr = fopen(path, "r")) == NULL) {
    PFATAL("Unable to open file %s", path);
  }

  while (getline(&line, &n, fptr) > 0) {
    char *start_of_line = line;
    if (line[0] == '\t') {
      start_of_line += 1;
      thread_cnt += 1;
    }
    start_of_line = strtok(start_of_line, "\r\n");
    if (start_of_line == NULL) {
      break;
    }
    cmd = strtok(start_of_line, " ");
    if (strlen(cmd) > sizeof(mlist_buf->cmd)) {
      FATAL("Invalid input file format");
    }
    key = strtok(NULL, " ");
    mlist_buf = maybe_grow(BUF_PARAMS(mlist), (i + 1) * sizeof(list_el_t));
    strcpy(mlist_buf[i].cmd, cmd);
    mlist_buf[i].tid = thread_cnt;
    mlist_buf[i].key = key == NULL ? 0 : strtol(key, NULL, 0);
#if defined(MEMCACHED)
    if ((strcmp(cmd, "add") == 0) || (strcmp(cmd, "set") == 0) ||
        (strcmp(cmd, "replace") == 0) || (strcmp(cmd, "prepend") == 0) ||
        (strcmp(cmd, "append") == 0)) {
      if (getline(&line, &n, fptr) == -1) {
        FATAL("Invalid input file format");
      }
      val = line;
    } else {
      val = strtok(NULL, " ");
    }
#else
    val = strtok(NULL, " ");
#endif
    mlist_buf[i].val = val == NULL ? 0 : strtol(val, NULL, 0);
    ++i;
  }
  mlist_buf->size = i;

  free(line);
  fclose(fptr);
  return mlist_buf;
}

mainlist_t read_mlist_from_file(const char *path) {
  struct stat st;
  int fd;
  mainlist_t mlist;

  if ((fd = open(path, O_RDONLY)) == -1) {
    PFATAL("Failed to open file %s", path);
  }
  if (fstat(fd, &st) == -1) {
    PFATAL("Failed to get status of %s", path);
  }

  mlist = (mainlist_t)malloc(st.st_size);
  if (read(fd, mlist, st.st_size) == -1) {
    PFATAL("Failed to read file %s", path);
  }
  close(fd);

  if (mlist->size != (st.st_size / sizeof(list_el_t))) {
    FATAL("Inconsistent mlist file %s", path);
  }
  return mlist;
}

void dump_mlist_to_file(mainlist_t mlist, const char *path) {
  int fd;

  if ((fd = creat(path, 0700)) == -1) {
    PFATAL("Failed to create file %s", path);
  }

  if (write(fd, mlist, mlist->size * sizeof(list_el_t)) == -1) {
    PFATAL("Failed to write file %s", path);
  }
  close(fd);
}

sublist_t *get_sublist_from_mlist(mainlist_t mlist, size_t sublist_num) {
  sublist_t *sublists = (sublist_t *)calloc(sublist_num, sizeof(sublist_t));
  size_t sublist_cnts[sublist_num], i;

  for (i = 0; i < sublist_num; ++i) {
    sublists[i] = (sublist_t)calloc(mlist->size, sizeof(list_el_t));
    sublist_cnts[i] = 0;
  }
  for (i = 0; i < mlist->size; ++i) {
    int tid = mlist[i].tid;
    sublists[tid][sublist_cnts[tid]++] = mlist[i];
  }
  for (i = 0; i < sublist_num; ++i) {
    sublists[i]->size = sublist_cnts[i];
  }
  return sublists;
}

void dump_cmd_from_mlist(mainlist_t mlist, FILE *stream) {
  char *cmd, val[16];
  size_t len, i;

  for (i = 0; i < mlist->size; ++i) {
    cmd = mlist[i].cmd;
    len = sprintf(val, "%d", mlist[i].val);

#if defined(MEMCACHED)
    if ((strcmp(cmd, "add") == 0) || (strcmp(cmd, "set") == 0) ||
        (strcmp(cmd, "replace") == 0) || (strcmp(cmd, "prepend") == 0) ||
        (strcmp(cmd, "append") == 0)) {
      fprintf(stream, "%s 0x%x 0 0 %ld\r\n%s\r\n", cmd, mlist[i].key, len, val);
    } else if ((strcmp(cmd, "get") == 0) || (strcmp(cmd, "delete") == 0)) {
      fprintf(stream, "%s 0x%x\r\n", cmd, mlist[i].key);
    } else if ((strcmp(cmd, "incr") == 0) || (strcmp(cmd, "decr") == 0)) {
      fprintf(stream, "%s 0x%x %s\r\n", cmd, mlist[i].key, val);
    } else if ((strcmp(cmd, "stats") == 0) || (strcmp(cmd, "flush_all") == 0)) {
      fprintf(stream, "%s\r\n", cmd);
    } else {
      PFATAL("Unknown cmd %s", cmd);
    }
#elif defined(CLHT)
    if ((strcmp(cmd, "i") == 0) || (strcmp(cmd, "u") == 0)) {
      fprintf(stream, "%s 0x%x %s\n", cmd, mlist[i].key, val);
    } else if ((strcmp(cmd, "d") == 0) || (strcmp(cmd, "g") == 0)) {
      fprintf(stream, "%s 0x%x\n", cmd, mlist[i].key);
    } else {
      PFATAL("Unknown cmd %s", cmd);
    }
#elif defined(CCEH)
    if (strcmp(cmd, "i") == 0) {
      fprintf(stream, "%s 0x%x %s\n", cmd, mlist[i].key, val);
    } else if (strcmp(cmd, "g") == 0) {
      fprintf(stream, "%s 0x%x\n", cmd, mlist[i].key);
    } else if ((strcmp(cmd, "u") == 0) || (strcmp(cmd, "c") == 0)) {
      fprintf(stream, "%s\n", cmd);
    } else {
      PFATAL("Unknown cmd %s", cmd);
    }
#elif defined(FAST_FAIR)
    if ((strcmp(cmd, "i") == 0) || (strcmp(cmd, "r") == 0)) {
      fprintf(stream, "%s 0x%x %s\n", cmd, mlist[i].key, val);
    } else if ((strcmp(cmd, "s") == 0) || (strcmp(cmd, "d") == 0)) {
      fprintf(stream, "%s 0x%x\n", cmd, mlist[i].key);
    } else if (strcmp(cmd, "p") == 0) {
      fprintf(stream, "%s\n", cmd);
    } else {
      PFATAL("Unknown cmd %s", cmd);
    }
#elif defined(CLEVEL)
    if ((strcmp(cmd, "i") == 0) || (strcmp(cmd, "u") == 0)) {
      fprintf(stream, "%s 0x%x %s\n", cmd, mlist[i].key, val);
    } else if ((strcmp(cmd, "s") == 0) || (strcmp(cmd, "e") == 0)) {
      fprintf(stream, "%s 0x%x\n", cmd, mlist[i].key);
    } else {
      PFATAL("Unknown cmd %s", cmd);
    }
#elif defined(PMEMKV)
    if (strcmp(cmd, "i") == 0) {
      fprintf(stream, "%s 0x%x %s\n", cmd, mlist[i].key, val);
    } else if ((strcmp(cmd, "r") == 0) || (strcmp(cmd, "g") == 0)) {
      fprintf(stream, "%s 0x%x\n", cmd, mlist[i].key);
    } else if (strcmp(cmd, "d") == 0) {
      fprintf(stream, "%s 0 %d\n", cmd, rand() % 100);
    } else {
      PFATAL("Unknown cmd %s", cmd);
    }
#else
  FATAL("Workload undefined");
#endif
  }
}
