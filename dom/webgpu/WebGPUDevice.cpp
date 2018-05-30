/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGPUDevice.h"

#include "WebGPUBuffer.h"
#include "WebGPUCommandEncoder.h"
#include "WebGPUFence.h"
#include "WebGPUQueue.h"

namespace mozilla {
namespace webgpu {

RefPtr<Buffer>
Device::CreateBuffer(const WebGPUBufferDescriptor& desc)
{
    return new Buffer;
}

RefPtr<CommandEncoder>
Device::CreateCommandEncoder(const WebGPUCommandEncoderDescriptor& desc)
{
    return new CommandEncoder;
}

RefPtr<Fence>
Device::CreateFence(const WebGPUFenceDescriptor& desc)
{
    return new Fence;
}

RefPtr<Queue>
Device::GetQueue()
{
    return new Queue;
}

} // namespace webgpu
} // namespace mozilla
