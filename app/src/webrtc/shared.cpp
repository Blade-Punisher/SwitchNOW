#include "internal.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace opennow::webrtc::internal
{

bool StartsWith(const std::string& value, const char* prefix)
{
    return value.rfind(prefix, 0) == 0;
}

constexpr std::array<uint64_t, 9> kVideoLatencyThresholdsUs = {
    1000, 2000, 4000, 8000, 12000, 16000, 24000, 33000, 50000
};

void RecordLatency(std::array<std::atomic<uint64_t>, 10>& buckets, uint64_t latency_us)
{
    size_t bucket = 0;
    while (bucket < kVideoLatencyThresholdsUs.size() &&
           latency_us > kVideoLatencyThresholdsUs[bucket]) {
        ++bucket;
    }
    buckets[bucket].fetch_add(1, std::memory_order_relaxed);
}

static uint64_t LatencyPercentile(
    const std::array<std::atomic<uint64_t>, 10>& buckets, uint64_t percent)
{
    uint64_t total = 0;
    for (const auto& bucket : buckets)
        total += bucket.load(std::memory_order_relaxed);
    if (total == 0)
        return 0;

    const uint64_t target = (total * percent + 99) / 100;
    uint64_t accumulated = 0;
    for (size_t i = 0; i < buckets.size(); ++i) {
        accumulated += buckets[i].load(std::memory_order_relaxed);
        if (accumulated >= target)
            return i < kVideoLatencyThresholdsUs.size() ? kVideoLatencyThresholdsUs[i] : 50001;
    }
    return 50001;
}

uint64_t LatencyPercentile95(const std::array<std::atomic<uint64_t>, 10>& buckets)
{
    return LatencyPercentile(buckets, 95);
}

uint64_t LatencyPercentile99(const std::array<std::atomic<uint64_t>, 10>& buckets)
{
    return LatencyPercentile(buckets, 99);
}


std::string HexPreview(const uint8_t* data, size_t size, size_t max_bytes)
{
    if (!data || size == 0)
        return "";

    std::ostringstream out;
    const size_t count = std::min(size, max_bytes);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0)
            out << ' ';
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    if (size > count)
        out << " ...";
    return out.str();
}

bool ContainsH264Idr(const uint8_t* data, size_t size)
{
    if (!data || size < 5)
        return false;

    for (size_t i = 0; i + 4 < size; ++i) {
        size_t header = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            header = i + 3;
        else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 &&
                 data[i + 2] == 0 && data[i + 3] == 1)
            header = i + 4;

        if (header > 0 && header < size && (data[header] & 0x1f) == 5)
            return true;
    }
    return false;
}

uint64_t NowUs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace opennow::webrtc::internal
