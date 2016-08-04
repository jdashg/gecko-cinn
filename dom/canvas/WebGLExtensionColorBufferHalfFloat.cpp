/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "GLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "WebGLContext.h"
#include "WebGLFormats.h"

namespace mozilla {

WebGLExtensionColorBufferHalfFloat::WebGLExtensionColorBufferHalfFloat(WebGLContext* webgl,
                                                                       WebGLExtensionID extID)
    : WebGLExtensionHelper(webgl, extID)
{
    auto& fua = webgl->mFormatUsage;

    auto fnUpdateUsage = [&fua](GLenum sizedFormat, webgl::EffectiveFormat effFormat) {
        auto usage = fua->EditUsage(effFormat);
        usage->SetRenderable();
        fua->AllowRBFormat(sizedFormat, usage);
    };

#define FOO(x) fnUpdateUsage(LOCAL_GL_ ## x, webgl::EffectiveFormat::x)

    FOO(RGBA16F);
    FOO(RGB16F);

#undef FOO
}

/*static*/ bool
WebGLExtensionColorBufferHalfFloat::IsSupported(const WebGLContext* webgl)
{
    if (webgl->IsWebGL2())
        return false;

    return webgl->GL()->IsSupported(gl::GLFeature::renderbuffer_color_half_float);
}

IMPL_WEBGL_EXTENSION_GOOP(WebGLExtensionColorBufferHalfFloat, EXT_color_buffer_half_float)

} // namespace mozilla
