/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGL2Context.h"
#include "WebGLProgram.h"
#include "WebGLTransformFeedback.h"
#include "GLContext.h"

namespace mozilla {

// -------------------------------------------------------------------------
// Transform Feedback

already_AddRefed<WebGLTransformFeedback>
WebGL2Context::CreateTransformFeedback() {
  const FuncScope funcScope(*this, "createTransformFeedback");
  if (IsContextLost()) return nullptr;

  GLuint tf = 0;
  gl->fGenTransformFeedbacks(1, &tf);

  RefPtr<WebGLTransformFeedback> ret = new WebGLTransformFeedback(this, tf);
  return ret.forget();
}

void WebGL2Context::DeleteTransformFeedback(WebGLTransformFeedback* tf) {
  const FuncScope funcScope(*this, "deleteTransformFeedback");
  if (!ValidateDeleteObject(tf)) return;

  if (tf->mIsActive) {
    ErrorInvalidOperation("Cannot delete active transform feedbacks.");
    return;
  }

  if (mBoundTransformFeedback == tf) {
    BindTransformFeedback(nullptr);
  }

  tf->RequestDelete();
}

void WebGL2Context::BindTransformFeedback(WebGLTransformFeedback* tf) {
  const FuncScope funcScope(*this, "bindTransformFeedback");
  if (IsContextLost()) return;
  webgl::ScopedBindFailureGuard guard(*this);

  if (tf && !ValidateObject("tf", *tf)) return;

  if (mBoundTransformFeedback->mIsActive &&
      !mBoundTransformFeedback->mIsPaused) {
    ErrorInvalidOperation(
        "Currently bound transform feedback is active and not"
        " paused.");
    return;
  }

  ////

  mBoundTransformFeedback = (tf ? tf : mDefaultTransformFeedback);

  gl->fBindTransformFeedback(LOCAL_GL_TRANSFORM_FEEDBACK, mBoundTransformFeedback->mGLName);

  if (mBoundTransformFeedback) {
    mBoundTransformFeedback->mHasBeenBound = true;
  }

  guard.OnSuccess();
}

void WebGL2Context::BeginTransformFeedback(GLenum primMode) {
  const FuncScope funcScope(*this, "beginTransformFeedback");
  if (IsContextLost()) return;

  mBoundTransformFeedback->BeginTransformFeedback(primMode);
}

void WebGL2Context::EndTransformFeedback() {
  const FuncScope funcScope(*this, "endTransformFeedback");
  if (IsContextLost()) return;

  mBoundTransformFeedback->EndTransformFeedback();
}

void WebGL2Context::PauseTransformFeedback() {
  const FuncScope funcScope(*this, "pauseTransformFeedback");
  if (IsContextLost()) return;

  mBoundTransformFeedback->PauseTransformFeedback();
}

void WebGL2Context::ResumeTransformFeedback() {
  const FuncScope funcScope(*this, "resumeTransformFeedback");
  if (IsContextLost()) return;

  mBoundTransformFeedback->ResumeTransformFeedback();
}

void WebGL2Context::TransformFeedbackVaryings(
    WebGLProgram& program, const std::vector<std::string>& varyings,
    GLenum bufferMode) const {
  const FuncScope funcScope(*this, "transformFeedbackVaryings");
  if (IsContextLost()) return;

  if (!ValidateObject("program", program)) return;

  program.TransformFeedbackVaryings(varyings, bufferMode);
}

}  // namespace mozilla
