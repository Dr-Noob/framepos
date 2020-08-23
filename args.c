#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>

#include "args.h"

#define ARG_STR_VIDEO   "video"
#define ARG_STR_IMAGE   "image"
#define ARG_STR_THREADS "threads"
#define ARG_STR_HELP    "help"

#define ARG_CHAR_VIDEO   'v'
#define ARG_CHAR_IMAGE   'i'
#define ARG_CHAR_THREADS 't'
#define ARG_CHAR_HELP    'h'

struct args_struct {
  char* video_path;
  char* image_path;
  int n_threads;
  bool help_flag;  
};

static struct args_struct args;

char* get_video_path() {
  return args.video_path;
}

char* get_image_path() {
  return args.image_path;
}

int get_n_threads() {
  return args.n_threads;
}

bool show_help() {
  return args.help_flag;
}

bool parse_args(int argc, char* argv[]) {
  int c;
  int option_index = 0;  
  opterr = 0;

  args.video_path = NULL;
  args.image_path = NULL;
  args.n_threads = -1;
  args.help_flag = false;

  static struct option long_options[] = {
      {ARG_STR_VIDEO,    required_argument, 0, ARG_CHAR_VIDEO   },
      {ARG_STR_IMAGE,    required_argument, 0, ARG_CHAR_IMAGE   },
      {ARG_STR_THREADS,  required_argument, 0, ARG_CHAR_THREADS },
      {ARG_STR_HELP,     no_argument,       0, ARG_CHAR_HELP    },
      {0, 0, 0, 0}
  };

  c = getopt_long(argc, argv, "", long_options, &option_index);

  while (c != -1) {
     if(c == ARG_CHAR_VIDEO) {
       args.video_path = optarg;
     }
     else if(c == ARG_CHAR_IMAGE) {
       args.image_path = optarg;
     }
     else if(c == ARG_CHAR_THREADS) {
       args.n_threads = atoi(optarg);
       if(args.n_threads < 1) {
         printf("Invalid number of threads specified: %d\n", args.n_threads);
         return false;    
       }
     }
     else if(c == ARG_CHAR_HELP) {
       args.help_flag  = true;
       return true;
     }
     else if(c == '?') {
       printf("Invalid options!\n");
       args.help_flag  = true;
       return true;
     }
     else {
       printf("Bug\n");
       return false;
     }

    option_index = 0;
    c = getopt_long(argc, argv,"",long_options, &option_index);
  }

  if (optind < argc) {
    printf("Invalid options!\n");
    args.help_flag  = true;
    return true;
  }

  if(args.video_path == NULL) {
    printf("Missing mandatory argument: --video\n");
    args.help_flag  = true;
    return true;    
  }
  if(args.image_path == NULL) {
    printf("Missing mandatory argument: --image\n");
    args.help_flag  = true;
    return true;    
  }
  if(args.n_threads == -1) {
    args.n_threads = omp_get_max_threads();    
  }

  return true;
}
