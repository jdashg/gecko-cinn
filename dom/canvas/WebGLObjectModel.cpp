/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLObjectModel.h"

#include "WebGLContext.h"

namespace mozilla {

WebGLContextBoundObject::WebGLContextBoundObject(WebGLContext* webgl, bool isPermanent)
    : mMutableContext(webgl)
    , mSet(isPermanent ? webgl->mPermanentObjects
                       : webgl->mGenerationObjects)
    , mContext(mMutableContext) // By ref.
{
    MOZ_ASSERT(mContext);

    MOZ_ALWAYS_TRUE( mSet.insert(this).second );
}

void
WebGLContextBoundObject::Detach()
{
    MOZ_ASSERT(mContext);

    OnDetach();

    MOZ_ALWAYS_TRUE( mSet.erase(this) == 1 );

    mMutableContext = nullptr;
    MOZ_ASSERT(!mContext);
}

} // namespace mozilla
