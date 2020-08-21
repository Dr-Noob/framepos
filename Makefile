CXX=gcc

CXXFLAGS=-O2 -std=c99 -Wno-deprecated-declarations
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
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS))
FRAMEPOS=framepos
CUTFRAME=cutframe

all: $(FRAMEPOS) $(CUTFRAME)

$(FRAMEPOS): Makefile $(FRAMEPOS).c
	$(CXX) $(CXXFLAGS) $(SANITY_FLAGS) $(CFLAGS) $(LDLIBS) $(FRAMEPOS).c -o $@
	
$(CUTFRAME): Makefile $(CUTFRAME).c
	$(CXX) $(CXXFLAGS) $(SANITY_FLAGS) $(CFLAGS) $(LDLIBS) $(CUTFRAME).c -o $@

clean:
	@rm $(FRAMEPOS) $(CUTFRAME)
