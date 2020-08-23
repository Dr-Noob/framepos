CXX=gcc

CXXFLAGS=-O0 -g -std=c99
SANITY_FLAGS=-Wall -Wextra -Werror -pedantic -fstack-protector-all -pedantic -Wfloat-equal -Wshadow -Wpointer-arith -Wstrict-overflow=5 -Wformat=2

# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=libavdevice                        \
            libavformat                        \
            libavfilter                        \
            libavcodec                         \
            libswresample                      \
            libswscale                         \
            libavutil                          \
            
CFLAGS := $(shell pkg-config --cflags $(FFMPEG_LIBS))
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS)) -fopenmp
FRAMEPOS=framepos

all: $(FRAMEPOS)

$(FRAMEPOS): Makefile $(FRAMEPOS).c args.c args.h
	$(CXX) $(CXXFLAGS) $(SANITY_FLAGS) $(CFLAGS) $(LDLIBS) $(FRAMEPOS).c args.c -o $@

clean:
	@rm $(FRAMEPOS) $(CUTFRAME)
