#include "media_input_policy.hpp"

#include <cassert>

int main()
{
    using namespace opennow::media_input;

    assert(HasScheme("https://example.test/ad.mp4"));
    assert(HasScheme("sdmc:/switch/SwitchNOW/queue_ad_media"));
    assert(!HasScheme("queue_ad_media"));

    assert(FfmpegLocalFileUrl("sdmc:/switch/SwitchNOW/queue_ad_media") ==
           "file:sdmc:/switch/SwitchNOW/queue_ad_media");
    assert(FfmpegLocalFileUrl("file:sdmc:/switch/SwitchNOW/ad.mp4") ==
           "file:sdmc:/switch/SwitchNOW/ad.mp4");
    assert(DownloadAdvertisementFile("https://cdn.test/opaque/signed?token=x", "default"));
    assert(DownloadAdvertisementFile("HTTPS://cdn.test/video", "DEFAULT"));
    assert(DownloadAdvertisementFile("http://cdn.test/video", "mp4"));
    assert(DownloadAdvertisementFile("https://cdn.test/video?format=mp4", "default"));
    assert(!DownloadAdvertisementFile("https://cdn.test/video.m3u8?token=x", "default"));
    assert(!DownloadAdvertisementFile("https://cdn.test/opaque", "HLS"));
    assert(!DownloadAdvertisementFile("https://cdn.test/video.mpd", "default"));
    assert(!DownloadAdvertisementFile("file:sdmc:/ad.mp4", "mp4"));
    assert(!DownloadAdvertisementFile("", "default"));
    return 0;
}
