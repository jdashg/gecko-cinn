/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BasicCanvasLayer.h"
#include "AsyncCanvasRenderer.h"
#include "basic/BasicLayers.h"          // for BasicLayerManager
#include "basic/BasicLayersImpl.h"      // for GetEffectiveOperator
#include "mozilla/mozalloc.h"           // for operator new
#include "nsCOMPtr.h"                   // for already_AddRefed
#include "nsISupportsImpl.h"            // for Layer::AddRef, etc
#include "gfx2DGlue.h"
#include "GLContext.h"
#include "gfxUtils.h"
#include "SharedSurface.h"
#include "mozilla/layers/PersistentBufferProvider.h"
#include "client/TextureClientSharedSurface.h"

class gfxContext;

using namespace mozilla::gfx;
using namespace mozilla::gl;

namespace mozilla {
namespace layers {

RefPtr<SourceSurface>
BasicCanvasLayer::UpdateSurface(gl::OriginPos* const out_origin)
{
  const auto& frontTex = GetFrontTex();
  if (!frontTex) {
    return nullptr;
  }
  const auto& frontSurf = frontTex->Surf();

  IntSize readSize(frontSurf->mSize);
  SurfaceFormat format = (GetContentFlags() & CONTENT_OPAQUE)
                          ? SurfaceFormat::B8G8R8X8
                          : SurfaceFormat::B8G8R8A8;

  RefPtr<DataSourceSurface> resultSurf = GetTempSurface(readSize, format);
  // There will already be a warning from inside of GetTempSurface, but
  // it doesn't hurt to complain:
  if (NS_WARN_IF(!resultSurf)) {
    return nullptr;
  }

  // Readback handles Flush/MarkDirty.
  Readback(frontSurf, resultSurf);
  const bool isNonPremult = bool(frontTex->GetFlags() & TextureFlags::NON_PREMULTIPLIED);
  if (isNonPremult) {
    gfxUtils::PremultiplyDataSurface(resultSurf, resultSurf);
  }
  MOZ_ASSERT(resultSurf);

  if (frontTex->GetFlags() & TextureFlags::ORIGIN_BOTTOM_LEFT) {
    *out_origin = gl::OriginPos::TopLeft;
  } else {
    *out_origin = gl::OriginPos::TopLeft;
  }
  return resultSurf;
}

void
BasicCanvasLayer::Paint(DrawTarget* aDT,
                        const Point& aDeviceOffset,
                        Layer* aMaskLayer)
{
  if (IsHidden())
    return;

  RefPtr<SourceSurface> surface;
  auto surfOrigin = gl::OriginPos::TopLeft;
  if (IsDirty()) {
    Painted();

    FirePreTransactionCallback();
    surface = UpdateSurface(&surfOrigin);
    FireDidTransactionCallback();
  }

  bool bufferPoviderSnapshot = false;
  if (!surface && mBufferProvider) {
    surface = mBufferProvider->BorrowSnapshot();
    surfOrigin = gl::OriginPos::TopLeft;
    bufferPoviderSnapshot = !!surface;
  }

  if (!surface) {
    return;
  }

  const bool needsYFlip = (surfOrigin != gl::OriginPos::TopLeft);

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
