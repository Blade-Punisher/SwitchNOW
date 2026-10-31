#include "localization.hpp"
#include "ad_playback.hpp"

#include "app_paths.hpp"
#include "advertisement_source.hpp"
#include "media_input_policy.hpp"
#include "runtime_journal.hpp"
#include "stream/deko3d/DKVideoRenderer.hpp"

#include <switch.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace opennow
{
namespace
{

constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioBufferCount = 16;
constexpr size_t kAudioBufferBytes = 0x4000;
constexpr size_t kAudioQueueLimit = 8;

std::string FfmpegError(int code)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text {};
    av_strerror(code, text.data(), text.size());
    return text.data();
}

int64_t FrameTimeUs(const AVFrame* frame, const AVStream* stream)
{
    if (!frame || !stream)
        return AV_NOPTS_VALUE;
    const int64_t timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
        ? frame->best_effort_timestamp : frame->pts;
    return timestamp == AV_NOPTS_VALUE
        ? AV_NOPTS_VALUE
        : av_rescale_q(timestamp, stream->time_base, AV_TIME_BASE_Q);
}

class AudioOutput
{
  public:
    AudioOutput() = default;
    ~AudioOutput() { Shutdown(); }

    bool Initialize()
    {
        const Result rc = audoutInitialize();
        if (R_FAILED(rc))
            return false;
        initialized_ = true;
        if (audoutGetSampleRate() != kAudioSampleRate ||
            audoutGetChannelCount() != kAudioChannels ||
            audoutGetPcmFormat() != PcmFormat_Int16)
        {
            Shutdown();
            return false;
        }
        for (auto& slot : slots_)
        {
            slot.storage = static_cast<uint8_t*>(
                memalign(0x1000, kAudioBufferBytes));
            if (!slot.storage)
            {
                Shutdown();
                return false;
            }
            std::memset(slot.storage, 0, kAudioBufferBytes);
            slot.buffer.buffer = slot.storage;
            slot.buffer.buffer_size = kAudioBufferBytes;
            slot.buffer.data_offset = 0;
            slot.buffer.data_size = 0;
            slot.buffer.next = nullptr;
            slot.free = true;
        }
        return true;
    }

    bool Submit(const int16_t* samples, size_t sample_count,
                const std::function<bool()>& is_cancelled)
    {
        if (!initialized_ || !samples || sample_count == 0)
            return true;
        const uint8_t* source = reinterpret_cast<const uint8_t*>(samples);
        size_t remaining = sample_count * sizeof(int16_t);
        while (remaining > 0)
        {
            Slot* slot = nullptr;
            for (int attempts = 0; attempts < 500 && !slot; ++attempts)
            {
                Reclaim();
                if (queued_ < kAudioQueueLimit)
                    slot = Acquire();
                if (!slot)
                {
                    if (is_cancelled && is_cancelled())
                        return false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            if (!slot)
                return false;

            const size_t bytes = std::min(remaining, kAudioBufferBytes);
            std::memcpy(slot->storage, source, bytes);
            armDCacheFlush(slot->storage, bytes);
            slot->buffer.next = nullptr;
            slot->buffer.data_offset = 0;
            slot->buffer.data_size = bytes;
            const Result rc = audoutAppendAudioOutBuffer(&slot->buffer);
            if (R_FAILED(rc))
            {
                slot->free = true;
                return false;
            }
            slot->free = false;
            ++queued_;
            if (!started_ && queued_ >= 2)
            {
                if (R_FAILED(audoutStartAudioOut()))
                    return false;
                started_ = true;
            }
            source += bytes;
            remaining -= bytes;
        }
        return true;
    }

    void Drain(const std::function<bool()>& is_cancelled)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (queued_ > 0 && std::chrono::steady_clock::now() < deadline)
        {
            Reclaim();
            if (is_cancelled && is_cancelled())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

  private:
    struct Slot
    {
        AudioOutBuffer buffer {};
        uint8_t* storage = nullptr;
        bool free = true;
    };

    Slot* Acquire()
    {
        for (auto& slot : slots_)
            if (slot.free)
                return &slot;
        return nullptr;
    }

    void Reclaim()
    {
        AudioOutBuffer* released = nullptr;
        u32 count = 0;
        while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&released, &count)) &&
               released && count > 0)
        {
            for (auto& slot : slots_)
            {
                if (&slot.buffer == released)
                {
                    if (!slot.free && queued_ > 0)
                        --queued_;
                    slot.free = true;
                    break;
                }
            }
            released = nullptr;
            count = 0;
        }
    }

    void Shutdown()
    {
        if (!initialized_)
            return;
        if (started_)
        {
            bool flushed = false;
            (void)audoutFlushAudioOutBuffers(&flushed);
            (void)audoutStopAudioOut();
        }
        for (auto& slot : slots_)
        {
            free(slot.storage);
            slot.storage = nullptr;
            slot.free = true;
        }
        queued_ = 0;
        started_ = false;
        initialized_ = false;
        audoutExit();
    }

    std::array<Slot, kAudioBufferCount> slots_ {};
    size_t queued_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

struct FormatDeleter
{
    void operator()(AVFormatContext* value) const
    {
        if (value)
            avformat_close_input(&value);
    }
};

struct CodecDeleter
{
    void operator()(AVCodecContext* value) const
    {
        if (value)
            avcodec_free_context(&value);
    }
};

struct PacketDeleter
{
    void operator()(AVPacket* value) const { av_packet_free(&value); }
};

struct FrameDeleter
{
    void operator()(AVFrame* value) const { av_frame_free(&value); }
};

struct SwrDeleter
{
    void operator()(SwrContext* value) const
    {
        if (value)
            swr_free(&value);
    }
};

struct MediaInterruptContext
{
    const std::function<bool()>* is_cancelled = nullptr;
};

int InterruptMediaRead(void* opaque)
{
    const auto* context = static_cast<const MediaInterruptContext*>(opaque);
    return context && context->is_cancelled && *context->is_cancelled &&
           (*context->is_cancelled)()
        ? 1
        : 0;
}

} // namespace

AdvertisementPlaybackState::AdvertisementPlaybackState() = default;

AdvertisementPlaybackState::~AdvertisementPlaybackState()
{
    std::lock_guard<std::mutex> lock(mutex_);
    av_frame_free(&frame_);
}

void AdvertisementPlaybackState::Begin(std::string title, int duration_seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    title_ = std::move(title);
    status_ = "Loading advertisement...";
    duration_ms_ = std::max(0, duration_seconds) * 1000;
    watched_ms_ = 0;
    finished_ = false;
    av_frame_free(&frame_);
    ++generation_;
}

void AdvertisementPlaybackState::PresentFrame(
    const AVFrame* frame, int watched_time_ms)
{
    AVFrame* copy = frame ? av_frame_clone(frame) : nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    if (copy)
    {
        av_frame_free(&frame_);
        frame_ = copy;
        ++generation_;
    }
    watched_ms_ = std::max(watched_ms_, watched_time_ms);
    status_ = "Advertisement playing";
}

void AdvertisementPlaybackState::SetProgress(
    int watched_time_ms, std::string status)
{
    std::lock_guard<std::mutex> lock(mutex_);
    watched_ms_ = std::max(watched_ms_, watched_time_ms);
    status_ = std::move(status);
}

void AdvertisementPlaybackState::Finish(std::string status)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(status);
    finished_ = true;
}

struct AdvertisementPlaybackView::Impl
{
    explicit Impl(std::shared_ptr<AdvertisementPlaybackState> source)
        : state(std::move(source)) {}
    std::shared_ptr<AdvertisementPlaybackState> state;
    DKVideoRenderer renderer;
};

AdvertisementPlaybackView::AdvertisementPlaybackView(
    std::shared_ptr<AdvertisementPlaybackState> state)
    : impl_(std::make_unique<Impl>(std::move(state)))
{
    setWidth(620);
    setHeight(430);
}

AdvertisementPlaybackView::~AdvertisementPlaybackView() = default;

void AdvertisementPlaybackView::draw(
    NVGcontext* vg, float x, float y, float width, float height,
    brls::Style style, brls::FrameContext* ctx)
{
    (void)style;
    (void)ctx;
    AVFrame* frame = nullptr;
    uint64_t generation = 0;
    int duration_ms = 0;
    int watched_ms = 0;
    bool finished = false;
    std::string title;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(impl_->state->mutex_);
        frame = impl_->state->frame_
            ? av_frame_clone(impl_->state->frame_) : nullptr;
        generation = impl_->state->generation_;
        duration_ms = impl_->state->duration_ms_;
        watched_ms = impl_->state->watched_ms_;
        finished = impl_->state->finished_;
        title = impl_->state->title_;
        status = impl_->state->status_;
    }

    if (frame)
    {
        impl_->renderer.drawLatest(
            vg, brls::Application::ORIGINAL_WINDOW_WIDTH,
            brls::Application::ORIGINAL_WINDOW_HEIGHT, frame, 0, generation);
        av_frame_free(&frame);
    }
    else
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 12.0f);
        nvgFillColor(vg, nvgRGB(9, 11, 14));
        nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, 72.0f);
    nvgFillColor(vg, nvgRGBA(5, 7, 10, 218));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, y + height - 92.0f, width, 92.0f);
    nvgFillColor(vg, nvgRGBA(5, 7, 10, 225));
    nvgFill(vg);

    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, nvgRGB(77, 218, 130));
    nvgText(vg, x + 22.0f, y + 21.0f, opennow::Tr("ADVERTISEMENT").c_str(), nullptr);
    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, nvgRGB(245, 247, 249));
    nvgText(vg, x + 22.0f, y + 49.0f, title.c_str(), nullptr);

    const float progress = duration_ms > 0
        ? std::clamp(static_cast<float>(watched_ms) / duration_ms, 0.0f, 1.0f)
        : (finished ? 1.0f : 0.0f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 22.0f, y + height - 31.0f,
                   width - 44.0f, 7.0f, 3.5f);
    nvgFillColor(vg, nvgRGB(42, 47, 54));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 22.0f, y + height - 31.0f,
                   (width - 44.0f) * progress, 7.0f, 3.5f);
    nvgFillColor(vg, nvgRGB(77, 218, 130));
    nvgFill(vg);
    nvgFontSize(vg, 17.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGB(226, 231, 235));
    nvgText(vg, x + 22.0f, y + height - 62.0f, opennow::TrMessage(status).c_str(), nullptr);
    if (duration_ms > 0)
    {
        const int remaining = std::max(0, duration_ms - watched_ms + 999) / 1000;
        const std::string timer = std::to_string(remaining) + " s";
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + width - 22.0f, y + height - 62.0f,
                timer.c_str(), nullptr);
    }
    invalidate();
}

AdvertisementPlaybackResult PlayAdvertisementMedia(
    const SessionAdInfo& ad,
    const AdMediaFileInfo& media,
    const HttpClient& http_client,
    const std::shared_ptr<AdvertisementPlaybackState>& state,
    const std::function<bool()>& is_cancelled,
    const std::function<void()>& on_started)
{
    AdvertisementPlaybackResult result;
    std::unique_ptr<AdvertisementSource> prepared;
    const auto cancelled = [&]() {
        return is_cancelled && is_cancelled();
    };
    state->Begin("Free-tier message", ad.duration_seconds);
    const auto playback_started = std::chrono::steady_clock::now();

    try
    {
        if (media.url.empty())
            throw std::runtime_error("NVIDIA did not provide advertisement media");

        std::string input = media.url;
        if (media.url.rfind("https://", 0) == 0 || media.url.rfind("http://", 0) == 0)
        {
            state->SetProgress(0, "Downloading advertisement...");
            prepared = std::make_unique<AdvertisementSource>(
                AppHomePath() + "/queue_ad_media",
                [&](const std::string& url, const std::string& path) {
                    std::string effective_url;
                    http_client.DownloadToFile(url, path,
                        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36", {},
                        [state](long long received, long long total) {
                            if (total > 0) state->SetProgress(0,
                                "Downloading advertisement... " + std::to_string(
                                    std::clamp(received * 100 / total, 0LL, 100LL)) + "%");
                        }, cancelled, &effective_url);
                    return effective_url;
                }, cancelled, [](const std::string& detail) {
                    LogRuntimeEvent("session", "ad_source_inspected", detail);
                });
            input = media_input::FfmpegLocalFileUrl(prepared->Prepare(media.url));
            LogRuntimeEvent("session", "ad_media_download_completed", "input=file source=resolved");
        }
        if (cancelled())
        {
            result.cancelled = true;
            return result;
        }

        MediaInterruptContext interrupt_context {&is_cancelled};
        AVFormatContext* raw_format = avformat_alloc_context();
        if (!raw_format)
            throw std::runtime_error("Could not allocate advertisement reader");
        raw_format->interrupt_callback.callback = InterruptMediaRead;
        raw_format->interrupt_callback.opaque = &interrupt_context;
        LogRuntimeEvent(
            "session", "ad_media_open_started",
            "profile=" + media.encoding_profile +
            " input=" + (input.rfind("file:", 0) == 0 ? "file" : "network"));
        AVDictionary* format_options = nullptr;
        // Prepared HLS resources use extensionless local cache paths. Their
        // origins were restricted to HTTP(S) by AdvertisementSource.
        if (prepared) av_dict_set(&format_options, "allowed_extensions", "ALL", 0);
        int error = avformat_open_input(&raw_format, input.c_str(), nullptr, &format_options);
        av_dict_free(&format_options);
        if (error < 0)
        {
            if (raw_format)
                avformat_free_context(raw_format);
            throw std::runtime_error("Could not open advertisement: " + FfmpegError(error));
        }
        std::unique_ptr<AVFormatContext, FormatDeleter> format(raw_format);
        LogRuntimeEvent(
            "session", "ad_media_open_completed",
            "profile=" + media.encoding_profile);
        if ((error = avformat_find_stream_info(format.get(), nullptr)) < 0)
            throw std::runtime_error("Could not inspect advertisement: " + FfmpegError(error));

        const int video_index = av_find_best_stream(
            format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int audio_index = av_find_best_stream(
            format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (video_index < 0)
            throw std::runtime_error("Advertisement has no supported video stream");

        {
            const AVCodecParameters* video =
                format->streams[video_index]->codecpar;
            std::ostringstream details;
            details << "video_codec=" << avcodec_get_name(video->codec_id)
                    << " width=" << video->width
                    << " height=" << video->height
                    << " pixel_format=" << video->format;
            if (audio_index >= 0)
            {
                const AVCodecParameters* audio =
                    format->streams[audio_index]->codecpar;
                details << " audio_codec=" << avcodec_get_name(audio->codec_id)
                        << " sample_rate=" << audio->sample_rate
                        << " channels=" << audio->ch_layout.nb_channels;
            }
            else
            {
                details << " audio_codec=none";
            }
            LogRuntimeEvent("session", "ad_media_streams", details.str());
        }

        auto open_decoder = [&](int index) {
            AVStream* stream = format->streams[index];
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec)
                throw std::runtime_error("Advertisement codec is not available");
            AVCodecContext* context = avcodec_alloc_context3(codec);
            if (!context)
                throw std::runtime_error("Could not allocate advertisement decoder");
            if (avcodec_parameters_to_context(context, stream->codecpar) < 0 ||
                avcodec_open2(context, codec, nullptr) < 0)
            {
                avcodec_free_context(&context);
                throw std::runtime_error("Could not initialize advertisement decoder");
            }
            return std::unique_ptr<AVCodecContext, CodecDeleter>(context);
        };

        auto video_decoder = open_decoder(video_index);
        std::unique_ptr<AVCodecContext, CodecDeleter> audio_decoder;
        SwrContext* resampler = nullptr;
        AudioOutput audio_output;
        bool audio_ready = false;
        if (audio_index >= 0)
        {
            audio_decoder = open_decoder(audio_index);
            AVChannelLayout output_layout;
            av_channel_layout_default(&output_layout, kAudioChannels);
            if (swr_alloc_set_opts2(
                    &resampler, &output_layout, AV_SAMPLE_FMT_S16,
                    kAudioSampleRate, &audio_decoder->ch_layout,
                    audio_decoder->sample_fmt, audio_decoder->sample_rate,
                    0, nullptr) >= 0 && swr_init(resampler) >= 0)
                audio_ready = audio_output.Initialize();
            av_channel_layout_uninit(&output_layout);
        }
        std::unique_ptr<SwrContext, SwrDeleter> swr(resampler);

        std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
        std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
        if (!packet || !frame)
            throw std::runtime_error("Could not allocate advertisement frames");

        int64_t timeline_origin_us = AV_NOPTS_VALUE;
        auto wall_origin = std::chrono::steady_clock::now();
        bool playback_clock_started = false;
        int64_t video_fallback_us = 0;
        int64_t audio_fallback_us = 0;
        size_t video_frames = 0;
        size_t audio_frames = 0;
        size_t video_decode_errors = 0;
        size_t audio_decode_errors = 0;
        size_t leading_audio_frames_dropped = 0;
        AVRational video_rate = av_guess_frame_rate(
            format.get(), format->streams[video_index], nullptr);
        int64_t video_interval_us = 33333;
        if (video_rate.num > 0 && video_rate.den > 0)
        {
            video_interval_us = av_rescale_q(
                1, av_inv_q(video_rate), AV_TIME_BASE_Q);
            video_interval_us = std::clamp<int64_t>(
                video_interval_us, 10000, 100000);
        }

        const auto wait_for_time = [&](int64_t frame_time_us) {
            if (frame_time_us == AV_NOPTS_VALUE)
                return !cancelled();
            if (timeline_origin_us == AV_NOPTS_VALUE)
            {
                timeline_origin_us = frame_time_us;
                wall_origin = std::chrono::steady_clock::now();
            }
            const int64_t relative_us = std::max<int64_t>(
                0, frame_time_us - timeline_origin_us);
            const auto deadline = wall_origin + std::chrono::microseconds(relative_us);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (cancelled())
                    return false;
                const auto remaining = deadline - std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::min(
                    std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                    std::chrono::milliseconds(4)));
            }
            return true;
        };

        const auto mark_started = [&]() {
            if (result.started)
                return;
            if (on_started)
                on_started();
            result.started = true;
            wall_origin = std::chrono::steady_clock::now();
            playback_clock_started = true;
            LogRuntimeEvent("session", "ad_playback_started");
        };

        const auto process_decoded_frame = [&](bool is_video, AVStream* stream) {
            int64_t frame_time_us = FrameTimeUs(frame.get(), stream);
            if (frame_time_us == AV_NOPTS_VALUE)
            {
                if (is_video)
                {
                    frame_time_us = video_fallback_us;
                    video_fallback_us += video_interval_us;
                }
                else
                {
                    frame_time_us = audio_fallback_us;
                    const int sample_rate = std::max(1, audio_decoder->sample_rate);
                    audio_fallback_us += av_rescale(
                        frame->nb_samples, AV_TIME_BASE, sample_rate);
                }
            }
            else if (is_video)
            {
                video_fallback_us = frame_time_us + video_interval_us;
            }
            else
            {
                const int sample_rate = std::max(1, audio_decoder->sample_rate);
                audio_fallback_us = frame_time_us + av_rescale(
                    frame->nb_samples, AV_TIME_BASE, sample_rate);
            }

            // Do not let an audio-only decode path report a viewed ad. A real
            // video frame must be available before Start is sent to NVIDIA.
            if (!is_video && !result.started)
            {
                ++leading_audio_frames_dropped;
                av_frame_unref(frame.get());
                return true;
            }

            if (!wait_for_time(frame_time_us))
                return false;

            if (is_video)
            {
                if (frame->format != AV_PIX_FMT_YUV420P &&
                    frame->format != AV_PIX_FMT_YUVJ420P &&
                    frame->format != AV_PIX_FMT_NV12)
                    throw std::runtime_error("Advertisement uses an unsupported pixel format");
                ++video_frames;
                if (video_frames == 1)
                {
                    std::ostringstream details;
                    details << "width=" << frame->width
                            << " height=" << frame->height
                            << " pixel_format=" << frame->format;
                    LogRuntimeEvent(
                        "session", "ad_video_first_frame", details.str());
                    // Publish the first frame before the synchronous Start
                    // report so the user never waits on a black placeholder.
                    state->PresentFrame(frame.get(), 0);
                    mark_started();
                }
            }

            const int watched_ms = playback_clock_started
                ? static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - wall_origin).count())
                : 0;
            result.watched_time_ms = std::max(result.watched_time_ms, watched_ms);

            if (is_video)
            {
                if (video_frames > 1)
                    state->PresentFrame(frame.get(), result.watched_time_ms);
                else
                    state->SetProgress(
                        result.watched_time_ms, "Advertisement playing");
            }
            else if (audio_ready && swr)
            {
                ++audio_frames;
                const int output_samples = swr_get_out_samples(
                    swr.get(), frame->nb_samples);
                std::vector<int16_t> pcm(
                    static_cast<size_t>(output_samples) * kAudioChannels);
                uint8_t* output[] = {
                    reinterpret_cast<uint8_t*>(pcm.data())};
                const int converted = swr_convert(
                    swr.get(), output, output_samples,
                    const_cast<const uint8_t**>(frame->extended_data),
                    frame->nb_samples);
                if (converted > 0 && !audio_output.Submit(
                        pcm.data(), static_cast<size_t>(converted) *
                        kAudioChannels, cancelled))
                    throw std::runtime_error("Advertisement audio output stopped");
                state->SetProgress(result.watched_time_ms, "Advertisement playing");
            }
            av_frame_unref(frame.get());
            return true;
        };

        const auto drain_decoder = [&](AVCodecContext* decoder,
                                       AVStream* stream,
                                       bool is_video) {
            if (!decoder || cancelled())
                return;
            const int send_result = avcodec_send_packet(decoder, nullptr);
            if (send_result < 0 && send_result != AVERROR_EOF)
            {
                (is_video ? video_decode_errors : audio_decode_errors)++;
                return;
            }
            while (!cancelled())
            {
                const int receive_result = avcodec_receive_frame(decoder, frame.get());
                if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF)
                    break;
                if (receive_result < 0)
                {
                    (is_video ? video_decode_errors : audio_decode_errors)++;
                    break;
                }
                if (!process_decoded_frame(is_video, stream))
                    break;
            }
        };

        while (!cancelled() && av_read_frame(format.get(), packet.get()) >= 0)
        {
            AVCodecContext* decoder = nullptr;
            AVStream* stream = nullptr;
            const bool is_video = packet->stream_index == video_index;
            const bool is_audio = packet->stream_index == audio_index;
            if (is_video)
            {
                decoder = video_decoder.get();
                stream = format->streams[video_index];
            }
            else if (is_audio && audio_decoder)
            {
                decoder = audio_decoder.get();
                stream = format->streams[audio_index];
            }
            if (!decoder)
            {
                av_packet_unref(packet.get());
                continue;
            }
            error = avcodec_send_packet(decoder, packet.get());
            av_packet_unref(packet.get());
            if (error < 0 && error != AVERROR(EAGAIN))
            {
                (is_video ? video_decode_errors : audio_decode_errors)++;
                continue;
            }

            while (!cancelled())
            {
                const int receive_result =
                    avcodec_receive_frame(decoder, frame.get());
                if (receive_result == AVERROR(EAGAIN) ||
                    receive_result == AVERROR_EOF)
                    break;
                if (receive_result < 0)
                {
                    (is_video ? video_decode_errors : audio_decode_errors)++;
                    break;
                }
                if (!process_decoded_frame(is_video, stream))
                    break;
            }
        }

        drain_decoder(
            video_decoder.get(), format->streams[video_index], true);
        if (audio_decoder)
            drain_decoder(
                audio_decoder.get(), format->streams[audio_index], false);

        {
            std::ostringstream details;
            details << "video_frames=" << video_frames
                    << " audio_frames=" << audio_frames
                    << " video_errors=" << video_decode_errors
                    << " audio_errors=" << audio_decode_errors
                    << " leading_audio_dropped="
                    << leading_audio_frames_dropped
                    << " watched_ms=" << result.watched_time_ms;
            LogRuntimeEvent("session", "ad_playback_summary", details.str());
        }

        if (cancelled())
        {
            result.cancelled = true;
            state->Finish("Advertisement cancelled");
        }
        else
        {
            audio_output.Drain(cancelled);
            result.completed = result.started && video_frames > 0;
            if (video_frames == 0)
                result.error = "Advertisement produced no decodable video frames";
            state->Finish(result.completed
                ? "Advertisement complete" : "Advertisement did not start");
        }
    }
    catch (const std::exception& error)
    {
        result.error = error.what();
        result.cancelled = cancelled();
        state->Finish(result.cancelled
            ? "Advertisement cancelled" : "Advertisement playback failed");
        LogRuntimeEvent("session", "ad_playback_failed", result.error);
    }

    prepared.reset();

    if (result.started && result.watched_time_ms <= 0)
        result.watched_time_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - playback_started).count());
    return result;
}

} // namespace opennow
