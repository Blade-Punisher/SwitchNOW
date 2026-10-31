#pragma once
#include <cstdint>

namespace opennow::status {
enum class Stage { Signaling, Transport, Input, Video, Decode, Ready };
inline Stage ConnectionStage(bool signaling, bool transport, bool input,
    bool packets, std::uint64_t units, std::uint64_t decoded) {
    if (decoded) return Stage::Ready;
    if (units) return Stage::Decode;
    if (packets || input) return Stage::Video;
    if (transport) return Stage::Input;
    return signaling ? Stage::Transport : Stage::Signaling;
}
inline const char* StageTitle(Stage stage) {
    switch (stage) {
        case Stage::Signaling: return "Connecting to your cloud rig...";
        case Stage::Transport: return "Negotiating the stream...";
        case Stage::Input: return "Opening the control channel...";
        case Stage::Video: return "Waiting for the first video packets";
        case Stage::Decode: return "Decoding the video stream...";
        case Stage::Ready: return "Preparing the first clean frame...";
    }
    return "Connecting to your cloud rig...";
}
inline const char* StageDetail(Stage stage) {
    switch (stage) {
        case Stage::Signaling: return "Opening the secure signaling channel";
        case Stage::Transport: return "Establishing the secure network path";
        case Stage::Input: return "Secure connection established; waiting for game controls";
        case Stage::Video: return "Waiting for video from the cloud computer";
        case Stage::Decode: return "Video is arriving and the decoder is synchronizing";
        case Stage::Ready: return "The game image will appear automatically";
    }
    return "";
}

enum class Bottleneck { Measuring, Stable, Loss, Incoming, Decode, Presentation, Delivery };
struct Sample {
    float incoming = 0, decoded = 0, displayed = 0;
    int target = 60;
    std::uint32_t gaps = 0, late = 0, damaged = 0;
    std::uint64_t decode_us = 0;
    unsigned queued = 0;
};
inline Bottleneck Evaluate(const Sample& sample) {
    if (sample.target <= 0) return Bottleneck::Measuring;
    if (sample.damaged || (sample.gaps && sample.displayed < sample.target * .90f))
        return Bottleneck::Loss;
    if (sample.incoming < sample.target * .80f) return Bottleneck::Incoming;
    if (sample.decoded + 5 < sample.incoming &&
        (sample.queued > 1 || sample.decode_us > 1000000u / sample.target))
        return Bottleneck::Decode;
    if (sample.displayed + 5 < sample.decoded) return Bottleneck::Presentation;
    if (sample.late && sample.displayed < sample.target * .90f) return Bottleneck::Delivery;
    return Bottleneck::Stable;
}
inline const char* Label(Bottleneck reason) {
    switch (reason) {
        case Bottleneck::Measuring: return "Collecting performance samples";
        case Bottleneck::Stable: return "No sustained bottleneck detected";
        case Bottleneck::Loss: return "Damaged video or packet gaps";
        case Bottleneck::Delivery: return "Uneven stream delivery";
        case Bottleneck::Incoming: return "Low incoming FPS: server or network";
        case Bottleneck::Decode: return "Decoder is falling behind";
        case Bottleneck::Presentation: return "Display is falling behind";
    }
    return "";
}
// Two consecutive windows avoid blaming a single irregular sample.
struct Assessment {
    Bottleneck current = Bottleneck::Measuring, pending = Bottleneck::Measuring;
    unsigned count = 0;
    bool Observe(Bottleneck next) {
        if (next != pending) { pending = next; count = 1; }
        else if (count < 2) ++count;
        if (count < 2 || current == next) return false;
        current = next;
        return true;
    }
};
} // namespace opennow::status
