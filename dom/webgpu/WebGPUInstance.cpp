/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGPUInstance.h"

#include "WebGPUDevice.h"

namespace mozilla {
namespace webgpu {

class Device;

/*static*/ void
Instance::GetExtensions(dom::WebGPUExtensions& out)
{
    out.anisotropicFiltering = false;
}

/*static*/ void
Instance::GetFeatures(dom::WebGPUFeature& out)
{
    out.logicOp = false;
}

/*static*/ void
Instance::GetLimits(dom::WebGPULimits& out)
{
    out.maxBindGroups = 0;
}

/*static*/ RefPtr<Device>
Instance::CreateDevice(const dom::WebGPUDeviceDescriptor& descriptor, ErrorResult& out_rv)
{
    return new Device;
}

} // namespace webgpu
} // namespace mozilla
