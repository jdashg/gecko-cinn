/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "WebGLContext.h"

namespace mozilla {

already_AddRefed<WebGLVertexArray>
WebGLExtensionVertexArray::CreateVertexArrayOES()
{
    if (!mContext)
        return nullptr;

    return mContext->CreateVertexArray();
}

void
WebGLExtensionVertexArray::DeleteVertexArrayOES(WebGLVertexArray* array)
{
    if (!mContext)
        return;

    mContext->DeleteVertexArray(array);
}

bool
WebGLExtensionVertexArray::IsVertexArrayOES(WebGLVertexArray* array)
{
    if (!mContext)
        return false;

    return mContext->IsVertexArray(array);
}

void
WebGLExtensionVertexArray::BindVertexArrayOES(WebGLVertexArray* array)
{
    if (!mContext)
        return;

    mContext->BindVertexArray(array);
}

/*static*/ bool
WebGLExtensionVertexArray::IsSupported(const WebGLContext* webgl)
{
    if (webgl->IsWebGL2())
        return false;

    return true;
}

IMPL_WEBGL_EXTENSION_GOOP(WebGLExtensionVertexArray, OES_vertex_array_object)

} // namespace mozilla
