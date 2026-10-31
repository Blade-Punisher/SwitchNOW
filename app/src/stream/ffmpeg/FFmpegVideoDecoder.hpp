#pragma once
#include "IFFmpegVideoDecoder.hpp"
#include "AVFrameHolder.hpp"
#include <atomic>
#include <array>
#include <utility>

namespace opennow::video { class DecodedFrameGate; }
struct DecodeSubmissionStatus {
    int codec_error=0;
    unsigned corrupt_frames=0;
    bool packet_accepted=false;
};

class FFmpegVideoDecoder : public IFFmpegVideoDecoder {
  public:
    FFmpegVideoDecoder();
    ~FFmpegVideoDecoder();

    int setup(int video_format, int width, int height, int redraw_rate,
              void* context, int dr_flags) override;
    void cleanup() override;
    int submit_decode_unit(uint8_t* indata, int inlen, int64_t pts = AV_NOPTS_VALUE) override;
    void reset_stream();
    void set_frame_gate(opennow::video::DecodedFrameGate* gate) { m_frame_gate=gate; }
    const DecodeSubmissionStatus& last_submission() const { return m_submission; }
    uint64_t transfer_us_total() const { return m_transfer_us_total.load(); }
    uint64_t transfer_us_max() const { return m_transfer_us_max.load(); }
    uint64_t transfer_count() const { return m_transfer_count.load(); }
    uint64_t transfer_storage_reused() const { return m_transfer_storage_reused.load(); }
    uint64_t transfer_storage_fresh() const { return m_transfer_storage_fresh.load(); }
    int capabilities() const override;
    VideoDecodeStats* video_decode_stats() override;
    bool uses_hardware_decoder() const override { return m_uses_hardware_frames; }
    bool uses_hardware_frames() const override {
        return m_uses_hardware_frames && !m_transfer_hardware_frames;
    }
    const std::string& setup_diagnostics() const override { return m_setup_diagnostics; }
    uint64_t corrupt_frames_dropped() const override {
        return m_corrupt_frames_dropped.load(std::memory_order_relaxed);
    }

  private:
    static AVPixelFormat select_hardware_format(AVCodecContext* context,
                                                const AVPixelFormat* formats);
    int fail_setup(const char* stage, int error_code, const std::string& detail = {});
    void append_setup_diagnostic(const std::string& value);
    int decode(char* indata, int inlen, int64_t pts);
    AVFrame* get_frame(bool native_frame);

    AVPacket* m_packet = nullptr;
    AVBufferRef *hw_device_ctx = nullptr;
    const AVCodec* m_decoder = nullptr;
    AVCodecContext* m_decoder_context = nullptr;
    AVFrame *tmp_frame = nullptr;
    AVFrame** m_frames = nullptr;
    int m_frames_size = 0;

    int m_stream_fps = 0;
    int m_frames_in = 0;
    int m_frames_out = 0;
    std::atomic<uint64_t> m_corrupt_frames_dropped {0};
    opennow::video::DecodedFrameGate* m_frame_gate=nullptr;
    DecodeSubmissionStatus m_submission{};
    int m_receive_error=0;
    int m_current_frame = 0, m_next_frame = 0;
    uint32_t m_last_frame = 0;

    VideoDecodeStats m_video_decode_stats_progress = {};
    VideoDecodeStats m_video_decode_stats_cache = {};
    uint64_t timeCount = 0;

    AVFrame* m_frame = nullptr;
    bool m_uses_hardware_frames = false;
    bool m_transfer_hardware_frames = false;
    std::atomic<uint64_t> m_transfer_us_total {0};
    std::atomic<uint64_t> m_transfer_us_max {0};
    std::atomic<uint64_t> m_transfer_count {0};
    std::atomic<uint64_t> m_transfer_storage_reused {0}, m_transfer_storage_fresh {0};
    std::array<std::pair<int,int>,6> m_transfer_layouts {};
    bool m_requires_hardware_frames = false;
    bool m_first_output_format_logged = false;
    std::string m_setup_diagnostics;
};
