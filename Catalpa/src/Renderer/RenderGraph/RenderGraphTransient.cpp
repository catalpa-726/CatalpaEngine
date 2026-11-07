#include "catpch.h"
#include "RenderGraphTransient.h"
#include "RHI/RHIDevice.h"

namespace Catalpa {
    RGTransientStorage::~RGTransientStorage() {
        for (auto buffer : buffers)
            device.ReleaseBuffer(buffer);
    }
} // namespace Catalpa
