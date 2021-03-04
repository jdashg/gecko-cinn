/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawTargetWebgl.h"

#include "mozilla/gfx/DrawTargetSkia.h"
#include "WebGLContext.h"

namespace mozilla::gfx {

DrawTargetWebgl::DrawTargetWebgl() {
  mSkia = new DrawTargetSkia;

  webgl::InitContextDesc desc = {};
  desc.isWebgl2 = true;
  desc.size = {1,1};
  desc.options.alpha = true;
  desc.options.depth = true;
  desc.options.stencil = true;
  desc.options.antialias = true;
  desc.options.preserveDrawingBuffer = true;
  desc.options.failIfMajorPerformanceCaveat = true;

  webgl::InitContextResult res = {};
  mWebgl = WebGLContext::Create(nullptr, desc, &res);
  (void)res;
  if (!mWebgl) return;

  if (kMaxSurfaceSize > mWebgl->Limits().maxTex2dSize) {
    mWebgl = nullptr;
    return;
  }
}

DrawTargetWebgl::~DrawTargetWebgl() = default;

bool DrawTargetWebgl::Init(const IntSize& size, const SurfaceFormat format) {
  if (!mSkia->Init(size, format)) return false;
  return true;
}

}  // namespace mozilla::gfx
