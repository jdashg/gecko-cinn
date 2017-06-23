/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COPYABLECANVASLAYER_H
#define GFX_COPYABLECANVASLAYER_H

#include <stdint.h>                     // for uint32_t
#include "GLContextTypes.h"             // for GLContext
#include "Layers.h"                     // for CanvasLayer, etc
#include "gfxContext.h"                 // for gfxContext, etc
#include "gfxTypes.h"
#include "gfxPlatform.h"                // for gfxImageFormat
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/Preferences.h"        // for Preferences
#include "mozilla/RefPtr.h"             // for RefPtr
#include "mozilla/gfx/2D.h"             // for DrawTarget
#include "mozilla/mozalloc.h"           // for operator delete, etc
#include "nsISupportsImpl.h"            // for MOZ_COUNT_CTOR, etc

namespace mozilla {
namespace layers {

class ContentCanvasLayer : public CanvasLayer
{
private:
  RefPtr<dom::CanvasRenderingContext2D> mCanvas2D;
  RefPtr<WebGLContext> mWebGL;

  mutable uint64_t mFrameID;
  mutable RefPtr<gfx::DataSourceSurface> mReusableSurface;

protected:
  CanvasLayer::Source* mSource;

public:
  ContentCanvasLayer(LayerManager* aLayerManager, void* aImplData);
  virtual void Initialize(const Data& aData) override;

protected:
  virtual ~ContentCanvasLayer() override;

private:
  bool ShouldAlwaysRedraw() const {
    return (!mManager || !mManager->IsWidgetLayerManager());
  }
protected:
  RefPtr<CanvasLayer::FrameData> GetFrameForRedraw() const;

  RefPtr<gfx::DataSourceSurface> GetReusableSurface(const gfx::IntSize& aSize,
                                                    gfx::SurfaceFormat aFormat) const;

  void DrawTo(const FrameData* frame, gfx::DrawTarget* aDT,
              const gfx::Point& aDeviceOffset = gfx::Point(0, 0),
              Layer* aMaskLayer = nullptr) const;

private:
  RefPtr<gfx::SourceSurface> ToSourceSurface(TextureClient* texClient,
                                             gl::OriginPos* out_origin) const;
};

} // namespace layers
} // namespace mozilla

#endif
