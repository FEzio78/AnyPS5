#pragma once

#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"

namespace AgcDriver::Graphics {

class GpuWorkTiming {
public:
    GpuWorkTiming(VkCommandBuffer commands, Recorder::CommandClass category, const char* name) {
        if (!GpuDescriptorProfile::Enabled()) return;
        batch = CommandBatch::Find(commands);
        if (batch != nullptr) index = batch->BeginGpuTiming(name);
        else if ((recorder = Recorder::Active()) != nullptr) index = recorder->BeginGpuTiming(category, commands);
        else GpuDescriptorProfile::DroppedGpu();
    }
    ~GpuWorkTiming() {
        if (batch != nullptr) batch->EndGpuTiming(index);
        else if (recorder != nullptr) recorder->EndGpuTiming(index);
    }
    GpuWorkTiming(const GpuWorkTiming&) = delete;
    GpuWorkTiming& operator=(const GpuWorkTiming&) = delete;
private:
    Recorder* recorder = nullptr;
    CommandBatch* batch = nullptr;
    std::uint32_t index = Recorder::NoTiming;
};

}
