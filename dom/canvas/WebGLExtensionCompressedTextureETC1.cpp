/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "GLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "WebGLContext.h"

namespace mozilla {

WebGLExtensionCompressedTextureETC1::WebGLExtensionCompressedTextureETC1(WebGLContext* webgl,
                                                                         WebGLExtensionID extID)
    : WebGLExtensionHelper(webgl, extID)
{
    RefPtr<WebGLContext> webgl_ = webgl; // Bug 1201275
    const auto fnAdd = [&webgl_](GLenum sizedFormat, webgl::EffectiveFormat effFormat) {
        auto& fua = webgl_->mFormatUsage;

        auto usage = fua->EditUsage(effFormat);
        usage->isFilterable = true;
        fua->AllowSizedTexFormat(sizedFormat, usage);

        webgl_->mCompressedTextureFormats.AppendElement(sizedFormat);
    };

#define FOO(x) LOCAL_GL_ ## x, webgl::EffectiveFormat::x

    fnAdd(FOO(ETC1_RGB8_OES));

#undef FOO
}

/*static*/ bool
WebGLExtensionCompressedTextureETC1::IsSupported(const WebGLContext* webgl)
{
    return webgl->GL()->IsExtensionSupported(gl::GLContext::OES_compressed_ETC1_RGB8_texture);
}

IMPL_WEBGL_EXTENSION_GOOP(WebGLExtensionCompressedTextureETC1, WEBGL_compressed_texture_etc1)

} // namespace mozilla
