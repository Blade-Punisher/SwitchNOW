#pragma once

#include <nanovg.h>
#include <cstdint>

#define COLORSPACE_REC_601 1
#define COLORSPACE_REC_709 2
#define COLORSPACE_REC_2020 3
#define COLOR_RANGE_LIMITED 1
#define COLOR_RANGE_FULL 2

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

struct AVFrame;

struct VideoRenderStats {
    // NOT TO USE, INTERMEDIATE VALUES
    uint32_t rendered_frames;
    uint64_t total_render_time;

    float rendered_fps;
    float rendering_time;

    uint64_t measurement_start_timestamp;
    uint32_t retained_surfaces;
    uint32_t surface_mappings;
    uint64_t upload_count = 0;
    uint64_t upload_cpu_us_total = 0;
    uint64_t upload_cpu_us_max = 0;
    uint64_t upload_memcpy_us_total = 0, upload_memcpy_us_max = 0;
    uint64_t slot_fence_wait_us_total = 0, slot_fence_wait_us_max = 0;
    uint64_t command_fence_wait_us_total = 0, command_fence_wait_us_max = 0;
    uint64_t descriptor_updates = 0, update_submissions = 0;
    bool immutable_upload_descriptors = false;
};

class IVideoRenderer {
  public:
    virtual ~IVideoRenderer(){};
    virtual void draw(NVGcontext* vg, int width, int height,
                      AVFrame* frame, int imageFormat) = 0;
    virtual void drawLatest(NVGcontext* vg, int width, int height,
                            AVFrame* frame, int imageFormat, uint64_t generation) {
        (void)generation;
        draw(vg, width, height, frame, imageFormat);
    }
    virtual VideoRenderStats* video_render_stats() = 0;

    // Default implementations
    virtual int getDecoderColorspace() {
        // Rec 601 is default
        return COLORSPACE_REC_601;
    }

    virtual int getDecoderColorRange() {
        // Limited is the default
        return COLOR_RANGE_LIMITED;
    }

    virtual int getFrameColorspace(const AVFrame* frame) = 0;

    virtual bool isFrameFullRange(const AVFrame* frame) = 0;
};
