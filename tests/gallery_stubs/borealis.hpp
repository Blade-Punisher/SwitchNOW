#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>
struct NVGcontext {};
int nvgCreateImageMem(NVGcontext*, int, unsigned char*, int);
int nvgCreateImageRGBA(NVGcontext*, int, int, int, const unsigned char*);
void nvgDeleteImage(NVGcontext*, int);
namespace brls {
inline std::mutex ui_mutex;
inline std::vector<std::function<void()>> ui_jobs;
inline void sync(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(ui_mutex); ui_jobs.push_back(std::move(job));
}
inline size_t Pump() {
    std::vector<std::function<void()>> jobs;
    { std::lock_guard<std::mutex> lock(ui_mutex); jobs.swap(ui_jobs); }
    for (auto& job : jobs) job();
    return jobs.size();
}
class Application {
public: static NVGcontext* getNVGContext() { static NVGcontext context; return &context; }
};
class Image {
    int texture_ = 0;
    bool owns_ = true;
public:
    virtual ~Image() { clear(); }
    void clear() { if(texture_ && owns_) nvgDeleteImage(Application::getNVGContext(),texture_); texture_ = 0; }
    void invalidate() {}
    void setFreeTexture(bool value) { owns_ = value; }
    void innerSetImage(int value) { texture_ = value; }
    int getTexture() const { return texture_; }
    void setImageFromRes(const std::string&) {}
    void setImageFromMem(const unsigned char* data,int size) {
        clear();texture_=nvgCreateImageMem(Application::getNVGContext(),0,const_cast<unsigned char*>(data),size);
    }
    void setImageAsync(std::function<void(std::function<void(const std::string&, size_t)>)> cb) {
        cb([](const std::string&, size_t) {});
    }
};
}
