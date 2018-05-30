/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGPU_INSTANCE_H_
#define WEBGPU_INSTANCE_H_

#include "nsWrapperCache.h"

namespace mozilla {
namespace webgpu {

class Instance final
    : public nsWrapperCache
{
    static void GetExtensions(dom::WebGPUExtensions& out);
    static void GetFeatures(dom::WebGPUFeature& out);
    static void GetLimits(dom::WebGPULimits& out);

    static RefPtr<Device> CreateDevice(const dom::WebGPUDeviceDescriptor& descriptor,
                                       ErrorResult& rv);
};

} // namespace webgpu
} // namespace mozilla

#endif // WEBGPU_INSTANCE_H_
