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

bool write_frame_yuv(AVFrame *f, char* output_path) {
  FILE *fp = fopen(output_path, "wb");
  if(!fp) return false;
  
  int size = av_image_get_buffer_size(f->format, f->width, f->height, 16);
  fwrite(f->data[0], 1, size, fp);
  
  fclose(fp);
  
  return true;
}

int main(int argc, char **argv) {  
  if (argc < 3) {
    printf("Usage: %s input_video frame output_img\n", argv[0]);
    return EXIT_FAILURE;
  }
    
  char* video_path = argv[1];
  int frame_num = atoi(argv[2]);
  char* output_path = argv[3];
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
      if (pkt.pts == AV_NOPTS_VALUE) pkt.pts = pkt.dts = i;
      result = avcodec_decode_video2(ctx, fr, &got_frame, &pkt);
      if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
        return result;
      }     
      if (got_frame) {
        current_frame++;        
        if (frame_num == current_frame) {
          number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                   (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                   ctx->pix_fmt, ctx->width, ctx->height, 1);
          if (number_of_written_bytes < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_image_copy_to_buffer\n");
            return EXIT_FAILURE;
          }                        
          write_frame_yuv(fr, output_path);                        
          end = true;
        }         
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
