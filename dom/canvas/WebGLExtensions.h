/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_EXTENSIONS_H_
#define WEBGL_EXTENSIONS_H_

#include "mozilla/AlreadyAddRefed.h"
#include "nsWrapperCache.h"
#include "WebGLObjectModel.h"
#include "WebGLTypes.h"

namespace mozilla {

namespace dom {
template<typename T>
class Sequence;
} // namespace dom

namespace webgl {
class FormatUsageAuthority;
} // namespace webgl

class WebGLContext;
class WebGLShader;
class WebGLQuery;
class WebGLTimerQuery;
class WebGLVertexArray;

////////////////////////////////////////

class WebGLExtensionBase
    : public nsWrapperCache
    , public WebGLContextBoundObject
{
public:
    NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLExtensionBase)
    NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WebGLExtensionBase)

protected:
    explicit WebGLExtensionBase(WebGLContext* webgl)
        : WebGLContextBoundObject(webgl)
    { }

    virtual ~WebGLExtensionBase() { }

public:
    WebGLContext* GetParentObject() const { return mContext; }

private:
    virtual void DetachImpl() override { }
};

////////////////////////////////////////

#define DECL_WEBGL_EXTENSION_GOOP \
    virtual JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override; \
    static bool IsSupported(const WebGLContext*);

////////////////////////////////////////

class WebGLExtensionDebugShaders final
    : public WebGLExtensionBase
{
public:
    explicit WebGLExtensionDebugShaders(WebGLContext*);

    void GetTranslatedShaderSource(WebGLShader* shader, nsAString& retval);

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionLoseContext final
    : public WebGLExtensionBase
{
public:
    explicit WebGLExtensionLoseContext(WebGLContext*);

    void LoseContext();
    void RestoreContext();

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionTextureFloat final
    : public WebGLExtensionBase
{
public:
    static void InitWebGLFormats(webgl::FormatUsageAuthority* authority);

    explicit WebGLExtensionTextureFloat(WebGLContext*);

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionTextureHalfFloat final
    : public WebGLExtensionBase
{
public:
    static void InitWebGLFormats(webgl::FormatUsageAuthority* authority);

    explicit WebGLExtensionTextureHalfFloat(WebGLContext*);

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionDrawBuffers final
    : public WebGLExtensionBase
{
public:
    explicit WebGLExtensionDrawBuffers(WebGLContext*);

    void DrawBuffersWEBGL(const dom::Sequence<GLenum>& buffers);

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionVertexArray final
    : public WebGLExtensionBase
{
public:
    explicit WebGLExtensionVertexArray(WebGLContext* webgl);

    already_AddRefed<WebGLVertexArray> CreateVertexArrayOES();
    void DeleteVertexArrayOES(WebGLVertexArray* array);
    bool IsVertexArrayOES(WebGLVertexArray* array);
    void BindVertexArrayOES(WebGLVertexArray* array);

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionInstancedArrays final
    : public WebGLExtensionBase
{
public:
    explicit WebGLExtensionInstancedArrays(WebGLContext* webgl);

    void DrawArraysInstancedANGLE(GLenum mode, GLint first, GLsizei count,
                                  GLsizei primcount);
    void DrawElementsInstancedANGLE(GLenum mode, GLsizei count, GLenum type,
                                    WebGLintptr offset, GLsizei primcount);
    void VertexAttribDivisorANGLE(GLuint index, GLuint divisor);

    DECL_WEBGL_EXTENSION_GOOP
};

class WebGLExtensionDisjointTimerQuery final
    : public WebGLExtensionBase
{
    /**
     * An active TIME_ELAPSED query participating in a begin/end block.
     */
    WebGLRefPtr<WebGLTimerQuery> mActiveQuery;

public:
    explicit WebGLExtensionDisjointTimerQuery(WebGLContext* webgl);

    already_AddRefed<WebGLTimerQuery> CreateQueryEXT();
    void DeleteQueryEXT(WebGLTimerQuery* query);
    bool IsQueryEXT(WebGLTimerQuery* query);
    void BeginQueryEXT(GLenum target, WebGLTimerQuery* query);
    void EndQueryEXT(GLenum target);
    void QueryCounterEXT(WebGLTimerQuery* query, GLenum target);
    void GetQueryEXT(JSContext *cx, GLenum target, GLenum pname,
                     JS::MutableHandle<JS::Value> retval);
    void GetQueryObjectEXT(JSContext *cx, WebGLTimerQuery* query,
                           GLenum pname,
                           JS::MutableHandle<JS::Value> retval);

    DECL_WEBGL_EXTENSION_GOOP

private:
    virtual void DetachImpl() override;
};

////////////////////////////////////////

#define BASIC_EXT_DECL(T) \
    class T final : public WebGLExtensionBase \
    {                                         \
    public:                                   \
        explicit T(WebGLContext*);            \
                                              \
        DECL_WEBGL_EXTENSION_GOOP             \
    };

#define BASIC_EXT_DEFINE(T) \
    class T final : public WebGLExtensionBase \
    {                                         \
    public:                                   \
        explicit T(WebGLContext* webgl)       \
            : WebGLExtensionBase(webgl)       \
        {                                     \
            IsSupported(webgl);               \
        }                                     \
                                              \
        DECL_WEBGL_EXTENSION_GOOP             \
    };

BASIC_EXT_DEFINE(WebGLExtensionBlendMinMax)
BASIC_EXT_DECL(WebGLExtensionColorBufferFloat)
BASIC_EXT_DECL(WebGLExtensionColorBufferHalfFloat)
BASIC_EXT_DECL(WebGLExtensionCompressedTextureATC)
BASIC_EXT_DECL(WebGLExtensionCompressedTextureES3)
BASIC_EXT_DECL(WebGLExtensionCompressedTextureETC1)
BASIC_EXT_DECL(WebGLExtensionCompressedTexturePVRTC)
BASIC_EXT_DECL(WebGLExtensionCompressedTextureS3TC)
BASIC_EXT_DEFINE(WebGLExtensionDebugRendererInfo)
BASIC_EXT_DECL(WebGLExtensionDepthTexture)
BASIC_EXT_DEFINE(WebGLExtensionElementIndexUint)
BASIC_EXT_DECL(WebGLExtensionEXTColorBufferFloat)
BASIC_EXT_DEFINE(WebGLExtensionFragDepth)
BASIC_EXT_DEFINE(WebGLExtensionShaderTextureLod)
BASIC_EXT_DECL(WebGLExtensionSRGB)
BASIC_EXT_DEFINE(WebGLExtensionStandardDerivatives)
BASIC_EXT_DEFINE(WebGLExtensionTextureFilterAnisotropic)
BASIC_EXT_DECL(WebGLExtensionTextureFloatLinear)
BASIC_EXT_DECL(WebGLExtensionTextureHalfFloatLinear)

#undef BASIC_EXT_DECL
#undef BASIC_EXT_DEFINE
#undef DECL_WEBGL_EXTENSION_GOOP

////

#define IMPL_WEBGL_EXTENSION_GOOP(WebGLExtensionType, WebGLBindingType) \
    JSObject*                                                           \
    WebGLExtensionType::WrapObject(JSContext* cx, JS::Handle<JSObject*> givenProto) { \
        return dom::WebGLBindingType##Binding::Wrap(cx, this, givenProto); \
    }

} // namespace mozilla

#endif // WEBGL_EXTENSIONS_H_
