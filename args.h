#ifndef __ARGS__
#define __ARGS__

#include <stdbool.h>

#define MAX_IMAGES_PATHS 50

bool parse_args(int argc, char* argv[]);
bool show_help();
char* get_video_path();
char** get_images_paths();
int get_n_threads();
int get_n_images();

#endif
