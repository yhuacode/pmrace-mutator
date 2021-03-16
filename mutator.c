#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afl-fuzz.h"
#include "custom_mutator_helpers.h"
#include "mainlist.h"

static const char *cmds[] =
#if defined(MEMCACHED)
    {"set",    "add",       "replace", "append", "prepend", "get",
     "delete", "flush_all", "incr",    "decr",   "stats"};
#elif defined(CLHT)
    {"i", "u", "d", "g"};
#elif defined(CCEH)
    {"i", "u", "g", "c"};
#elif defined(FAST_FAIR)
    {"i", "d", "s", "r", "p"};
#elif defined(CLEVEL)
    {"i", "u", "s", "e"};
#elif defined(PMEMKV)
    {"i", "r", "g", "d"};
#else
    {};
#endif

#define NUM_OF(array) (sizeof(array) / sizeof(typeof(array[0])))
#define RAND_OF(afl, array) (array[rand_below(afl, NUM_OF(array))])

static const char *stage_names[] = {"addition", "race",     "resize",
                                    "deletion", "mutation", "shuffling",
                                    "random",   "merge"};

enum {
  ADDITION_STAGE,
  RACE_STAGE,
  RESIZE_STAGE,
  DELETION_STAGE,
  MUTATION_STAGE,
  SHUFFLING_STAGE,
  RANDOM_STAGE,
  MERGE_STAGE,
  STAGE_NUM
};

typedef struct my_mutator {
  afl_state_t *afl;

  BUF_VAR(uint8_t, post_process);
  FILE *post_process_memstream;

  size_t thread_num;

  size_t cur_fuzzing_stage;
  size_t cur_fuzzing_step;

  size_t stage_threshold[STAGE_NUM];

  bool mainlist_out_dir_exist;

  mainlist_t mutated_mlist;
} my_mutator_t;

my_mutator_t *afl_custom_init(afl_state_t *afl, unsigned int seed) {
  my_mutator_t *data = (my_mutator_t *)calloc(1, sizeof(my_mutator_t));
  u8 *seeds_out_dir, *src_name, *dst_name,
      *in_dir = alloc_printf("%s/input", afl->out_dir);
  struct dirent **nl;
  int items = scandir((char *)afl->in_dir, &nl, NULL, NULL);
  size_t i;
  mainlist_t mlist = NULL;
  struct stat st;

#ifdef SIMPLE_FILES
  FATAL("SIMPLE_FILES definition detected");
#endif

  // pre process input dir
  ACTF("Pre-processing input dir: %s", afl->in_dir);

  if (mkdir((char *)in_dir, 0700) != 0) {
    PFATAL("Cannot create the output directory %s (afl_custom_init)",
           (char *)in_dir);
  }

  if (items > 0) {
    for (i = 0; i < (size_t)items; ++i) {
      src_name = alloc_printf("%s/%s", afl->in_dir, nl[i]->d_name);
      if (stat((char *)src_name, &st) == 0 && S_ISREG(st.st_mode) &&
          st.st_size) {
        mlist = parse_mlist_from_file((char *)src_name);
        dst_name = alloc_printf("%s/%s", in_dir, nl[i]->d_name);
        dump_mlist_to_file(mlist, (char *)dst_name);
        ck_free(dst_name);
      }
      ck_free(src_name);
      free(nl[i]);
    }

    afl->in_dir = in_dir;
    free(nl);
    ck_free(mlist);
  } else {
    PFATAL("Scan input dir failed");
  }

  srand(seed);

  if (!data) {
    PFATAL("afl_custom_init alloc");
  }

  data->thread_num = getenv(PMRACE_THREAD_NUM_ENV_VAR) == NULL
                         ? 4
                         : atoi(getenv(PMRACE_THREAD_NUM_ENV_VAR));

  data->afl = afl;

  data->post_process_memstream = open_memstream(
      (char **)&data->post_process_buf, &data->post_process_size);
  if (data->post_process_memstream == NULL) {
    PFATAL("Failed to open memstream");
  }

  // generate seeds for PMRace
  seeds_out_dir = alloc_printf("%s/seeds", afl->out_dir);
  if (mkdir((char *)seeds_out_dir, 0700) != 0) {
    PFATAL("Cannot create the output directory %s (afl_custom_init)",
           (char *)seeds_out_dir);
  }
  ck_free(seeds_out_dir);

  return data;
}

uint32_t afl_custom_fuzz_count(my_mutator_t *data, const uint8_t *buf,
                               size_t buf_size) {
  size_t cur_mlist_sz = ((const list_el_t *)buf)->size, i;

  // update stage steps
  data->cur_fuzzing_step = 0;
  data->cur_fuzzing_stage = 0;
  snprintf((char *)data->afl->stage_name_buf, STAGE_BUF_SIZE, "%s",
           stage_names[data->cur_fuzzing_stage]);
  data->afl->stage_name = data->afl->stage_name_buf;

  data->stage_threshold[ADDITION_STAGE] = MIN(50 * cur_mlist_sz, 1000);
  data->stage_threshold[RACE_STAGE] = 1;
  data->stage_threshold[RESIZE_STAGE] = MIN(10 * cur_mlist_sz, 100);
  data->stage_threshold[DELETION_STAGE] = MIN(50 * cur_mlist_sz, 1000);
  data->stage_threshold[MUTATION_STAGE] = MIN(50 * cur_mlist_sz, 1000);
  data->stage_threshold[SHUFFLING_STAGE] = MIN(100 * cur_mlist_sz, 1000);
  data->stage_threshold[RANDOM_STAGE] = 100;
  data->stage_threshold[MERGE_STAGE] = MIN(10 * (data->afl->queued_paths - 1), 100);

  for (i = 0; i < STAGE_NUM; ++i) {
    if (i > 0) {
      data->stage_threshold[i] += data->stage_threshold[i - 1];
    }
  }
  return data->stage_threshold[STAGE_NUM - 1];
}

// all the testcase are in (cmd, key, val, tid) form
size_t afl_custom_post_process(my_mutator_t *data, uint8_t *buf,
                               size_t buf_size, uint8_t **out_buf) {
  sublist_t *sublists =
      get_sublist_from_mlist((mainlist_t)buf, data->thread_num);
  size_t i;

  for (i = 0; i < data->thread_num; ++i) {
    dump_cmd_from_mlist(sublists[i], data->post_process_memstream);

    // use tab to split each thread's input
    fprintf(data->post_process_memstream, "\t");
    free(sublists[i]);
  }
  free(sublists);
  fprintf(data->post_process_memstream, "%c", 0);
  fflush(data->post_process_memstream);
  fseek(data->post_process_memstream, 0, SEEK_SET);
  *out_buf = data->post_process_buf;
  return data->post_process_size;
}

void afl_custom_queue_new_entry(my_mutator_t *data,
                                const uint8_t *filename_new_queue,
                                const uint8_t *filename_orig_queue) {
  FILE *fptr;
  size_t i;
  mainlist_t mlist;
  sublist_t *sublists;
  u8 *out_path;

  // if not read from initial test cases (i.e., from input directory)
  if (likely(filename_orig_queue != NULL)) {
    mlist = read_mlist_from_file((char *)filename_new_queue);
    sublists = get_sublist_from_mlist(mlist, data->thread_num);
    out_path = alloc_printf("%s/seeds/%s", data->afl->out_dir,
                            basename((char *)filename_new_queue));

    if ((fptr = fopen((char *)out_path, "w+")) == NULL) {
      PFATAL("Unable to create file");
    }
    ck_free(out_path);

    for (i = 0; i < data->thread_num; ++i) {
      dump_cmd_from_mlist(sublists[i], fptr);

      // use tab to split each thread's input
      fprintf(fptr, "\t");
      free(sublists[i]);
    }
    free(sublists);
    fclose(fptr);
  }
}

const char *afl_custom_describe(my_mutator_t *data,
                                size_t max_description_len) {
  return stage_names[data->cur_fuzzing_stage];
}

size_t afl_custom_fuzz(my_mutator_t *data, uint8_t *buf, size_t buf_size,
                       uint8_t **out_buf, uint8_t *add_buf, size_t add_buf_size,
                       size_t max_size) {
  mainlist_t add_mlist = (mainlist_t)add_buf, cur_mlist = (mainlist_t)buf;
  size_t interested_idx, cur_mlist_sz = cur_mlist->size, i, j, k, merged_sz;

  ++data->cur_fuzzing_step;
  // TODO: now we only mutate 1 operation during each fuzzing stage
  if (unlikely(data->mutated_mlist == NULL)) {
    data->mutated_mlist =
        (mainlist_t)calloc(cur_mlist_sz + 1, sizeof(list_el_t));
  } else if (cur_mlist_sz + 1 > data->mutated_mlist->size) {
    data->mutated_mlist = (mainlist_t)realloc(
        data->mutated_mlist, (cur_mlist_sz + 1) * sizeof(list_el_t));
  }

  switch (data->cur_fuzzing_stage) {
    case ADDITION_STAGE:
    // TODO
    case RACE_STAGE:
      interested_idx = rand_below(data->afl, cur_mlist_sz + 1);
      for (i = 0, j = 0; i <= cur_mlist_sz; ++i, ++j) {
        if (i == interested_idx) {
          data->mutated_mlist[i] = (list_el_t){
              .size = 0,
              .tid = rand_below(data->afl, data->thread_num),
              .key = cur_mlist[rand_below(data->afl, cur_mlist_sz)].key,
              .val = rand()};
          strcpy(data->mutated_mlist[i].cmd, RAND_OF(data->afl, cmds));
          ++i;
        }
        if (j < cur_mlist_sz) {
          data->mutated_mlist[i] = cur_mlist[j];
        }
      }
      data->mutated_mlist->size = cur_mlist_sz + 1;
      break;
    case RESIZE_STAGE:
      for (i = 0; i < cur_mlist_sz; ++i) {
        data->mutated_mlist[i] = cur_mlist[i];
        data->mutated_mlist[i].key = rand_below(data->afl, -1);
#if defined(MEMCACHED)
        strcpy(data->mutated_mlist[i].cmd, "set");
#else
        strcpy(data->mutated_mlist[i].cmd, "i");
#endif
      }
      break;
    case DELETION_STAGE:
      for (i = 0, j = 0; j < cur_mlist_sz; ++j) {
        if (rand_below(data->afl, 10) < 6) {
          data->mutated_mlist[i++] = cur_mlist[j];
        }
      }
      data->mutated_mlist->size = i;
      break;
    case MUTATION_STAGE:
      for (i = 0; i < cur_mlist_sz; ++i) {
        data->mutated_mlist[i] = cur_mlist[i];
        if (rand_below(data->afl, 10) < 6) {
          data->mutated_mlist[i].key =
              cur_mlist[rand_below(data->afl, cur_mlist_sz)].key;
        }
      }
      data->mutated_mlist->size = cur_mlist_sz;
      break;
    case SHUFFLING_STAGE:
      for (i = 0; i < cur_mlist_sz; ++i) {
        data->mutated_mlist[i] = cur_mlist[i];
        data->mutated_mlist[i].tid = rand_below(data->afl, data->thread_num);
      }
      data->mutated_mlist->size = cur_mlist_sz;
      break;
    case RANDOM_STAGE:
      for (i = 0; i < cur_mlist_sz; ++i) {
        data->mutated_mlist[i] = (list_el_t){
            .size = 0,
            .tid = rand_below(data->afl, data->thread_num),
            .key = cur_mlist[rand_below(data->afl, cur_mlist_sz)].key,
            .val = rand()};
        strcpy(data->mutated_mlist[i].cmd, RAND_OF(data->afl, cmds));
      }
      data->mutated_mlist->size = cur_mlist_sz;
      break;
    case MERGE_STAGE:
      if (add_buf != NULL) {
        merged_sz = data->mutated_mlist->size + add_mlist->size;

        // merge two testcase
        mainlist_t merged_mlist =
            (mainlist_t)calloc(merged_sz, sizeof(list_el_t));
        for (k = 0, i = 0, j = 0; k < merged_sz; ++k) {
          if ((rand_below(data->afl, 2) == 0 ||
               j >= data->mutated_mlist->size) &&
              i < add_mlist->size) {
            merged_mlist[k] = add_mlist[i++];
          } else {
            merged_mlist[k] = data->mutated_mlist[j++];
          }
        }
        merged_mlist->size = merged_sz;
        free(data->mutated_mlist);
        data->mutated_mlist = merged_mlist;
      }
      break;
    default:
      PFATAL("Unknown stage in (afl_custom_fuzz)");
  }

  if (data->cur_fuzzing_step ==
      data->stage_threshold[data->cur_fuzzing_stage]) {
    ++data->cur_fuzzing_stage;
    snprintf((char *)data->afl->stage_name_buf, STAGE_BUF_SIZE, "%s",
             stage_names[data->cur_fuzzing_stage]);
    data->afl->stage_name = data->afl->stage_name_buf;
  }

  *out_buf = (uint8_t *)data->mutated_mlist;
  return data->mutated_mlist->size * sizeof(list_el_t);
}

void afl_custom_deinit(my_mutator_t *data) {
  fclose(data->post_process_memstream);
  free(data->post_process_buf);
  free(data->mutated_mlist);
  free(data);
}
