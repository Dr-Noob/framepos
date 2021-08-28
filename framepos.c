#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

#include "args.h"

#define MAX_PIX_FMT_STR_LENGTH 10
#define N_FRAMES_DEBUG       1000
#define THRESHOLD             100

void writePlane(FILE *f, uint8_t *data, ptrdiff_t linesize, int w, int h) {
  for (int y = 0; y < h; y++) {
    fwrite(data, 1, w, f);
    data += linesize;
  }
}

bool write_frame_yuv(AVFrame *f, char* output_path) {
  FILE *fp = fopen(output_path, "wb");
  if(!fp) return false;

  writePlane(fp, f->data[0], f->linesize[0], f->width, f->height);
  writePlane(fp, f->data[1], f->linesize[1], f->width / 2, f->height / 2);
  writePlane(fp, f->data[2], f->linesize[2], f->width / 2, f->height / 2);

  fclose(fp);

  return true;
}

// Naive images_equal (needs intrinsics to vectorice!)
bool images_equal(AVFrame *f1, AVFrame *f2) {
  uint8_t *img1 = f1->data[0];
  uint8_t *img2 = f2->data[0];
  
  for(int i=0; i < f1->width * f1->height; i++) {
    if(abs(img1[i]-img2[i]) > THRESHOLD)return false;
  }
  
  return true;
}

AVFrame *read_frame_yuv(char* input_path, int w, int h) {
  FILE *fp;
  AVFrame* dst;
  uint8_t *buffer;
  int ret = 0;
  enum AVPixelFormat dst_pixfmt = AV_PIX_FMT_YUV420P;

  if((fp = fopen(input_path, "r")) == NULL) {
    perror("fopen");
    return NULL;
  }

  if ((dst = av_frame_alloc()) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_frame_alloc\n");
    return NULL;
  }

  int n = av_image_get_buffer_size(dst_pixfmt, w, h, 32);

  buffer = av_malloc(n * sizeof(uint8_t));
  av_image_fill_arrays(dst->data, dst->linesize, buffer, dst_pixfmt, w, h, 32);

  if((ret = fread(dst->data[0], sizeof(uint8_t), n, fp)) < n) {
    printf("fread: Read %d, expected to read %d\n", ret, n);
    return NULL;
  }

  dst->format = dst_pixfmt;
  dst->width = w;
  dst->height = h;

  if(fclose(fp) != 0) {
    perror("fclose");
    return NULL;
  }

  return dst;
}

#define MPCLAMP(a, min, max) (((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))

double get_current_time(AVStream *stream, AVFrame* fr) { return (double) fr->pts * av_q2d(stream->time_base); }
double get_time_length(AVFormatContext* fmt_ctx) { return (double) fmt_ctx->duration / AV_TIME_BASE; }

double get_current_pos_ratio(AVStream *stream, AVFrame* fr, AVFormatContext* fmt_ctx) {
  double start = 0;
  double pos = get_current_time(stream, fr);
  double len = get_time_length(fmt_ctx);

  double ans = MPCLAMP((pos - start) / len, 0, 1);

  return ans;
}

int get_estimated_frame_number(AVStream *stream, AVFrame* fr, AVFormatContext* fmt_ctx, double fps) {
  int64_t frames = get_time_length(fmt_ctx) * fps;
  return lrint(get_current_pos_ratio(stream, fr, fmt_ctx) * frames);
}

void print_images_equal(int img, int64_t pts, int frame) {
  // format XX:XX:XX.XXX
  char* time = malloc(sizeof(char) * 13);

  int total_secs = pts / 1000;
  int total_min = total_secs / 60;

  uint16_t ms = pts % 1000;
  uint8_t seg = total_secs % 60;
  uint8_t min = total_min % 60;
  uint8_t hou = total_min / 60;

  assert(ms < 1000);
  assert(seg < 60);
  assert(min < 60);
  assert(hou < 60);

  sprintf(time, "%02d:%02d:%02d.%03d", hou, min, seg, ms);

  printf("\r[IMG %d]: %s (frame %d)\n", img, time, frame);

  free(time);
}

void print_help(char *argv[]) {
  printf("Usage: %s [OPTIONS]...\n\
Options: \n\
  --video    [MANDATORY] Path to the input video. \n\
  --image    [MANDATORY] Path to the input image, which must be in YUV raw format. The specific \n\
                         YUV encoding must match the input video's. E.g, if video is yuv420p,   \n\
                         image must be in yuv420p. Image and video's dimensions must match.     \n\
  --threads  [OPTIONAL]  Set the number of threads to use in the decoder. Default value is the  \n\
                         max number of threads supported in the system. \n\
  --help     [OPTIONAL]  Prints this help and exit \n",

  argv[0]);
}

int main(int argc, char **argv) {
  if(!parse_args(argc,argv))
    return EXIT_FAILURE;

  if(show_help()) {
    print_help(argv);
    return EXIT_SUCCESS;
  }

  char* video_path = get_video_path();
  char** images_paths = get_images_paths();
  int n_threads = get_n_threads();
  int n_images = get_n_images();

  AVCodecContext *ctx= NULL;
  AVCodecParameters *origin_par = NULL;
  AVFormatContext *fmt_ctx = NULL;
  AVFrame *fr = NULL;
  AVFrame **imgs = malloc(sizeof(AVFrame *) * n_images);
  AVPacket pkt;
  AVStream *stream;

  uint8_t *byte_buffer = NULL;
  int number_of_written_bytes;
  int video_stream;
  int byte_buffer_size;
  int current_frame = 0;
  int result;
  double fps;
  bool prev_frame_matches[n_images];
  char* buf = malloc(sizeof(char) * MAX_PIX_FMT_STR_LENGTH);

  av_log_set_level(AV_LOG_QUIET);

  if ((result = avformat_open_input(&fmt_ctx, video_path, NULL, NULL)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "avformat_open_input\n");
    return EXIT_FAILURE;
  }

  if ((result = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info\n");
    return EXIT_FAILURE;
  }

  if ((video_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "av_find_best_stream\n");
    return EXIT_FAILURE;
  }

  origin_par = fmt_ctx->streams[video_stream]->codecpar;
  stream = fmt_ctx->streams[video_stream];

  if ((fmt_ctx->video_codec = avcodec_find_decoder(origin_par->codec_id)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_find_decoder\n");
    return EXIT_FAILURE;
  }

  if ((ctx = avcodec_alloc_context3(fmt_ctx->video_codec)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3\n");
    return EXIT_FAILURE;
  }

  if ((result = avcodec_parameters_to_context(ctx, origin_par)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context\n");
    return EXIT_FAILURE;
  }

  ctx->thread_count = n_threads;
  ctx->thread_type = FF_THREAD_FRAME; // FF_THREAD_FRAME seems to be better than FF_THREAD_SLICE

  if ((result = avcodec_open2(ctx, fmt_ctx->video_codec, NULL)) < 0) {
    av_log(ctx, AV_LOG_ERROR, "avcodec_open2\n");
    return EXIT_FAILURE;
  }

  fps = av_q2d(av_guess_frame_rate(fmt_ctx, fmt_ctx->streams[video_stream], NULL));

  printf("%s dimensions: %dx%d\n", video_path, ctx->width, ctx->height);

  for(int i=0; i < n_images; i++) {
    printf("Assuming that %s has the same dimensions...\n", images_paths[i]);
    prev_frame_matches[i] = false;

    if((imgs[i] = read_frame_yuv(images_paths[i], ctx->width, ctx->height)) == NULL) {
      return EXIT_FAILURE;
    }
  }

  printf("Loaded %d images\n", n_images);

  if ((fr = av_frame_alloc()) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_frame_alloc\n");
    return EXIT_FAILURE;
  }

  byte_buffer_size = av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 16);
  if ((byte_buffer = av_malloc(byte_buffer_size)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_malloc\n");
    return EXIT_FAILURE;
  }

  printf("Using codec: %s\n", fmt_ctx->video_codec->name);
  av_get_pix_fmt_string(buf, MAX_PIX_FMT_STR_LENGTH, ctx->pix_fmt);
  printf("Input video: %ffps\n", fps);
  printf("Input video PIX_FMT: %s\n", buf);
  if(ctx->pix_fmt != AV_PIX_FMT_YUV420P) printf("WARNING: %s was not designed to work with format different than yuv420p\n", argv[0]);

  av_init_packet(&pkt);

  printf("Using %d threads to decode\n", n_threads);

  // https://stackoverflow.com/questions/44711921/ffmpeg-failed-to-call-avcodec-send-packet
  while (av_read_frame(fmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index == video_stream) {
      result = avcodec_send_packet(ctx, &pkt);

      if (result < 0 || result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet\n");
        return EXIT_FAILURE;
      }

      while((result = avcodec_receive_frame(ctx, fr)) >= 0) {
        current_frame = get_estimated_frame_number(stream, fr, fmt_ctx, fps);

        if(current_frame % N_FRAMES_DEBUG == 0) {
          printf("\r%d decoded frames...", current_frame);
          fflush(stdout);
        }

        number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                 (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                 ctx->pix_fmt, ctx->width, ctx->height, 1);
        if (number_of_written_bytes < 0) {
          av_log(NULL, AV_LOG_ERROR, "av_image_copy_to_buffer\n");
          return EXIT_FAILURE;
        }
        for(int i=0; i < n_images; i++) {
          if(images_equal(fr, imgs[i])) {
            if(!prev_frame_matches[i]) {
              prev_frame_matches[i] = true;
              print_images_equal(i, fr->pts, current_frame);
            }
          }
          else {
            prev_frame_matches[i] = false;
          }
        }
      }

      if(result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame\n");
        return EXIT_FAILURE;
      }
    }

    av_packet_unref(&pkt);
  }

  av_packet_unref(&pkt);
  av_frame_free(&fr);
  avformat_close_input(&fmt_ctx);
  avcodec_free_context(&ctx);
  av_freep(&byte_buffer);
}
