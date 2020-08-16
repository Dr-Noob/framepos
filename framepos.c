#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

bool write_frame_jpg(AVFrame *pFrame, const char *out_file) {
  int width = pFrame->width;
  int height = pFrame->height;

  AVFormatContext* pFormatCtx = avformat_alloc_context();
  AVStream* pAVStream = NULL;
    
  pFormatCtx->oformat = av_guess_format("mjpeg", NULL, NULL);
  if(avio_open(&pFormatCtx->pb, out_file, AVIO_FLAG_READ_WRITE) < 0) {
    av_log(NULL, AV_LOG_ERROR, "avio_open\n");
    return false;
  }
    
  if((pAVStream = avformat_new_stream(pFormatCtx, 0)) == NULL ) {
    av_log(NULL, AV_LOG_ERROR, "avformat_new_stream\n");
    return false;
  }

  AVCodecContext* pCodecCtx = pAVStream->codec;
  pCodecCtx->codec_id = pFormatCtx->oformat->video_codec;
  pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
  pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
  pCodecCtx->width = width;
  pCodecCtx->height = height;
  pCodecCtx->time_base.num = 1;
  pCodecCtx->time_base.den = 25;

  av_dump_format(pFormatCtx, 0, out_file, 1);

  AVCodec* pCodec = NULL;
    
  if((pCodec = avcodec_find_encoder(pCodecCtx->codec_id)) == NULL) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_find_encoder\n");
    return false;
  }
  if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0 ) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_open2\n");
    return false;
  }

  //Write Header
  if(avformat_write_header(pFormatCtx, NULL) < 0) {
    av_log(NULL, AV_LOG_ERROR, "avformat_write_header\n");
    return false;    
  }

  int y_size = pCodecCtx->width * pCodecCtx->height;

  AVPacket pkt;
  av_new_packet(&pkt, y_size * 3);

  int got_picture = 0;
  if(avcodec_encode_video2(pCodecCtx, &pkt, pFrame, &got_picture) < 0 ) {
    av_log(NULL, AV_LOG_ERROR, "avcodec_encode_video2\n");
    return false;
  }
  if(got_picture == 1) {
    av_write_frame(pFormatCtx, &pkt);
  }

  av_free_packet(&pkt);

  av_write_trailer(pFormatCtx);
    
  if(pAVStream) {
    avcodec_close(pAVStream->codec);
  }
  avio_close(pFormatCtx->pb);
  avformat_free_context(pFormatCtx);

  return true;
}

int main(int argc, char **argv) {
  char* filename;
  
  if (argc < 2) {
    printf("Missing filename!\n");
    return EXIT_FAILURE;
  }
  
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
  int result;
  bool end = 0;
  filename = argv[1];
 
  if ((result = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
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

  printf("#tb %d: %d/%d\n", video_stream, fmt_ctx->streams[video_stream]->time_base.num, fmt_ctx->streams[video_stream]->time_base.den);
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
            printf("Frame %d\n", i);
            //write_frame_jpg(fr, "test.jpg");
            number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                    (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                    ctx->pix_fmt, ctx->width, ctx->height, 1);
            if (number_of_written_bytes < 0) {
              av_log(NULL, AV_LOG_ERROR, "av_image_copy_to_buffer\n");
              return EXIT_FAILURE;
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
