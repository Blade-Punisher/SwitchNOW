#pragma once

#include <cstdint>
#include <string>

#define VIDEO_FORMAT_H264 0x01
#define VIDEO_FORMAT_H265 0x02
#define VIDEO_FORMAT_H265_MAIN10 0x04
#define VIDEO_FORMAT_AV1_MAIN8 0x08
#define VIDEO_FORMAT_AV1_MAIN10 0x10

#define VIDEO_FORMAT_MASK_H264 0x01
#define VIDEO_FORMAT_MASK_H265 0x06
#define VIDEO_FORMAT_MASK_10BIT 0x14

#define VIDEO_DECODER_PREFER_HARDWARE 0x01
#define VIDEO_DECODER_FORCE_SOFTWARE 0x02
#define VIDEO_DECODER_REQUIRE_HARDWARE 0x04
#define VIDEO_DECODER_COPY_HARDWARE_FRAMES 0x08

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVFrame;
struct AVPacket;
struct AVBufferRef;
struct AVCodec;
struct AVCodecContext;

struct VideoDecodeStats {
    // NOT TO USE, INTERMEDIATE VALUES
    uint32_t current_received_frames;
    uint32_t current_decoded_frames;
    uint32_t total_frames;
    uint32_t network_dropped_frames;
    uint32_t current_reassembly_time;
    uint32_t current_decode_time;
    uint32_t total_received_frames;
    uint32_t total_decoded_frames;
    uint32_t total_reassembly_time;
    uint32_t total_decode_time;

    float current_host_fps;
    float current_received_fps;
    float current_decoded_fps;

    float current_receive_time;
    float current_decoding_time;

    float session_receive_time;
    float session_decoding_time;

    uint64_t measurement_start_timestamp;
};

class IFFmpegVideoDecoder {
  public:
    virtual ~IFFmpegVideoDecoder()= default;
    virtual int setup(int video_format, int width, int height, int redraw_rate,
                      void* context, int dr_flags) = 0;
    virtual void start(){};
    virtual void stop(){};
    virtual void cleanup() = 0;
    virtual int submit_decode_unit(uint8_t* indata, int inlen, int64_t pts = AV_NOPTS_VALUE) = 0;
    virtual int capabilities() const = 0;
    virtual VideoDecodeStats* video_decode_stats() = 0;
    virtual bool uses_hardware_decoder() const = 0;
    virtual bool uses_hardware_frames() const = 0;
    virtual const std::string& setup_diagnostics() const = 0;
    virtual uint64_t corrupt_frames_dropped() const = 0;
};
