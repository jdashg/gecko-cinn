/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientWebGLExtensions.h"

namespace mozilla {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(ClientWebGLExtensionBase)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(ClientWebGLExtensionBase, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(ClientWebGLExtensionBase, Release)

DEFINE_WEBGL_EXTENSION_GOOP(ANGLE_instanced_arrays,
                            WebGLExtensionInstancedArrays)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_blend_minmax, WebGLExtensionBlendMinMax)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_color_buffer_float,
                            WebGLExtensionEXTColorBufferFloat)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_color_buffer_half_float,
                            WebGLExtensionColorBufferHalfFloat)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_float_blend, WebGLExtensionFloatBlend)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_frag_depth, WebGLExtensionFragDepth)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_sRGB, WebGLExtensionSRGB)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_shader_texture_lod,
                            WebGLExtensionShaderTextureLod)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_texture_filter_anisotropic,
                            WebGLExtensionTextureFilterAnisotropic)
DEFINE_WEBGL_EXTENSION_GOOP(EXT_disjoint_timer_query,
                            WebGLExtensionDisjointTimerQuery)
DEFINE_WEBGL_EXTENSION_GOOP(MOZ_debug, WebGLExtensionMOZDebug)
DEFINE_WEBGL_EXTENSION_GOOP(OES_element_index_uint,
                            WebGLExtensionElementIndexUint)
DEFINE_WEBGL_EXTENSION_GOOP(OES_fbo_render_mipmap,
                            WebGLExtensionFBORenderMipmap)
DEFINE_WEBGL_EXTENSION_GOOP(OES_standard_derivatives,
                            WebGLExtensionStandardDerivatives)
DEFINE_WEBGL_EXTENSION_GOOP(OES_texture_float, WebGLExtensionTextureFloat)
DEFINE_WEBGL_EXTENSION_GOOP(OES_texture_float_linear,
                            WebGLExtensionTextureFloatLinear)
DEFINE_WEBGL_EXTENSION_GOOP(OES_texture_half_float,
                            WebGLExtensionTextureHalfFloat)
DEFINE_WEBGL_EXTENSION_GOOP(OES_texture_half_float_linear,
                            WebGLExtensionTextureHalfFloatLinear)
DEFINE_WEBGL_EXTENSION_GOOP(OES_vertex_array_object, WebGLExtensionVertexArray)
DEFINE_WEBGL_EXTENSION_GOOP(OVR_multiview2, WebGLExtensionMultiview)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_color_buffer_float,
                            WebGLExtensionColorBufferFloat)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_debug_renderer_info,
                            WebGLExtensionDebugRendererInfo)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_debug_shaders, WebGLExtensionDebugShaders)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_depth_texture, WebGLExtensionDepthTexture)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_draw_buffers, WebGLExtensionDrawBuffers)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_explicit_present,
                            WebGLExtensionExplicitPresent)
DEFINE_WEBGL_EXTENSION_GOOP(WEBGL_lose_context, WebGLExtensionLoseContext)

// --------------
// Compressed textures

void ClientWebGLContext::AddCompressedFormat(const GLenum format) {
  auto& state = *(mNotLost->generation);
  state.mCompressedTextureFormats.push_back(format);
}

// -

JSObject* WebGLExtensionCompressedTextureASTC::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WEBGL_compressed_texture_astc##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureASTC::WebGLExtensionCompressedTextureASTC(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);

  _(COMPRESSED_RGBA_ASTC_4x4_KHR)
  _(COMPRESSED_RGBA_ASTC_5x4_KHR)
  _(COMPRESSED_RGBA_ASTC_5x5_KHR)
  _(COMPRESSED_RGBA_ASTC_6x5_KHR)
  _(COMPRESSED_RGBA_ASTC_6x6_KHR)
  _(COMPRESSED_RGBA_ASTC_8x5_KHR)
  _(COMPRESSED_RGBA_ASTC_8x6_KHR)
  _(COMPRESSED_RGBA_ASTC_8x8_KHR)
  _(COMPRESSED_RGBA_ASTC_10x5_KHR)
  _(COMPRESSED_RGBA_ASTC_10x6_KHR)
  _(COMPRESSED_RGBA_ASTC_10x8_KHR)
  _(COMPRESSED_RGBA_ASTC_10x10_KHR)
  _(COMPRESSED_RGBA_ASTC_12x10_KHR)
  _(COMPRESSED_RGBA_ASTC_12x12_KHR)
  _
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR)
  _(COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR)

#undef _
}


JSObject* WebGLExtensionCompressedTextureBPTC::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::EXT_texture_compression_bptc##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureBPTC::WebGLExtensionCompressedTextureBPTC(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);
  _(COMPRESSED_RGBA_BPTC_UNORM)
  _(COMPRESSED_SRGB_ALPHA_BPTC_UNORM)
  _(COMPRESSED_RGB_BPTC_SIGNED_FLOAT)
  _(COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT)
#undef _
}


JSObject* WebGLExtensionCompressedTextureRGTC::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::EXT_texture_compression_rgtc##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureRGTC::WebGLExtensionCompressedTextureRGTC(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);
  _(COMPRESSED_RED_RGTC1)
  _(COMPRESSED_SIGNED_RED_RGTC1)
  _(COMPRESSED_RG_RGTC2)
  _(COMPRESSED_SIGNED_RG_RGTC2)
#undef _
}


JSObject* WebGLExtensionCompressedTextureES3::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WEBGL_compressed_texture_etc##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureES3::WebGLExtensionCompressedTextureES3(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);
  _(COMPRESSED_R11_EAC)
  _(COMPRESSED_SIGNED_R11_EAC)
  _(COMPRESSED_RG11_EAC)
  _(COMPRESSED_SIGNED_RG11_EAC)
  _(COMPRESSED_RGB8_ETC2)
  _(COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2)
  _(COMPRESSED_RGBA8_ETC2_EAC)

  // sRGB support is manadatory in GL 4.3 and GL ES 3.0, which are the only
  // versions to support ETC2.
  _(COMPRESSED_SRGB8_ALPHA8_ETC2_EAC)
  _(COMPRESSED_SRGB8_ETC2)
  _(COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2)
#undef _
}


JSObject* WebGLExtensionCompressedTextureETC1::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WEBGL_compressed_texture_etc1##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureETC1::WebGLExtensionCompressedTextureETC1(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
  webgl->AddCompressedFormat(LOCAL_GL_ETC1_RGB8_OES);
}


JSObject* WebGLExtensionCompressedTexturePVRTC::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WEBGL_compressed_texture_pvrtc##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTexturePVRTC::WebGLExtensionCompressedTexturePVRTC(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);
  _(COMPRESSED_RGB_PVRTC_4BPPV1)
  _(COMPRESSED_RGB_PVRTC_2BPPV1)
  _(COMPRESSED_RGBA_PVRTC_4BPPV1)
  _(COMPRESSED_RGBA_PVRTC_2BPPV1)
#undef _
}


JSObject* WebGLExtensionCompressedTextureS3TC::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WEBGL_compressed_texture_s3tc##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureS3TC::WebGLExtensionCompressedTextureS3TC(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);
  _(COMPRESSED_RGB_S3TC_DXT1_EXT)
  _(COMPRESSED_RGBA_S3TC_DXT1_EXT)
  _(COMPRESSED_RGBA_S3TC_DXT3_EXT)
  _(COMPRESSED_RGBA_S3TC_DXT5_EXT)
#undef _
}


JSObject* WebGLExtensionCompressedTextureS3TC_SRGB::WrapObject(JSContext* cx,
                                         JS::Handle<JSObject*> givenProto) {
  return dom::WEBGL_compressed_texture_s3tc_srgb##_Binding::Wrap(cx, this, givenProto);
}
WebGLExtensionCompressedTextureS3TC_SRGB::WebGLExtensionCompressedTextureS3TC_SRGB(
 const RefPtr<ClientWebGLContext>& webgl)
    : ClientWebGLExtensionBase(webgl) {
#define _(X) webgl->AddCompressedFormat(LOCAL_GL_##X);
  _(COMPRESSED_SRGB_S3TC_DXT1_EXT)
  _(COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT)
  _(COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT)
  _(COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
#undef _
}

}  // namespace mozilla
