/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BasicCanvasLayer.h"

#include "AsyncCanvasRenderer.h"
#include "dom/canvas/WebGLContext.h"
#include "basic/BasicLayers.h"          // for BasicLayerManager
#include "basic/BasicLayersImpl.h"      // for GetEffectiveOperator
#include "mozilla/mozalloc.h"           // for operator new
#include "nsCOMPtr.h"                   // for already_AddRefed
#include "nsISupportsImpl.h"            // for Layer::AddRef, etc
#include "gfx2DGlue.h"
#include "GLScreenBuffer.h"
#include "GLContext.h"
#include "gfxUtils.h"
#include "mozilla/layers/PersistentBufferProvider.h"
#include "client/TextureClientSharedSurface.h"

class gfxContext;

using namespace mozilla::gfx;
using namespace mozilla::gl;

namespace mozilla {
namespace layers {

already_AddRefed<SourceSurface>
BasicCanvasLayer::UpdateSurface()
{
  if (mAsyncRenderer) {
    MOZ_ASSERT(!mBufferProvider);
    MOZ_ASSERT(!mGLContext);
    return mAsyncRenderer->GetSurface();
  }

  RefPtr<layers::SharedSurfaceTextureClient> frontTex;
  SharedSurface* frontbuffer = nullptr;
  bool isAlphaPremult = true;
  if (mGLFrontbuffer) {
    frontbuffer = mGLFrontbuffer.get();
  } else if (mWebGL) {
    frontTex = mWebGL->GetLayerFrame(nullptr, LayersBackend::LAYERS_BASIC);
    if (frontTex) {
      isAlphaPremult = !bool(frontTex->GetFlags() & TextureFlags::NON_PREMULTIPLIED);
      frontbuffer = frontTex->Surf();
    }
  } else {
    return nullptr;
  }

  if (!frontbuffer) {
    NS_WARNING("Null frame received.");
    return nullptr;
  }

  IntSize readSize(frontbuffer->mSize);
  SurfaceFormat format = (GetContentFlags() & CONTENT_OPAQUE)
                          ? SurfaceFormat::B8G8R8X8
                          : SurfaceFormat::B8G8R8A8;
  bool needsPremult = frontbuffer->mHasAlpha && !isAlphaPremult;

  RefPtr<DataSourceSurface> resultSurf = GetTempSurface(readSize, format);
  // There will already be a warning from inside of GetTempSurface, but
  // it doesn't hurt to complain:
  if (NS_WARN_IF(!resultSurf)) {
    return nullptr;
  }

  // Readback handles Flush/MarkDirty.
  frontbuffer->Readback(resultSurf);
  if (needsPremult) {
    gfxUtils::PremultiplyDataSurface(resultSurf, resultSurf);
  }
  MOZ_ASSERT(resultSurf);

  return resultSurf.forget();
}

void
BasicCanvasLayer::Paint(DrawTarget* aDT,
                        const Point& aDeviceOffset,
                        Layer* aMaskLayer)
{
  if (IsHidden())
    return;

  bool bufferPoviderSnapshot = false;
  RefPtr<SourceSurface> surface;
  if (IsDirty()) {
    Painted();

    FirePreTransactionCallback();

    surface = UpdateSurface();
    if (!surface && mBufferProvider) {
      surface = mBufferProvider->BorrowSnapshot();
      bufferPoviderSnapshot = !!surface;
    }

    FireDidTransactionCallback();
  }

  if (!surface) {
    return;
  }

  const bool needsYFlip = (mOriginPos == gl::OriginPos::BottomLeft);

  Matrix oldTM;
  if (needsYFlip) {
    oldTM = aDT->GetTransform();
    aDT->SetTransform(Matrix(oldTM).
                        PreTranslate(0.0f, mBounds.height).
                        PreScale(1.0f, -1.0f));
  }

  FillRectWithMask(aDT, aDeviceOffset,
                   Rect(0, 0, mBounds.width, mBounds.height),
                   surface, mSamplingFilter,
                   DrawOptions(GetEffectiveOpacity(), GetEffectiveOperator(this)),
                   aMaskLayer);

  if (needsYFlip) {
    aDT->SetTransform(oldTM);
  }

  if (bufferPoviderSnapshot) {
    mBufferProvider->ReturnSnapshot(surface.forget());
  }
}

already_AddRefed<CanvasLayer>
BasicLayerManager::CreateCanvasLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  RefPtr<CanvasLayer> layer = new BasicCanvasLayer(this);
  return layer.forget();
}

} // namespace layers
} // namespace mozilla
