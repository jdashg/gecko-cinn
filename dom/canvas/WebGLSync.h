/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_SYNC_H_
#define WEBGL_SYNC_H_

#include "nsWrapperCache.h"
#include "WebGLObjectModel.h"

namespace mozilla {

class WebGLSync final
    : public nsWrapperCache
    , public WebGLRefCountedObject<WebGLSync>
{
    friend class WebGL2Context;

public:
    const GLsync mGLName;

    WebGLSync(WebGLContext* webgl, GLenum condition, GLbitfield flags);

    void Delete();

    virtual JSObject* WrapObject(JSContext* cx, JS::Handle<JSObject*> givenProto) override;

    NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLSync)
    NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WebGLSync)

private:
    ~WebGLSync() {
        DetachOnce();
    }
};

} // namespace mozilla

#endif // WEBGL_SYNC_H_
