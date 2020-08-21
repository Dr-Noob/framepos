#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <png.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <jpeglib.h>
#include <assert.h>

#define MAX_PIX_FMT_STR_LENGTH 10

// Naive images_equal (needs intrinsics to vectorice!)
bool images_equal(AVFrame *f1, AVFrame *f2) {
  uint8_t *img1 = f1->data[0];
  uint8_t *img2 = f2->data[0];
  
  for(int i=0; i < f1->width * f1->height; i++) {
    if(img1[i] != img2[i])return false;
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
  
  int n = avpicture_get_size(dst_pixfmt, w, h);
  
  buffer = av_malloc(n * sizeof(uint8_t));
  avpicture_fill((AVPicture *)dst, buffer, dst_pixfmt, w, h);  
  
  
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

int main(int argc, char **argv) {  
  if (argc < 5) {
    printf("Usage: %s input_video input_img image_width image_height\n", argv[0]);
    return EXIT_FAILURE;
  }
  
  char* video_path = argv[1];
  char* image_path = argv[2];
  int image_width = atoi(argv[3]);
  int image_height = atoi(argv[4]);
  
  AVCodec *codec = NULL;
  AVCodecContext *ctx= NULL;
  AVCodecParameters *origin_par = NULL;  
  AVFormatContext *fmt_ctx = NULL;  
  AVFrame *fr = NULL;
  AVFrame *img = read_frame_yuv(image_path, image_width, image_height);
  AVPacket pkt;
  
  if(img == NULL)
    return EXIT_FAILURE;
  
  uint8_t *byte_buffer = NULL;
  int number_of_written_bytes;
  int video_stream;
  int got_frame = 0;
  int byte_buffer_size;
  int i = 0;
  int current_frame = 0;
  int result;
  bool end = 0;  
  char* buf = malloc(sizeof(char) * MAX_PIX_FMT_STR_LENGTH);
 
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

  if ((codec = avcodec_find_decoder(origin_par->codec_id)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_find_decoder\n");
    return EXIT_FAILURE;
  }

  if ((ctx = avcodec_alloc_context3(codec)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3\n");
    return EXIT_FAILURE;
  }

  if ((result = avcodec_parameters_to_context(ctx, origin_par)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context\n");
    return EXIT_FAILURE;
  }

  if ((result = avcodec_open2(ctx, codec, NULL)) < 0) {
    av_log(ctx, AV_LOG_ERROR, "avcodec_open2\n");
    return EXIT_FAILURE;
  }
  
  if(ctx->width != image_width || ctx->height != image_height) {
    printf("Input video and image dimensions do not match: %dx%d vs %dx%d\n", ctx->width, ctx->height, image_width, image_height);
    return EXIT_FAILURE;
  }
  
  if ((fr = av_frame_alloc()) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_frame_alloc\n");
    return EXIT_FAILURE;
  }

  byte_buffer_size = av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 16);
  if ((byte_buffer = av_malloc(byte_buffer_size)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_malloc\n");
    return EXIT_FAILURE;
  }
  
  av_get_pix_fmt_string(buf, MAX_PIX_FMT_STR_LENGTH, ctx->pix_fmt);
  printf("Input video PIX_FMT: %s\n", buf);
  if(ctx->pix_fmt != AV_PIX_FMT_YUV420P) printf("WARNING: %s was not designed to work with format different than yuv420p\n", argv[0]);

  i = 0;
  av_init_packet(&pkt);
  do {
    if (!end) {
      if (av_read_frame(fmt_ctx, &pkt) < 0) {
        end = true;
      }
    }
    if (end) {
        pkt.data = NULL;
        pkt.size = 0;
    }
    if (pkt.stream_index == video_stream || end) {
        got_frame = 0;
        if (pkt.pts == AV_NOPTS_VALUE)
            pkt.pts = pkt.dts = i;
        result = avcodec_decode_video2(ctx, fr, &got_frame, &pkt);
        if (result < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
            return result;
        }
        if (got_frame) {    
            current_frame++; 
            number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                    (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                    ctx->pix_fmt, ctx->width, ctx->height, 1);
            if (number_of_written_bytes < 0) {
              av_log(NULL, AV_LOG_ERROR, "av_image_copy_to_buffer\n");
              return EXIT_FAILURE;
            }            
            if(images_equal(fr, img))
              printf("%d\n", current_frame);
        }
        av_packet_unref(&pkt);
        av_init_packet(&pkt);
    }
    i++;
  } while (!end || got_frame);

  av_packet_unref(&pkt);
  av_frame_free(&fr);
  avformat_close_input(&fmt_ctx);
  avcodec_free_context(&ctx);
  av_freep(&byte_buffer);
}
