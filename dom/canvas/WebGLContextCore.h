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

class ABuffer : public AObject
{
public:
    const bool mIsIndexBuffer;

    ABuffer(const AContext* const context, const bool isIndexBuffer)
        : AObject(context)
        , mIsIndexBuffer(isIndexBuffer)
    { }

    virtual void BufferData(GLenum usage, uint64_t srcDataLen, const uint8_t* srcData) = 0;
    virtual void BufferSubData(uint64_t dstByteOffset, uint64_t srcDataLen, const uint8_t* srcData) = 0;
    virtual void CopyBufferSubData(uint64_t destOffset, const ABuffer& asrc, uint64_t srcOffset,
                                    uint64_t size) = 0;
    virtual void GetBufferSubData(uint64_t srcOffset, uint8_t* dest, uint64_t size) const = 0;
};

class AVertexArray : public AObject
{
public:
    explicit AVertexArray(const AContext* const context)
        : AObject(context)
    { }
};

class AShader : public AObject
{
public:
    const GLenum mType;

    AShader(const AContext* const context, const GLenum type)
        : AObject(context)
        , mType(type)
    { }
};

class AProgram : public AObject
{
public:
    explicit AProgram(const AContext* const context)
        : AObject(context)
    { }
};

// -

class AContext : public VRefCounted
{
    virtual RefPtr<ABuffer> CreateBuffer(GLenum target) = 0;
    virtual void BufferData(ABuffer&, GLenum target, GLenum usage, uint64_t srcDataLen,
                                 const uint8_t* srcData) = 0;
    virtual void BufferSubData(ABuffer&, GLenum target, uint64_t dstByteOffset,
                               uint64_t srcDataLen, const uint8_t* srcData) = 0;


    virtual RefPtr<AVertexArray> CreateVertexArray() = 0;
    virtual void BindVertexArray(AVertexArray& obj) = 0;
    virtual void VertexAttribPointer(bool isFuncInt, uint32_t index,
                             uint8_t channels, GLenum type, bool normalized,
                             uint8_t stride, uint64_t byteOffset, ABuffer*) = 0;

    virtual void SetEnabledVertexAttribArray(uint32_t index, bool val) = 0;
    virtual void VertexAttrib4v(GLuint index, webgl::AttribBaseType type,
                           const uint8_t* data) = 0;
};

class ContextDispatched final : public VRefCounted
{

};

// -

class ContextGL final : public AContext
{
    RefPtr<AFramebuffer> mDrawFbo;
    RefPtr<AVertexArray> mVao;
    RefPtr<AProgram> mProgram;

public:
    void BlendEquationSeparate(GLenum rgb, GLenum a);
    void BlendFuncSeparate(GLenum srcRgb, GLenum dstRgb, GLenum srcA,
                                   GLenum dstA);
    void PixelStorei(GLenum pname, uint32_t val);
    void SetEnabled(GLenum cap, bool val);
    void StencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                     GLuint mask);
    void StencilMaskSeparate(GLenum face, GLuint mask);
    void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                                   GLenum dppass);


    void BindBufferRange(GLenum target, uint32_t index, BufferGL*, uint64_t offset,
                         uint64_t size);
    void BindDrawFramebuffer(AFramebuffer&);
    void BindVertexArray(AVertexArray&) override;
    void UseProgram(AProgram&);

    void SetEnabledVertexAttribArray(uint32_t index, bool val) override;
    void VertexAttrib4v(uint32_t index, webgl::AttribBaseType type, const uint8_t* data) override;

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
    void FramebufferAttachment(FramebufferGL&, GLenum attachment, RenderbufferGL*
                                       TextureGL*, uint8_t mip, uint32_t z);
    void ReadPixels(AFramebuffer*, uint8_t readBuffer, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height, GLenum format, GLenum type, ABuffer*,
                    uint8_t* dstData, uint64_t dstDataLen);


    void DrawArrays(GLenum mode, uint32_t first, uint32_t vertCount,
                    uint32_t instanceCount, uint32_t drawBuffers);
    void DrawElements(GLenum mode, uint32_t indexCount, GLenum type, uint64_t byteOffset,
                      uint32_t instanceCount, uint32_t drawBuffers);


    RefPtr<RenderbufferGL> CreateRenderbuffer();
    void RenderbufferStorageMultisample(RenderbufferGL&, uint8_t samples,
                                                GLenum internalFormat, uint32_t width,
                                                uint32_t height);


    void UniformNTv(uint8_t N, GLenum T, AUniformLocation&, const uint8_t* data,
                            uint64_t elemCount);
    void UniformMatrixAxBfv(uint8_t A, uint8_t B, AUniformLocation&,
                                    bool transpose, const uint8_t* data,
                                    uint64_t elemCount);


    RefPtr<ATransformFeedback> CreateTransformFeedback();
    // Condensed Bind/Begin/Pause/Resume/End:
    // 'Used' always means active and not paused, never bound otherwise.
    void ResumeTransformFeedback(ATransformFeedback*, GLenum primMode);
    void PauseTransformFeedback();


    RefPtr<TextureGL> CreateTexture();
    void GenerateMipmap(ATexture&, GLenum hint);


    RefPtr<ShaderGL> CompileShader(GLenum target, const uint8_t* source,
                                   uint64_t sourceLen);
    RefPtr<ProgramGL> CreateProgram();
    void BindAttribLocation(ProgramGL&, uint32_t index, const uint8_t* name,
                            uint64_t nameLen);
    void LinkProgram(ProgramGL&, ShaderGL& vert, ShaderGL& frag);

    // Object getters are client-only, but other pnames are generally all returning some
    // 32-bit type. We could even represent this as a double if we want. It's all Number
    // to JS!
};

class BufferDispatch : public ABuffer {
    ContextDispatch& context;

public:
    BufferDispatch(ContextDispatch& context)
        : context(context)
    {}

    auto AsDispatch() override { return this; }
};

class ContextDispatch : public AContext
{
    RefPtr<ABuffer> CreateBuffer() override {
        return new BufferDispatch(this);
    }

    //void BufferData(BufferGL&, GLenum target, GLenum usage, uint64_t srcDataLen, const uint8_t* srcData);
};



} // namespace webgl
} // namespace mozilla

#endif // WEBGL_CONTEXT_CORE_H
