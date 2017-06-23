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

void
BasicCanvasLayer::Paint(DrawTarget* const aDT, const Point& aDeviceOffset,
                        Layer* const aMaskLayer)
{
  const auto& frame = GetFrameForRedraw();
  if (!frame)
    return;

  DrawTo(frame, aDT, aDeviceOffset, aMaskLayer);
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
