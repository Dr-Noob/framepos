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

// Naive images_equal (needs intrinsics to vectorice!)
bool images_equal(AVFrame *f1, AVFrame *f2) {
  if(f1->width != f2->width || f1->height != f2->height) return false;
  
  uint8_t *img1 = f1->data[0];
  uint8_t *img2 = f2->data[0];
  
  for(int i=0; i < f1->width * f1->height; i++) {
    if(img1[i] != img2[i])return false;
  }
  
  return true;
}

AVFrame *read_frame_yuv(char* input_path) {
  FILE *fp = fopen(input_path, "r");
  if(!fp) return NULL;
  
  AVFrame* dst = av_frame_alloc();
  int width = 1920;
  int height = 1080;
  enum AVPixelFormat dst_pixfmt = AV_PIX_FMT_YUV420P;
  int numBytes = avpicture_get_size(dst_pixfmt, width, height);
  uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
  avpicture_fill( (AVPicture *)dst, buffer, dst_pixfmt, width, height);
  
  fread(dst->data[0], 1, numBytes, fp);
  dst->format = (int)dst_pixfmt;
  dst->width = width;
  dst->height = height;
  
  fclose(fp);
  
  return dst;
}

int main(int argc, char **argv) {  
  if (argc < 3) {
    printf("Usage: %s input_video input_img\n", argv[0]);
    return EXIT_FAILURE;
  }
  
  char* video_path = argv[1];
  char* image_path = argv[2];
  AVCodec *codec = NULL;
  AVCodecContext *ctx= NULL;
  AVCodecParameters *origin_par = NULL;
  AVFrame *fr = NULL;
  uint8_t *byte_buffer = NULL;
  AVPacket pkt;
  AVFormatContext *fmt_ctx = NULL;
  int number_of_written_bytes;
  int video_stream;
  int got_frame = 0;
  int byte_buffer_size;
  int i = 0;
  int current_frame = 0;
  int result;
  bool end = 0;
  AVFrame * img = read_frame_yuv(image_path);
 
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
  
  if ((fr = av_frame_alloc()) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_frame_alloc\n");
    return EXIT_FAILURE;
  }

  byte_buffer_size = av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 16);
  if ((byte_buffer = av_malloc(byte_buffer_size)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_malloc\n");
    return EXIT_FAILURE;
  }
  
  assert(ctx->pix_fmt == AV_PIX_FMT_YUV420P);

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
            printf("Frame %d ", current_frame);
            if(images_equal(fr, img)) printf("MATCHES\n");
            else printf("\n");
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
