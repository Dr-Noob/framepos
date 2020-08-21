#include <stdio.h>
#include <stdbool.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

#define MAX_PIX_FMT_STR_LENGTH 10
#define N_FRAMES_DEBUG        100

bool write_frame_yuv(AVFrame *f, char* output_path) {
  FILE *fp;
  uint32_t n = av_image_get_buffer_size(f->format, f->width, f->height, 16);
  uint32_t ret;
  
  if((fp = fopen(output_path, "w")) == NULL) {
    perror("fopen");
    return false;
  }
  
  
  if((ret = fwrite(f->data[0], sizeof(uint8_t), n, fp)) < n) {
    printf("fwrite: Wrote %d, expected to write %d\n", ret, n); 
    return false;    
  }
  
  if(fclose(fp) != 0) {
    perror("fclose");
    return false;    
  }
  
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
  AVFormatContext *fmt_ctx = NULL;
  AVFrame *fr = NULL;
  AVPacket pkt;
  
  int video_stream;
  int got_frame = 0;
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
    
  av_get_pix_fmt_string(buf, MAX_PIX_FMT_STR_LENGTH, ctx->pix_fmt);
  printf("Input PIX_FMT: %s\n", buf);
  if(ctx->pix_fmt != AV_PIX_FMT_YUV420P) printf("WARNING: %s was not designed to work with format different than yuv420p\n", argv[0]);
  
  if ((fr = av_frame_alloc()) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "av_frame_alloc\n");
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
      if (pkt.pts == AV_NOPTS_VALUE) pkt.pts = i;
      result = avcodec_decode_video2(ctx, fr, &got_frame, &pkt);
      if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
        return result;
      }     
      if (got_frame) {
        current_frame++;  
        if(current_frame % N_FRAMES_DEBUG == 0) { 
          printf("\r%d decoded frames...", current_frame);
          fflush(stdout);
        }
        if (frame_num == current_frame) { 
          if(!write_frame_yuv(fr, output_path)) 
            return EXIT_FAILURE;
          printf("\nFrame %d cut successfully\n", frame_num);
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
}
