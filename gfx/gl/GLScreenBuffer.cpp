/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLScreenBuffer.h"

#include <cstring>
#include "CompositorTypes.h"
#include "gfxPrefs.h"
#include "GLContext.h"
#include "GLBlitHelper.h"
#include "GLReadTexImageHelper.h"
#include "SharedSurfaceEGL.h"
#include "SharedSurfaceGL.h"
#include "ScopedGLHelpers.h"
#include "gfx2DGlue.h"
#include "../layers/ipc/ShadowLayers.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/layers/TextureClientSharedSurface.h"

#ifdef XP_WIN
#include "SharedSurfaceANGLE.h"         // for SurfaceFactory_ANGLEShareHandle
#include "SharedSurfaceD3D11Interop.h"  // for SurfaceFactory_D3D11Interop
#include "mozilla/gfx/DeviceManagerDx.h"
#endif

#ifdef XP_MACOSX
#include "SharedSurfaceIO.h"
#endif

#ifdef GL_PROVIDER_GLX
#include "GLXLibrary.h"
#include "SharedSurfaceGLX.h"
#endif

namespace mozilla {
namespace gl {

using gfx::SurfaceFormat;

UniquePtr<GLScreenBuffer>
GLScreenBuffer::Create(GLContext* gl,
                       const gfx::IntSize& size,
                       const SurfaceCaps& caps)
{
    SurfaceCaps backbufferCaps = caps;
    if (caps.antialias) {
        if (!gl->IsSupported(GLFeature::framebuffer_multisample))
            return nullptr;

        backbufferCaps.antialias = false;
        backbufferCaps.depth = false;
        backbufferCaps.stencil = false;
    }

    layers::TextureFlags flags = layers::TextureFlags::ORIGIN_BOTTOM_LEFT;
    if (!caps.premultAlpha) {
        flags |= layers::TextureFlags::NON_PREMULTIPLIED;
    }

    UniquePtr<SurfaceFactory> factory;
    factory.reset(new SurfaceFactory_Basic(gl, backbufferCaps, flags));
    return UniquePtr<GLScreenBuffer>( new GLScreenBuffer(gl, caps, Move(factory)) );
}

/* static */ UniquePtr<SurfaceFactory>
GLScreenBuffer::CreateFactory(GLContext* gl,
                              const SurfaceCaps& caps,
                              KnowsCompositor* compositorConnection,
                              const layers::TextureFlags& flags)
{
  return CreateFactory(gl, caps, compositorConnection->GetTextureForwarder(),
                       compositorConnection->GetCompositorBackendType(), flags);
}

/* static */ UniquePtr<SurfaceFactory>
GLScreenBuffer::CreateFactory(GLContext* gl,
                              const SurfaceCaps& caps,
                              LayersIPCChannel* ipcChannel,
                              const mozilla::layers::LayersBackend backend,
                              const layers::TextureFlags& flags)
{
    UniquePtr<SurfaceFactory> factory = nullptr;
    if (!gfxPrefs::WebGLForceLayersReadback()) {
        switch (backend) {
            case mozilla::layers::LayersBackend::LAYERS_OPENGL: {
#if defined(XP_MACOSX)
                factory = SurfaceFactory_IOSurface::Create(gl, caps, ipcChannel, flags);
#elif defined(GL_PROVIDER_GLX)
                if (sGLXLibrary.UseTextureFromPixmap())
                  factory = SurfaceFactory_GLXDrawable::Create(gl, caps, ipcChannel, flags);
#elif defined(MOZ_WIDGET_UIKIT)
                factory = MakeUnique<SurfaceFactory_GLTexture>(mGLContext, caps, ipcChannel, mFlags);
#else
                if (gl->GetContextType() == GLContextType::EGL) {
                    if (XRE_IsParentProcess()) {
                        factory = SurfaceFactory_EGLImage::Create(gl, caps, ipcChannel, flags);
                    }
                }
#endif
                break;
            }
            case mozilla::layers::LayersBackend::LAYERS_D3D11: {
#ifdef XP_WIN
                // Enable surface sharing only if ANGLE and compositing devices
                // are both WARP or both not WARP
                gfx::DeviceManagerDx* dm = gfx::DeviceManagerDx::Get();
                if (gl->IsANGLE() &&
                    (gl->IsWARP() == dm->IsWARP()) &&
                    dm->TextureSharingWorks())
                {
                    factory = SurfaceFactory_ANGLEShareHandle::Create(gl, caps, ipcChannel, flags);
                }

                if (!factory && gfxPrefs::WebGLDXGLEnabled()) {
                  factory = SurfaceFactory_D3D11Interop::Create(gl, caps, ipcChannel, flags);
                }
#endif
              break;
            }
            default:
              break;
        }

#ifdef GL_PROVIDER_GLX
        if (!factory && sGLXLibrary.UseTextureFromPixmap()) {
            factory = SurfaceFactory_GLXDrawable::Create(gl, caps, ipcChannel, flags);
        }
#endif
    }

    return factory;
}

GLScreenBuffer::GLScreenBuffer(GLContext* gl,
                               const SurfaceCaps& caps,
                               UniquePtr<SurfaceFactory> factory)
    : mGL(gl)
    , mCaps(caps)
    , mFactory(Move(factory))
    , mNeedsBlit(true)
    , mUserReadBufferMode(LOCAL_GL_BACK)
    , mUserDrawBufferMode(LOCAL_GL_BACK)
    , mUserDrawFB(0)
    , mUserReadFB(0)
    , mDriverDrawFB(0)
    , mDriverReadFB(0)
{ }

GLScreenBuffer::~GLScreenBuffer()
{
    mFactory = nullptr;
    mDraw = nullptr;

    if (mBack) {
        // Detach mBack cleanly.
        mBack->Surf()->ProducerRelease();
        MOZ_ALWAYS_TRUE(mGL->PopSurfaceLock() == mBack->Surf());
    }
}

GLuint
GLScreenBuffer::ReadFB() const
{
    return mBack->Surf()->mFB;
}

const gfx::IntSize&
GLScreenBuffer::Size() const
{
    return mBack->Surf()->mSize;
}

void
GLScreenBuffer::BindFramebuffer(GLenum target, GLuint userFB)
{
    GLuint driverFB = 0;

    const auto fnUpdateDraw = [&]() {
        mUserDrawFB = userFB;
        mDriverDrawFB = (mUserDrawFB ? mUserDrawFB : DrawFB());
        driverFB = mDriverDrawFB;
    };
    const auto fnUpdateRead = [&]() {
        mUserReadFB = userFB;
        mDriverReadFB = (mUserReadFB ? mUserReadFB : ReadFB());
        driverFB = mDriverReadFB;
    };

    switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
        fnUpdateDraw();
        fnUpdateRead();

        if (mDriverDrawFB != mDriverReadFB) {
            mGL->raw_fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, mDriverDrawFB);
            mGL->raw_fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, mDriverReadFB);
            return;
        }
        break;

    case LOCAL_GL_DRAW_FRAMEBUFFER:
        fnUpdateDraw();
        break;

    case LOCAL_GL_READ_FRAMEBUFFER:
        fnUpdateRead();
        break;
    }

    mGL->raw_fBindFramebuffer(target, driverFB);
}

GLuint
GLScreenBuffer::CurDrawFB() const
{
#ifdef DEBUG
    // Don't need a branch here, because:
    // LOCAL_GL_DRAW_FRAMEBUFFER_BINDING == LOCAL_GL_FRAMEBUFFER_BINDING == 0x8CA6
    GLuint actual = 0;
    mGL->raw_fGetIntegerv(LOCAL_GL_FRAMEBUFFER_BINDING, (GLint*)&actual);

    const auto& predicted = mDriverDrawFB;
    if (predicted != actual) {
        printf_stderr("Misprediction: Bound draw FB predicted: %d. Was: %d.\n",
                      predicted, actual);
        MOZ_ASSERT(false, "Draw FB binding misprediction!");
    }
#endif

    return mUserDrawFB;
}

GLuint
GLScreenBuffer::CurReadFB() const
{
#ifdef DEBUG
    GLuint actual = 0;
    if (mGL->IsSupported(GLFeature::split_framebuffer))
        mGL->raw_fGetIntegerv(LOCAL_GL_READ_FRAMEBUFFER_BINDING, (GLint*)&actual);
    else
        mGL->raw_fGetIntegerv(LOCAL_GL_FRAMEBUFFER_BINDING, (GLint*)&actual);

    const auto& predicted = mDriverReadFB;
    if (predicted != actual) {
        printf_stderr("Misprediction: Bound read FB predicted: %d. Was: %d.\n",
                      predicted, actual);
        MOZ_ASSERT(false, "Read FB binding misprediction!");
    }
#endif

    return mUserReadFB;
}

void
GLScreenBuffer::AfterDrawCall()
{
    if (mUserDrawFB != 0)
        return;

    RequireBlit();
}

void
GLScreenBuffer::BeforeReadCall()
{
    if (mUserReadFB != 0)
        return;

    AssureBlitted();
}

bool
GLScreenBuffer::CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x,
                               GLint y, GLsizei width, GLsizei height, GLint border)
{
    if (CurReadFB() != 0)
        return false;

    const auto& surf = mBack->Surf();
    return surf->CopyTexImage2D(target, level, internalformat, x, y, width, height,
                                border);
}

bool
GLScreenBuffer::ReadPixels(GLint x, GLint y,
                           GLsizei width, GLsizei height,
                           GLenum format, GLenum type,
                           GLvoid* pixels)
{
    if (CurReadFB() != 0)
        return false;

    const auto& surf = mBack->Surf();
    return surf->ReadPixels(x, y, width, height, format, type, pixels);
}

void
GLScreenBuffer::AssureBlitted()
{
    if (!mNeedsBlit)
        return;
    mNeedsBlit = false;

    if (!mDraw)
        return;

    GLuint drawFB = DrawFB();
    GLuint readFB = ReadFB();

    MOZ_ASSERT(drawFB != 0);
    MOZ_ASSERT(drawFB != readFB);
    MOZ_ASSERT(mGL->IsSupported(GLFeature::split_framebuffer));

    const auto& srcSize = mDraw->mSize;
    const auto& destSize = mBack->Surf()->mSize;
    MOZ_ASSERT(srcSize == destSize);

    const ScopedBindFramebuffer boundFB(mGL);
    const ScopedGLState scissor(mGL, LOCAL_GL_SCISSOR_TEST, false);
    const ScopedBypassScreen bypass(mGL);

    mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, drawFB);
    mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, readFB);

    if (mGL->IsSupported(GLFeature::framebuffer_blit)) {
        mGL->fBlitFramebuffer(0, 0,  srcSize.width,  srcSize.height,
                              0, 0, destSize.width, destSize.height,
                              LOCAL_GL_COLOR_BUFFER_BIT,
                              LOCAL_GL_NEAREST);
    } else if (mGL->IsExtensionSupported(GLContext::APPLE_framebuffer_multisample)) {
        mGL->fResolveMultisampleFramebufferAPPLE();
    } else {
        MOZ_CRASH("GFX: No available blit methods.");
    }
    // Done!
}

bool
GLScreenBuffer::Morph(layers::KnowsCompositor* const info, const bool force)
{
    if (mFactory->mType != SharedSurfaceType::Basic && !force)
        return false;

    auto newFactory = CreateFactory(mFactory->mGL, mFactory->mCaps, info,
                                    mFactory->mFlags);
    if (!newFactory)
        return false;

    mFactory = Move(newFactory);
    return true;
}

bool
GLScreenBuffer::Swap(const gfx::IntSize& size,
                     RefPtr<layers::SharedSurfaceTextureClient>* const out_oldBack)
{
    AssureBlitted();

    RefPtr<SharedSurfaceTextureClient> newBack = mFactory->NewTexClient(size);
    if (!newBack)
        return false;

    if (mCaps.antialias) {
        if (!mDraw || mDraw->mSize != size) {
            const auto& formats = mFactory->mFormats;
            UniquePtr<DrawBuffer> newDraw = DrawBuffer::Create(mGL, mCaps, formats, size);
            if (!newDraw)
                return false;

            mDraw = Move(newDraw);
        }
    }

    ////
    // Swap!

    if (mBack) {
        const auto& oldSurf = mBack->Surf();
        MOZ_ALWAYS_TRUE(mGL->PopSurfaceLock() == oldSurf);
        mBack->Surf()->ProducerRelease();
    }

    RefPtr<SharedSurfaceTextureClient> oldBack = mBack;
    mBack = newBack;

    mGL->PushSurfaceLock(mBack->Surf());
    mBack->Surf()->ProducerAcquire();

    RequireBlit();

    ////
    // Fixup

    if (mGL->IsSupported(gl::GLFeature::draw_buffers)) {
        mGL->raw_fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, DrawFB());
        SetDrawBuffer(mUserDrawBufferMode);
    }

    if (mGL->IsSupported(gl::GLFeature::read_buffer)) {
        mGL->raw_fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, ReadFB());
        SetReadBuffer(mUserReadBufferMode);
    }

    RefreshFBBindings();

    *out_oldBack = oldBack;
    return true;
}

void
GLScreenBuffer::RefreshFBBindings()
{
    if (mUserDrawFB == mUserReadFB) {
        BindFramebuffer(LOCAL_GL_FRAMEBUFFER, mUserDrawFB);
    } else {
        BindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, mUserDrawFB);
        BindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, mUserReadFB);
    }
}

bool
GLScreenBuffer::PublishFrame()
{
    RefPtr<layers::SharedSurfaceTextureClient> oldBack;
    if (!Swap(mBack->Surf()->mSize, &oldBack))
        return false;

    mFront = oldBack;

    if (mCaps.preserve &&
        mFront &&
        mBack &&
        !mDraw)
    {
        //uint32_t srcPixel = ReadPixel(src);
        //uint32_t destPixel = ReadPixel(dest);
        //printf_stderr("Before: src: 0x%08x, dest: 0x%08x\n", srcPixel, destPixel);
#ifdef DEBUG
        GLContext::LocalErrorScope errorScope(*mGL);
#endif
        const auto& frontSurf = mFront->Surf();
        mGL->PushSurfaceLock(nullptr);
        frontSurf->ProducerReadAcquire();

        mBack->Surf()->CopyFrom(frontSurf);

        frontSurf->ProducerReadRelease();
        mGL->PopSurfaceLock();

#ifdef DEBUG
        MOZ_ASSERT(!errorScope.GetError());
#endif

        //srcPixel = ReadPixel(src);
        //destPixel = ReadPixel(dest);
        //printf_stderr("After: src: 0x%08x, dest: 0x%08x\n", srcPixel, destPixel);
    }

    return true;
}

bool
GLScreenBuffer::Resize(const gfx::IntSize& size)
{
    RefPtr<layers::SharedSurfaceTextureClient> oldBack;
    return Swap(size, &oldBack);
}

static GLenum
DriverModeForDriverFB(GLenum userMode, GLuint driverFB)
{
    if (userMode == LOCAL_GL_NONE)
        return userMode;

    MOZ_ASSERT(userMode == LOCAL_GL_BACK ||
               userMode == LOCAL_GL_FRONT);

    if (driverFB != 0)
        return LOCAL_GL_COLOR_ATTACHMENT0;

    return userMode;
}

void
GLScreenBuffer::SetDrawBuffer(GLenum mode)
{
    MOZ_ASSERT(mode != LOCAL_GL_COLOR_ATTACHMENT0);
    MOZ_ASSERT(mGL->IsSupported(gl::GLFeature::draw_buffers));
    MOZ_ASSERT(CurDrawFB() == 0);

    if (!mGL->IsSupported(GLFeature::draw_buffers))
        return;

    mUserDrawBufferMode = mode;

    mGL->MakeCurrent();
    const GLenum driverMode = DriverModeForDriverFB(mode, DrawFB());
    mGL->fDrawBuffers(1, &driverMode);
}

void
GLScreenBuffer::SetReadBuffer(GLenum mode)
{
    MOZ_ASSERT(mode != LOCAL_GL_COLOR_ATTACHMENT0);
    MOZ_ASSERT(mGL->IsSupported(gl::GLFeature::read_buffer));
    MOZ_ASSERT(CurReadFB() == 0);

    if (!mGL->IsSupported(GLFeature::read_buffer))
        return;

    mUserReadBufferMode = mode;

    mGL->MakeCurrent();
    const GLenum driverMode = DriverModeForDriverFB(mode, ReadFB());
    mGL->fReadBuffer(driverMode);
}

bool
GLScreenBuffer::IsDrawFramebufferDefault() const
{
    if (!mDraw)
        return IsReadFramebufferDefault();
    return mDraw->mFB == 0;
}

bool
GLScreenBuffer::IsReadFramebufferDefault() const
{
    return mBack->Surf()->mFB == 0;
}

uint32_t
GLScreenBuffer::DepthBits() const
{
    const GLFormats& formats = mFactory->mFormats;

    if (formats.depth == LOCAL_GL_DEPTH_COMPONENT16)
        return 16;

    return 24;
}

////////////////////////////////////////////////////////////////////////
// Utils

static void
RenderbufferStorageBySamples(GLContext* aGL, GLsizei aSamples,
                             GLenum aInternalFormat, const gfx::IntSize& aSize)
{
    if (aSamples) {
        aGL->fRenderbufferStorageMultisample(LOCAL_GL_RENDERBUFFER,
                                             aSamples,
                                             aInternalFormat,
                                             aSize.width, aSize.height);
    } else {
        aGL->fRenderbufferStorage(LOCAL_GL_RENDERBUFFER,
                                  aInternalFormat,
                                  aSize.width, aSize.height);
    }
}

static GLuint
CreateRenderbuffer(GLContext* aGL, GLenum aFormat, GLsizei aSamples,
                   const gfx::IntSize& aSize)
{
    GLuint rb = 0;
    aGL->fGenRenderbuffers(1, &rb);
    ScopedBindRenderbuffer autoRB(aGL, rb);

    RenderbufferStorageBySamples(aGL, aSamples, aFormat, aSize);

    return rb;
}

static void
CreateRenderbuffersForOffscreen(GLContext* aGL, const GLFormats& aFormats,
                                const gfx::IntSize& aSize, bool aMultisample,
                                GLuint* aColorMSRB, GLuint* aDepthRB,
                                GLuint* aStencilRB)
{
    GLsizei samples = aMultisample ? aFormats.samples : 0;
    if (aColorMSRB) {
        MOZ_ASSERT(aFormats.samples > 0);
        MOZ_ASSERT(aFormats.color_rbFormat);

        GLenum colorFormat = aFormats.color_rbFormat;
        if (aGL->IsANGLE()) {
            MOZ_ASSERT(colorFormat == LOCAL_GL_RGBA8);
            colorFormat = LOCAL_GL_BGRA8_EXT;
        }

        *aColorMSRB = CreateRenderbuffer(aGL, colorFormat, samples, aSize);
    }

    if (aDepthRB &&
        aStencilRB &&
        aFormats.depthStencil)
    {
        *aDepthRB = CreateRenderbuffer(aGL, aFormats.depthStencil, samples, aSize);
        *aStencilRB = *aDepthRB;
    } else {
        if (aDepthRB) {
            MOZ_ASSERT(aFormats.depth);

            *aDepthRB = CreateRenderbuffer(aGL, aFormats.depth, samples, aSize);
        }

        if (aStencilRB) {
            MOZ_ASSERT(aFormats.stencil);

            *aStencilRB = CreateRenderbuffer(aGL, aFormats.stencil, samples, aSize);
        }
    }
}

////////////////////////////////////////////////////////////////////////
// DrawBuffer

UniquePtr<DrawBuffer>
DrawBuffer::Create(GLContext* const gl,
                   const SurfaceCaps& caps,
                   const GLFormats& formats,
                   const gfx::IntSize& size)
{
    MOZ_ASSERT(formats.samples > 0);
    MOZ_ASSERT(formats.samples <= gl->MaxSamples());

    GLuint colorMSRB = 0;
    GLuint depthRB   = 0;
    GLuint stencilRB = 0;

    GLuint* pColorMSRB = caps.antialias ? &colorMSRB : nullptr;
    GLuint* pDepthRB   = caps.depth     ? &depthRB   : nullptr;
    GLuint* pStencilRB = caps.stencil   ? &stencilRB : nullptr;

    if (!formats.color_rbFormat)
        pColorMSRB = nullptr;

    if (pDepthRB && pStencilRB) {
        if (!formats.depth && !formats.depthStencil)
            pDepthRB = nullptr;

        if (!formats.stencil && !formats.depthStencil)
            pStencilRB = nullptr;
    } else {
        if (!formats.depth)
            pDepthRB = nullptr;

        if (!formats.stencil)
            pStencilRB = nullptr;
    }

    GLContext::LocalErrorScope localError(*gl);

    CreateRenderbuffersForOffscreen(gl, formats, size, caps.antialias,
                                    pColorMSRB, pDepthRB, pStencilRB);

    GLuint fb = 0;
    gl->fGenFramebuffers(1, &fb);
    gl->AttachBuffersToFB(0, colorMSRB, depthRB, stencilRB, fb);

    const GLsizei samples = formats.samples;
    UniquePtr<DrawBuffer> ret( new DrawBuffer(gl, size, samples, fb, colorMSRB,
                                              depthRB, stencilRB) );

    GLenum err = localError.GetError();
    MOZ_ASSERT_IF(err != LOCAL_GL_NO_ERROR, err == LOCAL_GL_OUT_OF_MEMORY);
    if (err || !gl->IsFramebufferComplete(fb))
        return nullptr;

    return ret;
}

DrawBuffer::~DrawBuffer()
{
    if (!mGL->MakeCurrent())
        return;

    GLuint fb = mFB;
    GLuint rbs[] = {
        mColorMSRB,
        mDepthRB,
        (mStencilRB != mDepthRB) ? mStencilRB : 0, // Don't double-delete DEPTH_STENCIL RBs.
    };

    mGL->fDeleteFramebuffers(1, &fb);
    mGL->fDeleteRenderbuffers(3, rbs);
}

} /* namespace gl */
} /* namespace mozilla */
