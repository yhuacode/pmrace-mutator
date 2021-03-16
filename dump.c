#include "mainlist.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s path threadNum\n", argv[0]);
    exit(1);
  }

  mainlist_t mlist = read_mlist_from_file(argv[1]);
  sublist_t *sublists = get_sublist_from_mlist(mlist, atoi(argv[2]));
  int i;

  for (i = 0; i < atoi(argv[2]); ++i) { 
    dump_cmd_from_mlist(sublists[i], stdout);
    printf("\t");
    free(sublists[i]);
  }
  free(sublists);
  return 0;
}
