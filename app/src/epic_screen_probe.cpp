#include "epic_screen_probe.hpp"
#ifdef __SWITCH__
#include <borealis.hpp>
#include <borealis/platforms/switch/switch_video.hpp>
#endif
namespace opennow::autologin::epic {
void RequestProbe(const std::shared_ptr<Probe>& job,Flow::Clock::time_point now) {
    if(!job||!job->active||job->pending)return;
    if(job->requested_at.time_since_epoch().count()&&now-job->requested_at<std::chrono::milliseconds(500))return;
    job->pending=true;job->requested_at=now;
#ifdef __SWITCH__
    auto* video=static_cast<brls::SwitchVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    if(!video){job->screen=Screen::Unknown;++job->serial;job->pending=false;return;}
    video->captureNextFrame([job]{
        if(!job->active){job->pending=false;return;}
        Detection result;
        try {
            auto* context=static_cast<brls::SwitchVideoContext*>(brls::Application::getPlatform()->getVideoContext());
            const auto width=brls::Application::windowWidth,height=brls::Application::windowHeight;
            if(context&&width>=640&&width<=1920&&height>=360&&height<=1080) {
                auto device=context->getDeko3dDevice();auto queue=context->getQueue();
                const std::uint32_t row=(width*4+255)&~255u;
                const std::uint32_t bytes=(row*kRowY.size()+DK_MEMBLOCK_ALIGNMENT-1)&~(DK_MEMBLOCK_ALIGNMENT-1);
                auto pixels=dk::MemBlockMaker{device,bytes}.setFlags(DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached).create();
                auto commands=dk::MemBlockMaker{device,DK_MEMBLOCK_ALIGNMENT}.setFlags(DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached).create();
                auto copy=dk::CmdBufMaker{device}.create();
                if(pixels&&commands&&copy) {
                    // Only six RGBA scanlines; no PNG, disk I/O, OCR or normal-play readback.
                    queue.waitIdle();copy.addMemory(commands,0,DK_MEMBLOCK_ALIGNMENT);copy.barrier(DkBarrier_Full,0);
                    for(unsigned i=0;i<kRowY.size();++i)copy.copyImageToBuffer(dk::ImageView{*context->getFramebuffer()},
                        DkImageRect{0,static_cast<std::uint32_t>(kRowY[i])*height/720,0,width,1,1},DkCopyBuf{pixels.getGpuAddr()+row*i,row,0});
                    queue.submitCommands(copy.finishList());queue.waitIdle();
                    Samples samples{};const auto* rgba=static_cast<const unsigned char*>(pixels.getCpuAddr());
                    for(unsigned i=0;i<samples.size();++i){const unsigned line=i<2?0:i<6?1:i<10?2:3+(i-10)/4;const auto x=static_cast<unsigned>(kSampleX[i])*width/1280;
                        const auto* p=rgba+row*line+x*4;samples[i]={p[0],p[1],p[2]};}
                    result=Detect(samples);
                }
            }
        } catch(...) {}
        job->screen=result.screen;job->continue_y=result.continue_y;++job->serial;job->pending=false;
    });
#else
    job->screen=Screen::Unknown;++job->serial;job->pending=false;
#endif
}
} // namespace opennow::autologin::epic
