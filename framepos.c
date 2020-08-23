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
  
  int size = av_image_get_buffer_size(f->format, f->width, f->height, 16);
  
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

int main(int argc, char **argv) {  
  if (argc < 5) {
    printf("Usage: %s input_video input_img image_width image_height\n", argv[0]);
    return EXIT_FAILURE;
  }
  
  char* video_path = argv[1];
  char* image_path = argv[2];
  int image_width = atoi(argv[3]);
  int image_height = atoi(argv[4]);
    
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
  int current_frame = 0;
  int result;
  bool end = 0;  
  int n_threads = 8;
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
  
  printf("Using codec: %s\n", fmt_ctx->video_codec->name);
  av_get_pix_fmt_string(buf, MAX_PIX_FMT_STR_LENGTH, ctx->pix_fmt);
  printf("Input video PIX_FMT: %s\n", buf);
  if(ctx->pix_fmt != AV_PIX_FMT_YUV420P) printf("WARNING: %s was not designed to work with format different than yuv420p\n", argv[0]);

  av_init_packet(&pkt);
  
  // https://stackoverflow.com/questions/44711921/ffmpeg-failed-to-call-avcodec-send-packet
  while (av_read_frame(fmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index == video_stream) {
      result = avcodec_send_packet(ctx, &pkt);
      
      if (result < 0 || result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet\n");
        return EXIT_FAILURE;
      }
        
      while((result = avcodec_receive_frame(ctx, fr)) >= 0) {                                           
        current_frame++; 
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
        if(images_equal(fr, img)) {
          printf("FRAME %d MATCHES\n", current_frame);                
          return EXIT_SUCCESS;
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
