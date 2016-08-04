/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_TRANSFORM_FEEDBACK_H_
#define WEBGL_TRANSFORM_FEEDBACK_H_

#include "nsWrapperCache.h"
#include "WebGLObjectModel.h"

namespace mozilla {

class WebGLTransformFeedback final
    : public nsWrapperCache
    , public WebGLRefCountedObject<WebGLTransformFeedback>
{
    friend class WebGLContext;
    friend class WebGL2Context;

public:
    WebGLTransformFeedback(WebGLContext* webgl, GLuint tf);

    void Delete();
    virtual JSObject* WrapObject(JSContext* cx, JS::Handle<JSObject*> givenProto) override;

    NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLTransformFeedback)
    NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WebGLTransformFeedback)

private:
    ~WebGLTransformFeedback() {
        DetachOnce();
    }

    const GLuint mGLName;
    GLenum mMode;
    bool mIsActive;
    bool mIsPaused;
};

} // namespace mozilla

#endif // WEBGL_TRANSFORM_FEEDBACK_H_
