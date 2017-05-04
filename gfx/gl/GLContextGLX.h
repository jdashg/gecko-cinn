/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GLCONTEXTGLX_H_
#define GLCONTEXTGLX_H_

#include "GLContext.h"
#include "GLXLibrary.h"
#include "mozilla/X11Util.h"

namespace mozilla {
namespace gl {

class GLContextGLX : public GLContext
{
public:
    const GLXContext mContext;
    Display* const mDisplay;
    const GLXDrawable mDrawable;
    const bool mDeleteDrawable;
    const bool mDoubleBuffered;

    GLXLibrary* const mGLX;

    const RefPtr<gfxXlibSurface> mPixmap;
    const bool mOwnsContext;


    MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GLContextGLX, override)

    static already_AddRefed<GLContextGLX>
    CreateGLContext(CreateContextFlags flags,
                    bool isOffscreen,
                    Display* display,
                    GLXDrawable drawable,
                    GLXFBConfig cfg,
                    bool deleteDrawable,
                    gfxXlibSurface* pixmap);

    static RefPtr<GLContextGLX>
    CreateForWindow(Display* aXDisplay, Window aXWindow, CreateContextFlags flags);

    ~GLContextGLX();

    virtual GLContextType GetContextType() const override { return GLContextType::GLX; }

    static GLContextGLX* Cast(GLContext* gl) {
        MOZ_ASSERT(gl->GetContextType() == GLContextType::GLX);
        return static_cast<GLContextGLX*>(gl);
    }

    bool Init() override;

    virtual bool MakeCurrentImpl(bool aForce) override;

    virtual bool IsCurrent() const override;

    virtual bool SetupLookupFunction() override;

    virtual bool IsDoubleBuffered() const override;

    virtual bool SwapBuffers() override;

    virtual void GetWSIInfo(nsCString* const out) const override;

    virtual bool IsConfigDepthStencilFlexible() const override { return false; }

    // Overrides the current GLXDrawable backing the context and makes the
    // context current.
    bool OverrideDrawable(GLXDrawable drawable);

    // Undoes the effect of a drawable override.
    bool RestoreDrawable();

private:
    friend class GLContextProviderGLX;

    GLContextGLX(CreateContextFlags flags,
                 bool isOffscreen,
                 Display* aDisplay,
                 GLXDrawable aDrawable,
                 GLXContext aContext,
                 bool aDeleteDrawable,
                 bool aDoubleBuffered,
                 gfxXlibSurface* aPixmap,
                 bool ownsContext);
};

}
}

#endif // GLCONTEXTGLX_H_
