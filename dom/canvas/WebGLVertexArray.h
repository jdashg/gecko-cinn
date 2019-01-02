/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_VERTEX_ARRAY_H_
#define WEBGL_VERTEX_ARRAY_H_

#include "nsTArray.h"
#include "mozilla/LinkedList.h"
#include "nsWrapperCache.h"

#include "CacheInvalidator.h"
#include "WebGLObjectModel.h"
#include "WebGLStrongTypes.h"
#include "WebGLVertexAttribData.h"

namespace mozilla {

class VertexArrayGL final : public AVertexArray,
                      public CacheInvalidator {
  friend class ContextGL;

 private:
  const GLuint mGLName;

  std::vector<WebGLVertexAttribData> mAttribs;
  RefPtr<BufferGL> mIndexBuffer;

public:
  explicit VertexArrayGL(ContextGL*);
  virtual ~VertexArrayGL();

  void Bind(VertexArrayGL* prev) const;
};

}  // namespace mozilla

#endif  // WEBGL_VERTEX_ARRAY_H_
