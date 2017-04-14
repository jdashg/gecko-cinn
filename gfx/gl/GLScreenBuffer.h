/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* GLScreenBuffer is the abstraction for the "default framebuffer" used
 * by an offscreen GLContext. Since it's only for offscreen GLContext's,
 * it's only useful for things like WebGL, and is NOT used by the
 * compositor's GLContext. Remember that GLContext provides an abstraction
 * so that even if you want to draw to the 'screen', even if that's not
 * actually the screen, just draw to 0. This GLScreenBuffer class takes the
 * logic handling out of GLContext.
*/

#ifndef SCREEN_BUFFER_H_
#define SCREEN_BUFFER_H_

#include "GLContextTypes.h"
#include "GLDefs.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/UniquePtr.h"
#include "SharedSurface.h"
#include "SurfaceTypes.h"

namespace mozilla {
namespace layers {
class KnowsCompositor;
class LayersIPCChannel;
class SharedSurfaceTextureClient;
} // namespace layers

namespace gl {

class GLContext;
class SharedSurface;
class ShSurfHandle;
class SurfaceFactory;

class DrawBuffer
{
public:
    // Fallible!
    // But it may return true with *out_buffer==nullptr if unneeded.
    static UniquePtr<DrawBuffer> Create(GLContext* gl, const SurfaceCaps& caps,
                                        const GLFormats& formats,
                                        const gfx::IntSize& size);

protected:
    GLContext* const mGL;
public:
    const gfx::IntSize mSize;
    const GLsizei mSamples;
    const GLuint mFB;
protected:
    const GLuint mColorMSRB;
    const GLuint mDepthRB;
    const GLuint mStencilRB;

    DrawBuffer(GLContext* gl,
               const gfx::IntSize& size,
               GLsizei samples,
               GLuint fb,
               GLuint colorMSRB,
               GLuint depthRB,
               GLuint stencilRB)
        : mGL(gl)
        , mSize(size)
        , mSamples(samples)
        , mFB(fb)
        , mColorMSRB(colorMSRB)
        , mDepthRB(depthRB)
        , mStencilRB(stencilRB)
    {}

public:
    virtual ~DrawBuffer();
};

class GLScreenBuffer
{
public:
    // Infallible.
    static UniquePtr<GLScreenBuffer> Create(GLContext* gl,
                                            const gfx::IntSize& size,
                                            const SurfaceCaps& caps);
    static UniquePtr<SurfaceFactory>
    CreateFactory(GLContext* gl,
                  const SurfaceCaps& caps,
                  layers::KnowsCompositor* compositorConnection,
                  const layers::TextureFlags& flags);
private:
    static UniquePtr<SurfaceFactory>
    CreateFactory(GLContext* gl,
                  const SurfaceCaps& caps,
                  layers::LayersIPCChannel* ipcChannel,
                  const mozilla::layers::LayersBackend backend,
                  const layers::TextureFlags& flags);

    GLContext* const mGL; // Owns us.
public:
    const SurfaceCaps mCaps;
protected:
    UniquePtr<SurfaceFactory> mFactory;

    RefPtr<layers::SharedSurfaceTextureClient> mBack;
    RefPtr<layers::SharedSurfaceTextureClient> mFront;

    UniquePtr<DrawBuffer> mDraw;

    bool mNeedsBlit;

    GLenum mUserReadBufferMode;
    GLenum mUserDrawBufferMode;

    // Below are the parts that help us pretend to be framebuffer 0:
    GLuint mUserDrawFB;
    GLuint mUserReadFB;
    GLuint mDriverDrawFB;
    GLuint mDriverReadFB;

    GLScreenBuffer(GLContext* gl,
                   const SurfaceCaps& caps,
                   UniquePtr<SurfaceFactory> factory);

public:
    virtual ~GLScreenBuffer();

    SurfaceFactory* Factory() const { return mFactory.get(); }

    const decltype(mFront)& Front() const { return mFront; }

    GLuint DrawFB() const {
        if (!mDraw)
            return ReadFB();

        return mDraw->mFB;
    }

    GLuint ReadFB() const;

    GLsizei Samples() const {
        if (!mDraw)
            return 0;

        return mDraw->mSamples;
    }

    uint32_t DepthBits() const;

    const gfx::IntSize& Size() const;

    void BindFramebuffer(GLenum target, GLuint userFB);
    GLuint CurDrawFB() const;
    GLuint CurReadFB() const;

    void RequireBlit() { mNeedsBlit = true; }
    void AssureBlitted();
    void AfterDrawCall();
    void BeforeReadCall();

    bool CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x,
                        GLint y, GLsizei width, GLsizei height, GLint border);

    void SetReadBuffer(GLenum userMode);
    void SetDrawBuffer(GLenum userMode);

    GLenum GetReadBufferMode() const { return mUserReadBufferMode; }
    GLenum GetDrawBufferMode() const { return mUserDrawBufferMode; }

    /**
     * Attempts to read pixels from the current bound framebuffer, if
     * it is backed by a SharedSurface.
     *
     * Returns true if the pixel data has been read back, false
     * otherwise.
     */
    bool ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, GLvoid* pixels);

    // Morph changes the factory used to create surfaces.
    bool Morph(layers::KnowsCompositor* info, bool force = false);

protected:
    // Returns the old mBack on success.
    bool Swap(const gfx::IntSize& size,
              RefPtr<layers::SharedSurfaceTextureClient>* out_oldBack);
    void RefreshFBBindings();

public:
    bool PublishFrame();
    bool Resize(const gfx::IntSize& size);

    bool IsDrawFramebufferDefault() const;
    bool IsReadFramebufferDefault() const;
};

} // namespace gl
} // namespace mozilla

#endif  // SCREEN_BUFFER_H_
