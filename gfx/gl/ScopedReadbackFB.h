/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SCOPED_READBACK_FB_H_
#define SCOPED_READBACK_FB_H_

#include "GLContext.h"
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace gl {

class MozFramebuffer;

class ScopedReadbackFB
{
    GLContext* const mGL;
    const ScopedBindFramebuffer mAutoFB;
    const ScopedSurfaceLock mSurfLock;
    UniquePtr<MozFramebuffer> mIndirectFB;

public:
    explicit ScopedReadbackFB(SharedSurface* src);
    ~ScopedReadbackFB();
};

} // namespace gl
} // namespace mozilla

#endif // SCOPED_READBACK_FB_H_
