/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SURFACE_TYPES_H_
#define SURFACE_TYPES_H_

#include <stdint.h>

namespace mozilla {
namespace gl {

enum class SharedSurfaceType : uint8_t {
    Unknown = 0,

    Basic,
    EGLImageShare,
    EGLSurfaceANGLE,
    DXGLInterop,
    DXGLInterop2,
    IOSurface,
    GLXDrawable,
    SharedGLTexture,

    Max
};

} // namespace gl
} // namespace mozilla

#endif // SURFACE_TYPES_H_
