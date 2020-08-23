#ifndef __ARGS__
#define __ARGS__

#include <stdbool.h>

bool parse_args(int argc, char* argv[]);
bool show_help();
char* get_video_path();
char* get_image_path();
int get_n_threads();

#endif
