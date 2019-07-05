/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "GLContext.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/dom/BindingUtils.h"
#include "WebGLContext.h"
#include "WebGLQuery.h"

namespace mozilla {

WebGLExtensionDisjointTimerQuery::WebGLExtensionDisjointTimerQuery(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

WebGLExtensionDisjointTimerQuery::~WebGLExtensionDisjointTimerQuery() {}

void WebGLContext::QueryCounter(WebGLQuery& query, const GLenum target) const {
  const WebGLContext::FuncScope funcScope(*this, "queryCounterEXT");
  if (IsContextLost()) return;

  if (!ValidateObject("query", query)) return;

  query.QueryCounter(target);
}

bool WebGLExtensionDisjointTimerQuery::IsSupported(
    const WebGLContext* const webgl) {
  if (!gfxPrefs::WebGLPrivilegedExtensionsEnabled()) return false;

  gl::GLContext* gl = webgl->GL();
  return gl->IsSupported(gl::GLFeature::query_objects) &&
         gl->IsSupported(gl::GLFeature::get_query_object_i64v) &&
         gl->IsSupported(
             gl::GLFeature::query_counter);  // provides GL_TIMESTAMP
}

}  // namespace mozilla
