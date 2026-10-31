#pragma once

namespace opennow::network
{

enum class WifiBand { Unknown, Ghz2_4, Ghz5 };

constexpr WifiBand WifiBandFromChannel(int channel)
{
    if (channel >= 1 && channel <= 14)
        return WifiBand::Ghz2_4;
    if (channel > 14)
        return WifiBand::Ghz5;
    return WifiBand::Unknown;
}

constexpr bool ShouldWarnForStreaming(WifiBand band)
{
    return band == WifiBand::Ghz2_4;
}

constexpr const char* WifiBandLabel(WifiBand band)
{
    switch (band)
    {
        case WifiBand::Ghz2_4: return "2.4 GHz";
        case WifiBand::Ghz5: return "5 GHz";
        case WifiBand::Unknown: return "Wi-Fi";
    }
    return "Wi-Fi";
}

} // namespace opennow::network
