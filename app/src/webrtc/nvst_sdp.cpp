#include "nvst_sdp.hpp"

#include "../video_quality_policy.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace opennow::webrtc
{
namespace
{

std::string ExtractSdpValue(const std::string& sdp, const std::string& prefix)
{
    size_t start = 0;
    while (start < sdp.size()) {
        size_t end = sdp.find('\n', start);
        const size_t length = end == std::string::npos ? std::string::npos : end - start;
        std::string line = sdp.substr(start, length);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind(prefix, 0) == 0)
            return line.substr(prefix.size());
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return "";
}

} // namespace

std::string BuildNvstSdp(
    const std::string& answer_sdp,
    const StreamSettings& settings,
    const RiInputCapabilities& ri_caps)
{
    constexpr int kOfficialMinimumBitrateKbps = 4000;
    const std::string ice_ufrag = ExtractSdpValue(answer_sdp, "a=ice-ufrag:");
    const std::string ice_pwd = ExtractSdpValue(answer_sdp, "a=ice-pwd:");
    const std::string fingerprint = ExtractSdpValue(answer_sdp, "a=fingerprint:sha-256 ");
    const auto tuning = video::ResolveQualityTuning(settings.image_quality_mode);
    const int max_bitrate = std::max(kOfficialMinimumBitrateKbps, settings.bitrate_kbps);
    const int min_bitrate = std::min(max_bitrate, std::max(
        kOfficialMinimumBitrateKbps,
        max_bitrate * tuning.minimum_bitrate_percent / 100));
    const int initial_bitrate = std::min(max_bitrate, std::max(
        min_bitrate, max_bitrate * tuning.initial_bitrate_percent / 100));

    const std::vector<std::string> lines = {
        "v=0",
        "o=SdpTest test_id_13 14 IN IPv4 127.0.0.1",
        "s=-",
        "t=0 0",
        "a=general.icePassword:" + ice_pwd,
        "a=general.iceUserNameFragment:" + ice_ufrag,
        "a=general.dtlsFingerprint:" + fingerprint,
        "m=video 0 RTP/AVP",
        "a=msid:fbc-video-0",
        "a=vqos.fec.rateDropWindow:10",
        "a=vqos.fec.minRequiredFecPackets:2",
        "a=vqos.drc.minRequiredBitrateCheckEnabled:1",
        "a=vqos.fec.repairMinPercent:" + std::to_string(tuning.fec_repair_min_percent),
        "a=vqos.fec.repairPercent:" + std::to_string(tuning.fec_repair_percent),
        "a=vqos.fec.repairMaxPercent:" + std::to_string(tuning.fec_repair_max_percent),
        "a=vqos.dynamicStreamingMode:0",
        "a=vqos.bllFec.enable:0",
        "a=vqos.drc.enable:0",
        "a=vqos.dfc.enable:0",
        "a=vqos.dfc.adjustResAndFps:0",
        "a=video.dx9EnableNv12:1",
        "a=video.dx9EnableHdr:1",
        "a=vqos.qpg.enable:1",
        "a=vqos.resControl.qp.qpg.featureSetting:7",
        "a=bwe.useOwdCongestionControl:1",
        "a=video.enableRtpNack:1",
        "a=vqos.bw.txRxLag.minFeedbackTxDeltaMs:200",
        "a=vqos.drc.bitrateIirFilterFactor:18",
        "a=video.packetSize:1140",
        "a=packetPacing.minNumPacketsPerGroup:" +
            std::to_string(tuning.pacing_min_packets_per_group),
        "a=vqos.adjustStreamingFpsDuringOutOfFocus:0",
        "a=vqos.resControl.cpmRtc.ignoreOutOfFocusWindowState:1",
        "a=vqos.resControl.perfHistory.rtcIgnoreOutOfFocusWindowState:1",
        "a=vqos.resControl.cpmRtc.featureMask:0",
        "a=vqos.resControl.cpmRtc.enable:0",
        "a=vqos.resControl.cpmRtc.minResolutionPercent:100",
        "a=vqos.resControl.cpmRtc.resolutionChangeHoldonMs:999999",
        "a=packetPacing.numGroups:" + std::to_string(tuning.pacing_groups),
        "a=packetPacing.maxDelayUs:" + std::to_string(tuning.pacing_max_delay_us),
        "a=packetPacing.minNumPacketsFrame:10",
        "a=video.rtpNackQueueLength:1024",
        "a=video.rtpNackQueueMaxPackets:512",
        "a=video.rtpNackMaxPacketCount:25",
        "a=vqos.drc.qpMaxResThresholdAdj:4",
        "a=vqos.grc.qpMaxResThresholdAdj:4",
        "a=vqos.drc.iirFilterFactor:100",
        "a=video.clientViewportWd:" + std::to_string(settings.width),
        "a=video.clientViewportHt:" + std::to_string(settings.height),
        "a=video.maxFPS:" + std::to_string(settings.fps),
        "a=video.initialBitrateKbps:" + std::to_string(initial_bitrate),
        "a=video.initialPeakBitrateKbps:" + std::to_string(max_bitrate),
        "a=vqos.bw.maximumBitrateKbps:" + std::to_string(max_bitrate),
        "a=vqos.bw.minimumBitrateKbps:" + std::to_string(min_bitrate),
        "a=vqos.bw.enableBandwidthEstimation:1",
        "a=vqos.bw.disableBitrateLimit:0",
        "a=video.maxNumReferenceFrames:4",
        "a=video.mapRtpTimestampsToFrames:1",
        "a=video.encoderCscMode:3",
        "a=video.encoderHdrCscMode:4",
        "a=video.dynamicRangeMode:0",
        "a=video.bitDepth:8",
        "a=video.scalingFeature1:0",
        "a=video.prefilterParams.prefilterMode:0",
        "a=video.prefilterParams.prefilterModel:0",
        "a=video.prefilterParams.denoiseLevel:0",
        "a=video.prefilterParams.sharpnessLevel:0",
        "m=audio 0 RTP/AVP",
        "a=msid:audio",
        "m=mic 0 RTP/AVP",
        "a=msid:mic",
        "a=rtpmap:0 PCMU/8000",
        "m=application 0 RTP/AVP",
        "a=msid:input_1",
        "a=ri.partialReliableThresholdMs:" + std::to_string(ri_caps.partial_reliable_threshold_ms),
        "a=ri.hidDeviceMask:" + std::to_string(ri_caps.hid_device_mask),
        "a=ri.enablePartiallyReliableTransferGamepad:" +
            std::to_string(ri_caps.partial_reliable_gamepad_mask),
        "a=ri.enablePartiallyReliableTransferHid:" +
            std::to_string(ri_caps.partial_reliable_hid_mask),
        "",
    };

    std::string result;
    for (const auto& line : lines) {
        result += line;
        result += "\n";
    }
    return result;
}

} // namespace opennow::webrtc
