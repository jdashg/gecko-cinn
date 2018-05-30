/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGPU_RENDERING_CONTEXT_H_
#define WEBGPU_RENDERING_CONTEXT_H_

#include "nsWrapperCache.h"

namespace mozilla {
namespace webgpu {

class RenderingContext final
    : public SwapChain
{
};

} // namespace webgpu
} // namespace mozilla

#endif // WEBGPU_RENDERING_CONTEXT_H_
