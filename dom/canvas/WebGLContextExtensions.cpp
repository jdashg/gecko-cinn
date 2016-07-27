/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"

#include "AccessCheck.h"
#include "gfxPrefs.h"
#include "GLContext.h"
#include "mozilla/Preferences.h"
#include "nsString.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"

namespace mozilla {

#define FOR_EACH_PRIVILEGED_EXT(FUNC) \
    FUNC(WEBGL_debug_renderer_info, WebGLExtensionDebugRendererInfo) \
    FUNC(WEBGL_debug_shaders      , WebGLExtensionDebugShaders     )

#define FOR_EACH_EXT(FUNC) \
    FUNC(ANGLE_instanced_arrays        , WebGLExtensionInstancedArrays         ) \
    FUNC(EXT_blend_minmax              , WebGLExtensionBlendMinMax             ) \
    FUNC(EXT_color_buffer_float        , WebGLExtensionEXTColorBufferFloat     ) \
    FUNC(EXT_color_buffer_half_float   , WebGLExtensionColorBufferHalfFloat    ) \
    FUNC(EXT_disjoint_timer_query      , WebGLExtensionDisjointTimerQuery      ) \
    FUNC(EXT_frag_depth                , WebGLExtensionFragDepth               ) \
    FUNC(EXT_shader_texture_lod        , WebGLExtensionShaderTextureLod        ) \
    FUNC(EXT_sRGB                      , WebGLExtensionSRGB                    ) \
    FUNC(EXT_texture_filter_anisotropic, WebGLExtensionTextureFilterAnisotropic) \
    FUNC(OES_element_index_uint        , WebGLExtensionElementIndexUint        ) \
    FUNC(OES_standard_derivatives      , WebGLExtensionStandardDerivatives     ) \
    FUNC(OES_texture_float             , WebGLExtensionTextureFloat            ) \
    FUNC(OES_texture_float_linear      , WebGLExtensionTextureFloatLinear      ) \
    FUNC(OES_texture_half_float        , WebGLExtensionTextureHalfFloat        ) \
    FUNC(OES_texture_half_float_linear , WebGLExtensionTextureHalfFloatLinear  ) \
    FUNC(OES_vertex_array_object       , WebGLExtensionVertexArray             ) \
    FUNC(WEBGL_color_buffer_float      , WebGLExtensionColorBufferFloat        ) \
    FUNC(WEBGL_compressed_texture_atc  , WebGLExtensionCompressedTextureATC    ) \
    FUNC(WEBGL_compressed_texture_es3  , WebGLExtensionCompressedTextureES3    ) \
    FUNC(WEBGL_compressed_texture_etc1 , WebGLExtensionCompressedTextureETC1   ) \
    FUNC(WEBGL_compressed_texture_pvrtc, WebGLExtensionCompressedTexturePVRTC  ) \
    FUNC(WEBGL_compressed_texture_s3tc , WebGLExtensionCompressedTextureS3TC   ) \
    FUNC(WEBGL_depth_texture           , WebGLExtensionDepthTexture            ) \
    FUNC(WEBGL_draw_buffers            , WebGLExtensionDrawBuffers             ) \
    FUNC(WEBGL_lose_context            , WebGLExtensionLoseContext             )

/*static*/ const char*
WebGLContext::GetExtensionString(WebGLExtensionID ext)
{
    typedef EnumeratedArray<WebGLExtensionID, WebGLExtensionID::Max,
                            const char*> names_array_t;

    static names_array_t sExtensionNamesEnumeratedArray;
    static bool initialized = false;

    if (!initialized) {
        initialized = true;

#define FOO(ID,FUNC) sExtensionNamesEnumeratedArray[WebGLExtensionID::ID] = #ID;

        FOR_EACH_PRIVILEGED_EXT(FOO)
        FOR_EACH_EXT(FOO)

#undef FOO
    }

    return sExtensionNamesEnumeratedArray[ext];
}

bool
WebGLContext::IsExtensionEnabled(WebGLExtensionID ext) const
{
    return mExtensions[ext];
}

bool WebGLContext::IsExtensionSupported(JSContext* cx,
                                        WebGLExtensionID ext) const
{
    bool allowPrivilegedExts = false;

    // Chrome contexts need access to debug information even when
    // webgl.disable-extensions is set. This is used in the graphics
    // section of about:support
    if (NS_IsMainThread() &&
        xpc::AccessCheck::isChrome(js::GetContextCompartment(cx))) {
        allowPrivilegedExts = true;
    }

    if (gfxPrefs::WebGLPrivilegedExtensionsEnabled()) {
        allowPrivilegedExts = true;
    }

    if (allowPrivilegedExts) {
        switch (ext) {

#define FOO(ID,TYPE)               \
        case WebGLExtensionID::ID: \
            return TYPE::IsSupported(this);

            FOR_EACH_PRIVILEGED_EXT(FOO)

#undef FOO

        default:
            // For warnings-as-errors.
            break;
        }
    }

    return IsExtensionSupported(ext);
}

bool
WebGLContext::IsExtensionSupported(WebGLExtensionID ext) const
{
    if (mDisableExtensions)
        return false;

    switch (ext) {

#define FOO(ID,TYPE)           \
    case WebGLExtensionID::ID: \
        return TYPE::IsSupported(this);

        FOR_EACH_EXT(FOO)

#undef FOO

    case WebGLExtensionID::WEBGL_debug_renderer_info:
        return Preferences::GetBool("webgl.enable-debug-renderer-info", false);

    default:
        // For warnings-as-errors.
        break;
    }

    return false;
}

static bool
CompareWebGLExtensionName(const nsACString& name, const char* other)
{
    return name.Equals(other, nsCaseInsensitiveCStringComparator());
}

WebGLExtensionBase*
WebGLContext::EnableSupportedExtension(JSContext* js, WebGLExtensionID ext)
{
    if (!IsExtensionEnabled(ext)) {
        if (!IsExtensionSupported(js, ext))
            return nullptr;

        EnableExtension(ext);
    }

    return mExtensions[ext];
}

void
WebGLContext::GetExtension(JSContext* cx, const nsAString& wideName,
                           JS::MutableHandle<JSObject*> retval, ErrorResult& rv)
{
    retval.set(nullptr);

    if (IsContextLost())
        return;

    NS_LossyConvertUTF16toASCII name(wideName);

    WebGLExtensionID ext = WebGLExtensionID::Unknown;

    // step 1: figure what extension is wanted
    for (size_t i = 0; i < size_t(WebGLExtensionID::Max); i++) {
        WebGLExtensionID extension = WebGLExtensionID(i);

        if (CompareWebGLExtensionName(name, GetExtensionString(extension))) {
            ext = extension;
            break;
        }
    }

    if (ext == WebGLExtensionID::Unknown) {
        // We keep backward compatibility for these deprecated vendor-prefixed
        // alias. Do not add new ones anymore. Hide it behind the
        // webgl.enable-draft-extensions flag instead.

        if (CompareWebGLExtensionName(name, "MOZ_WEBGL_lose_context")) {
            ext = WebGLExtensionID::WEBGL_lose_context;

        } else if (CompareWebGLExtensionName(name, "MOZ_WEBGL_compressed_texture_s3tc")) {
            ext = WebGLExtensionID::WEBGL_compressed_texture_s3tc;

        } else if (CompareWebGLExtensionName(name, "MOZ_WEBGL_compressed_texture_atc")) {
            ext = WebGLExtensionID::WEBGL_compressed_texture_atc;

        } else if (CompareWebGLExtensionName(name, "MOZ_WEBGL_compressed_texture_pvrtc")) {
            ext = WebGLExtensionID::WEBGL_compressed_texture_pvrtc;

        } else if (CompareWebGLExtensionName(name, "MOZ_WEBGL_depth_texture")) {
            ext = WebGLExtensionID::WEBGL_depth_texture;
        }

        if (ext != WebGLExtensionID::Unknown) {
            GenerateWarning("getExtension('%s'): MOZ_ prefixed WebGL extension"
                            " strings are deprecated. Support for them will be"
                            " removed in the future. Use unprefixed extension"
                            " strings. To get draft extensions, set the"
                            " webgl.enable-draft-extensions preference.",
                            name.get());
        }
    }

    if (ext == WebGLExtensionID::Unknown)
        return;

    // step 2: check if the extension is supported
    if (!IsExtensionSupported(cx, ext))
        return;

    // step 3: if the extension hadn't been previously been created, create it now, thus enabling it
    WebGLExtensionBase* extObj = EnableSupportedExtension(cx, ext);
    if (!extObj)
        return;

    // Step 4: Enable any implied extensions.
    switch (ext) {
    case WebGLExtensionID::OES_texture_float:
        EnableSupportedExtension(cx, WebGLExtensionID::WEBGL_color_buffer_float);
        break;

    case WebGLExtensionID::OES_texture_half_float:
        EnableSupportedExtension(cx, WebGLExtensionID::EXT_color_buffer_half_float);
        break;

    default:
        break;
    }

    retval.set(WebGLObjectAsJSObject(cx, extObj, rv));
}

void
WebGLContext::EnableExtension(WebGLExtensionID ext)
{
    MOZ_ASSERT(IsExtensionEnabled(ext) == false);

    WebGLExtensionBase* obj = nullptr;

    switch (ext) {

#define FOO(ID,TYPE)           \
    case WebGLExtensionID::ID: \
        obj = new TYPE(this);  \
        break;

    FOR_EACH_PRIVILEGED_EXT(FOO)
    FOR_EACH_EXT(FOO)

#undef FOO

    default:
        gfxCriticalNote << "Invalid extension id: " << uint32_t(ext);
        return;
    }

    mExtensions[ext] = obj;
}

void
WebGLContext::GetSupportedExtensions(JSContext* cx,
                                     dom::Nullable< nsTArray<nsString> >& retval)
{
    retval.SetNull();
    if (IsContextLost())
        return;

    nsTArray<nsString>& arr = retval.SetValue();

    for (size_t i = 0; i < size_t(WebGLExtensionID::Max); i++) {
        WebGLExtensionID extension = WebGLExtensionID(i);

        if (IsExtensionSupported(cx, extension)) {
            const char* extStr = GetExtensionString(extension);
            arr.AppendElement(NS_ConvertUTF8toUTF16(extStr));
        }
    }

    /**
     * We keep backward compatibility for these deprecated vendor-prefixed
     * alias. Do not add new ones anymore. Hide it behind the
     * webgl.enable-draft-extensions flag instead.
     */
    if (IsExtensionSupported(cx, WebGLExtensionID::WEBGL_lose_context))
        arr.AppendElement(NS_LITERAL_STRING("MOZ_WEBGL_lose_context"));
    if (IsExtensionSupported(cx, WebGLExtensionID::WEBGL_compressed_texture_s3tc))
        arr.AppendElement(NS_LITERAL_STRING("MOZ_WEBGL_compressed_texture_s3tc"));
    if (IsExtensionSupported(cx, WebGLExtensionID::WEBGL_compressed_texture_atc))
        arr.AppendElement(NS_LITERAL_STRING("MOZ_WEBGL_compressed_texture_atc"));
    if (IsExtensionSupported(cx, WebGLExtensionID::WEBGL_compressed_texture_pvrtc))
        arr.AppendElement(NS_LITERAL_STRING("MOZ_WEBGL_compressed_texture_pvrtc"));
    if (IsExtensionSupported(cx, WebGLExtensionID::WEBGL_depth_texture))
        arr.AppendElement(NS_LITERAL_STRING("MOZ_WEBGL_depth_texture"));
}

#undef FOR_EACH_PRIVILEGED_EXT
#undef FOR_EACH_EXT

} // namespace mozilla
