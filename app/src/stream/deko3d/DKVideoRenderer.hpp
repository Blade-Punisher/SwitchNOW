#if defined(__SWITCH__) && defined(BOREALIS_USE_DEKO3D)

#pragma once

#include "../IVideoRenderer.hpp"

#include <borealis.hpp>
#include <borealis/platforms/switch/switch_video.hpp>
#include <deko3d.hpp>
#include <nanovg/framework/CShader.h>

#include <array>
#include <optional>
#include <deque>
#include <vector>

class DKVideoRenderer : public IVideoRenderer {
public:
    DKVideoRenderer() = default;
    ~DKVideoRenderer() override;

    void draw(NVGcontext* vg, int width, int height, AVFrame* frame, int imageFormat) override;
    void drawLatest(NVGcontext* vg, int width, int height, AVFrame* frame,
                    int imageFormat, uint64_t generation) override;
    VideoRenderStats* video_render_stats() override;
    int getFrameColorspace(const AVFrame* frame) override;
    bool isFrameFullRange(const AVFrame* frame) override;

private:
    void checkAndInitialize(int width, int height, AVFrame* frame);
    bool updateFrameMapping(AVFrame* frame, uint64_t generation);
    bool updateSoftwareFrame(AVFrame* frame);
    void beginUpdateCommands();
    bool bindDescriptors(const dk::ImageDescriptor& luma,
                         const dk::ImageDescriptor& chroma,
                         int luma_texture_id, int chroma_texture_id);
    bool bindCurrentHardwareTextures();
    void submitUpdateCommands();
    void releaseSoftwareSlots();
    void retainSubmittedFrame(AVFrame* frame);

    bool initialized_ = false;
    int frame_width_ = 0;
    int frame_height_ = 0;
    int screen_width_ = 0;
    int screen_height_ = 0;

    brls::SwitchVideoContext* video_context_ = nullptr;
    dk::Device device_;
    dk::Queue queue_;
    std::optional<CMemPool> code_pool_;
    std::optional<CMemPool> data_pool_;
    dk::UniqueCmdBuf static_cmd_buf_;
    dk::UniqueCmdBuf update_cmd_buf_;
    CMemPool::Handle update_cmd_memory_;
    uint32_t update_cmd_slice_ = 0;
    uint32_t active_update_cmd_slice_ = 0;
    std::array<dk::Fence, 8> update_cmd_fences_ {};
    DkCmdList static_cmd_list_ = 0;
    CShader vertex_shader_;
    CShader fragment_shader_;
    CMemPool::Handle vertex_buffer_;
    CMemPool::Handle transform_buffer_;
    dk::ImageLayout luma_layout_;
    dk::ImageLayout chroma_layout_;
    bool hardware_frames_ = false;

    struct FrameMapping {
        uint32_t handle = 0;
        void* cpu_address = nullptr;
        uint32_t size = 0;
        uint32_t chroma_offset = 0;
        uint64_t last_used_generation = 0;
        dk::UniqueMemBlock memory;
        dk::Image luma;
        dk::Image chroma;
        dk::ImageDescriptor luma_descriptor;
        dk::ImageDescriptor chroma_descriptor;
        int luma_texture_id = -1;
        int chroma_texture_id = -1;
    };

    std::vector<FrameMapping> frame_mappings_;

    struct SoftwareFrameSlot {
        CMemPool::Handle luma_memory;
        CMemPool::Handle chroma_memory;
        CMemPool::Handle luma_upload;
        CMemPool::Handle chroma_upload;
        dk::Image luma;
        dk::Image chroma;
        dk::ImageDescriptor luma_descriptor;
        dk::ImageDescriptor chroma_descriptor;
        int luma_texture_id = -1, chroma_texture_id = -1;
        dk::UniqueCmdBuf draw_commands;
        CMemPool::Handle draw_memory;
        DkCmdList draw_list = 0;
    };

    bool immutable_software_descriptors_ = false;
    std::optional<CMemPool> image_pool_;
    std::optional<CMemPool> upload_pool_;
    std::vector<SoftwareFrameSlot> software_slots_;
    size_t software_slot_cursor_ = 0;
    int current_software_slot_ = -1;
    std::array<dk::Fence, 4> software_slot_fences_ {};
    std::array<bool, 4> software_slot_submitted_ {};
    int current_mapping_ = -1;
    int luma_texture_id_ = -1;
    int chroma_texture_id_ = -1;
    uint64_t rendered_generation_ = 0;
    struct SubmittedFrame {
        AVFrame* frame = nullptr;
        dk::Fence fence {};
        bool fence_submitted = false;
    };

    std::deque<SubmittedFrame> submitted_frames_;
    VideoRenderStats render_stats_ {};
};

#endif
