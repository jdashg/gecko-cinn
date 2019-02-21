/* -*- Mode: C++; tab-width: 13; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=13 sts=4 et sw=4 tw=90: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_CONTEXT_CORE_H
#define WEBGL_CONTEXT_CORE_H

#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mozilla {
namespace webgl {

class AObject : public VRefCounted
{
public:
    const AContext* mContext;

    AObject(const AContext* const context)
        : mContext(context)
    { }
};

// -------------------------------------

class ABuffer : public AObject
{
public:
    const bool mIsIndexBuffer;

    ABuffer(const AContext* const context, const bool isIndexBuffer)
        : AObject(context)
        , mIsIndexBuffer(isIndexBuffer)
    { }
    /// !usage => BufferSubData, else ignore dstByteOffset.
    virtual void BufferData(GLenum usage, uint64_t dstByteOffset, uint64_t srcDataLen, const uint8_t* srcData) = 0;

    virtual void CopyBufferSubData(uint64_t destOffset, const ABuffer& asrc, uint64_t srcOffset,
                                    uint64_t size) = 0;
    virtual void GetBufferSubData(uint64_t srcOffset, uint8_t* dest, uint64_t size) const = 0;
};

class ATransformFeedback : public AObject
{
public:
    explicit ATransformFeedback(const AContext* const context)
        : AObject(context)
    { }
};

class AVertexArray : public AObject
{
    RefPtr<ABuffer> mIndexBuffer;
public:
    explicit AVertexArray(const AContext* const context)
        : AObject(context)
    { }
};

// -------------------------------------

class AFramebuffer : public AObject
{
public:
    explicit AFramebuffer(const AContext* const context)
        : AObject(context)
    { }

    virtual void FramebufferAttachment(GLenum attachment, RenderbufferGL*
                                       TextureGL*, uint8_t mip, uint32_t z);
};

class ARenderbuffer : public AObject
{
public:
    explicit ARenderbuffer(const AContext* const context)
        : AObject(context)
    { }

    virtual void RenderbufferStorage(uint8_t samples, GLenum internalFormat,
                                     uint32_t width, uint32_t height) = 0;
};

struct uvec2 final {
    uint32_t x = 0;
    uint32_t y = 0;
};

struct uvec3 final {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
};

class ATexture : public AObject
{
public:
    explicit ATexture(const AContext* const context)
        : AObject(context)
    { }

    virtual void GenerateMipmap(GLenum hint) = 0;
    virtual void TexStorage(GLenum target, uint32_t levels, GLenum internalFormat,
                            uvec3 size) = 0;

    /**
     * !internalFormat => *TexSubImage, offset ignored otherwise.
     * !unpackType => CompressedTex*Image
     */
    virtual void TexImage(GLenum target, uint32_t level, GLenum internalFormat,
                          uvec3 offset, uvec3 size,
                          GLenum unpackFormat, GLenum unpackType,
                          ABuffer*, const void* ptr, uint64_t dstDataLen) = 0;

    /**
     * !internalFormat => SubImage, destOffset ignored otherwise.
     */
    virtual void CopyTexImage(GLenum target, uint32_t level, GLenum internalFormat,
                          uvec3 destOffset, uvec2 srcOffset, uvec2 size) = 0;
};

// -------------------------------------

struct ShaderCompileInfo final
{
    bool pending = true;
    bool success = false;
};

class AShader : public AObject
{
public:
    const GLenum mType;

    AShader(const AContext* const context, const GLenum type)
        : AObject(context)
        , mType(type)
    { }

    ShaderCompileInfo GetCompileInfo() = 0;
};

// -

struct ProgramLinkInfo final
{
    bool pending = true;
    bool success = false;
};

class AProgram : public AObject
{
public:
    explicit AProgram(const AContext* const context)
        : AObject(context)
    { }

    virtual void BindAttribLocation(uint32_t index, const uint8_t* name,
                            uint64_t nameLen) = 0;
    virtual void LinkProgram(ShaderGL& vert, ShaderGL& frag) = 0;
    const ProgramLinkInfo& LinkInfo() = 0;
};

// -

class AContext : public VRefCounted
{
    // todo
};

// -------------------------------------

class ContextGL final : public AContext
{
    RefPtr<AFramebuffer> mFbo;
    RefPtr<AVertexArray> mVao;
    RefPtr<AProgram> mProgram;

    typedef uint64_t HandleT;

    std::unordered_map<HandleT, RefPtr<AObject>> mObjByHandle;

public:
    void BlendEquationSeparate(GLenum rgb, GLenum a);
    void BlendFuncSeparate(GLenum srcRgb, GLenum dstRgb, GLenum srcA,
                                   GLenum dstA);
    void SetEnabled(GLenum cap, bool val);
    void StencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                     GLuint mask);
    void StencilMaskSeparate(GLenum face, GLuint mask);
    void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                                   GLenum dppass);

    struct CLEAR_DESC final {
        GLbitfield bits;
        float r;
        float g;
        float b;
        float a;
        float d;
        int32_t s;
    };
    void Clear(const CLEAR_DESC&);

    struct CLEAR_BUFFER_DESC final {
        webgl::AttribBaseType type;
        GLenum attachment;
        uint8_t data[sizeof(float)*4)];
    };
    void ClearBufferTv(const CLEAR_BUFFER_DESC&);

    void BindBufferRange(GLenum target, uint32_t index, BufferGL*, uint64_t offset,
                         uint64_t size);
    void BindDrawFramebuffer(AFramebuffer&);
    void BindVertexArray(AVertexArray&) override;
    void UseProgram(AProgram&);

    void UniformNTv(uint8_t N, webgl::AttribBaseType T, uint32_t index, uint64_t elemCount,
                     const uint8_t* bytes, uint64_t byteCount);
    void UniformMatrixAxBfv(uint8_t A, uint8_t B, uint32_t index, bool transpose,
                               uint64_t elemCount,
                               const uint8_t* bytes,  uint64_t byteCount);

    void SetEnabledVertexAttribArray(uint32_t index, bool val) override;

    struct VERTEX_ATTRIB_DESC final {
        uint32_t index;
        webgl::AttribBaseType type;
        uint8_t data[sizeof(float)*4)];
    };
    void VertexAttrib4v(const VERTEX_ATTRIB_DESC&) override;

    virtual void VertexAttribPointer(uint32_t index,
                             uint8_t channels, GLenum type, bool normalized,
                             uint8_t stride, uint64_t byteOffset, ABuffer*) = 0;

private:
    RefPtr<ABuffer> CreateBuffer(bool isIndexBuffer) override {
        return new BufferGL(this, isIndexBuffer);
    }

    // -

    RefPtr<AVertexArray> CreateVertexArray() override {
        return new VertexArrayGL(this);
    }

    // -

    RefPtr<FramebufferGL> CreateFramebuffer();
    void ReadPixels(uint8_t readBuffer, uvec2 offset, uvec2 size,
                    GLenum format, GLenum type, ABuffer*,
                    void* dstData, uint64_t dstDataLen);

    struct DRAW_ARRAYS_DESC final {
        GLenum mode;
        uint32_t first;
        uint32_t vertCount;
        uint32_t instanceCount;
        uint32_t drawBuffersBits;
    };
    void DrawArrays(const DRAW_ARRAYS_DESC&);
    void DrawElements(GLenum mode, uint32_t indexCount, GLenum type, uint64_t byteOffset,
                      uint32_t instanceCount, uint32_t drawBuffersBits);


    RefPtr<RenderbufferGL> CreateRenderbuffer();
    RefPtr<TextureGL> CreateTexture();


    RefPtr<ATransformFeedback> CreateTransformFeedback();

    RefPtr<ShaderGL> CompileShader(GLenum target, const uint8_t* source,
                                   uint64_t sourceLen);
    RefPtr<ProgramGL> CreateProgram();

    // Object getters are client-only, but other pnames are generally all returning some
    // 32-bit type. We could even represent this as a double if we want. It's all Number
    // to JS!
};

class CommandBufferView final {
public:
    uint8_t* const mBegin;
    uint8_t* const mEnd;
private:
    uint8_t* mItr = nullptr;

public:
    CommandBufferView(uint8_t* const begin, uint8_t* const end)
        : mBegin(begin)
        , mEnd(end)
        , mItr(mBegin)
    { }


};

template<typename T>
struct Dispatchable {
    Dispatchable() = 0;
    static uint64_t Size();
    static void

} // namespace webgl
} // namespace mozilla

#endif // WEBGL_CONTEXT_CORE_H
