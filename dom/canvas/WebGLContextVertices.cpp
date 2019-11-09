/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"

#include "GLContext.h"
#include "mozilla/Casting.h"
#include "mozilla/CheckedInt.h"
#include "WebGLBuffer.h"
#include "WebGLFramebuffer.h"
#include "WebGLProgram.h"
#include "WebGLRenderbuffer.h"
#include "WebGLShader.h"
#include "WebGLTexture.h"
#include "WebGLTypes.h"
#include "WebGLVertexArray.h"
#include "WebGLVertexAttribData.h"

#include "mozilla/Casting.h"

namespace mozilla {

static bool ValidateAttribIndex(WebGLContext& webgl, GLuint index) {
  bool valid = (index < webgl.MaxVertexAttribs());

  if (!valid) {
    if (index == GLuint(-1)) {
      webgl.ErrorInvalidValue(
          "-1 is not a valid `index`. This value"
          " probably comes from a getAttribLocation()"
          " call, where this return value -1 means"
          " that the passed name didn't correspond to"
          " an active attribute in the specified"
          " program.");
    } else {
      webgl.ErrorInvalidValue(
          "`index` must be less than"
          " MAX_VERTEX_ATTRIBS.");
    }
  }

  return valid;
}

////////////////////////////////////////

void WebGLContext::VertexAttrib4T(GLuint index, const webgl::TypedQuad& src) {
  const FuncScope funcScope(*this, "vertexAttrib[1234]u?[fi]v?");
  if (IsContextLost()) return;

  if (!ValidateAttribIndex(*this, index)) return;

  ////

  if (index || !gl->IsCompatibilityProfile()) {
    switch (src.type) {
      case webgl::AttribBaseType::Boolean:
      case webgl::AttribBaseType::Float:
        gl->fVertexAttrib4fv(index, reinterpret_cast<const float*>(src.data));
        break;
      case webgl::AttribBaseType::Int:
        gl->fVertexAttribI4iv(index, reinterpret_cast<const int32_t*>(src.data));
        break;
      case webgl::AttribBaseType::Uint:
        gl->fVertexAttribI4uiv(index, reinterpret_cast<const uint32_t*>(src.data));
        break;
    }
  }

  ////

  mGenericVertexAttribTypes[index] = webgl::AttribBaseType::Float;
  mGenericVertexAttribTypeInvalidator.InvalidateCaches();

  if (!index) {
    const float data[4] = {x, y, z, w};
    memcpy(mGenericVertexAttrib0Data, data, sizeof(data));
  }
}

////////////////////////////////////////

void WebGLContext::EnableVertexAttribArray(GLuint index) {
  const FuncScope funcScope(*this, "enableVertexAttribArray");
  if (IsContextLost()) return;

  if (!ValidateAttribIndex(*this, index)) return;

  gl->fEnableVertexAttribArray(index);

  MOZ_ASSERT(mBoundVertexArray);
  mBoundVertexArray->mAttribs[index].mEnabled = true;
  mBoundVertexArray->InvalidateCaches();
}

void WebGLContext::DisableVertexAttribArray(GLuint index) {
  const FuncScope funcScope(*this, "disableVertexAttribArray");
  if (IsContextLost()) return;

  if (!ValidateAttribIndex(*this, index)) return;

  if (index || !gl->IsCompatibilityProfile()) {
    gl->fDisableVertexAttribArray(index);
  }

  MOZ_ASSERT(mBoundVertexArray);
  mBoundVertexArray->mAttribs[index].mEnabled = false;
  mBoundVertexArray->InvalidateCaches();
}

Maybe<double> WebGLContext::GetVertexAttrib(GLuint index, GLenum pname) {
  const FuncScope funcScope(*this, "getVertexAttrib");
  if (IsContextLost()) return Nothing();

  if (!ValidateAttribIndex(*this, index)) return Nothing();

  MOZ_ASSERT(mBoundVertexArray);

  switch (pname) {
    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_STRIDE:
      return Some(
          static_cast<int32_t>(mBoundVertexArray->mAttribs[index].Stride()));

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_SIZE:
      return Some(
          static_cast<int32_t>(mBoundVertexArray->mAttribs[index].Size()));

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_TYPE:
      return Some(
          static_cast<int32_t>(mBoundVertexArray->mAttribs[index].Type()));

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_INTEGER:
      if (IsWebGL2())
        return Some(static_cast<bool>(
            mBoundVertexArray->mAttribs[index].IntegerFunc()));

      break;

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
      if (IsWebGL2() ||
          IsExtensionEnabled(WebGLExtensionID::ANGLE_instanced_arrays)) {
        return Some(
            static_cast<int32_t>(mBoundVertexArray->mAttribs[index].mDivisor));
      }
      break;
    }

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_ENABLED:
      return Some(mBoundVertexArray->mAttribs[index].mEnabled);

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
      return Some(mBoundVertexArray->mAttribs[index].Normalized());

    case LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER:
      return Some(mBoundVertexArray->mAttribs[index].ByteOffset());

    default:
      break;
  }

  ErrorInvalidEnumInfo("pname", pname);
  return Nothing();
}

////////////////////////////////////////

void WebGLContext::VertexAttribAnyPointer(bool isFuncInt, GLuint index,
                                          GLint size, GLenum type,
                                          bool normalized, GLsizei stride,
                                          WebGLintptr byteOffset) {
  if (IsContextLost()) return;

  if (!ValidateAttribIndex(*this, index)) return;

  ////

  if (size < 1 || size > 4) {
    ErrorInvalidValue("Invalid element size.");
    return;
  }

  // see WebGL spec section 6.6 "Vertex Attribute Data Stride"
  if (stride < 0 || stride > 255) {
    ErrorInvalidValue("Negative or too large stride.");
    return;
  }

  if (byteOffset < 0) {
    ErrorInvalidValue("Negative offset.");
    return;
  }

  ////

  bool isTypeValid = true;
  uint8_t typeAlignment;
  switch (type) {
    // WebGL 1:
    case LOCAL_GL_BYTE:
    case LOCAL_GL_UNSIGNED_BYTE:
      typeAlignment = 1;
      break;

    case LOCAL_GL_SHORT:
    case LOCAL_GL_UNSIGNED_SHORT:
      typeAlignment = 2;
      break;

    case LOCAL_GL_FLOAT:
      if (isFuncInt) {
        isTypeValid = false;
      }
      typeAlignment = 4;
      break;

    // WebGL 2:
    case LOCAL_GL_INT:
    case LOCAL_GL_UNSIGNED_INT:
      if (!IsWebGL2()) {
        isTypeValid = false;
      }
      typeAlignment = 4;
      break;

    case LOCAL_GL_HALF_FLOAT:
      if (isFuncInt || !IsWebGL2()) {
        isTypeValid = false;
      }
      typeAlignment = 2;
      break;

    case LOCAL_GL_FIXED:
      if (isFuncInt || !IsWebGL2()) {
        isTypeValid = false;
      }
      typeAlignment = 4;
      break;

    case LOCAL_GL_INT_2_10_10_10_REV:
    case LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV:
      if (isFuncInt || !IsWebGL2()) {
        isTypeValid = false;
        break;
      }
      if (size != 4) {
        ErrorInvalidOperation("Size must be 4 for this type.");
        return;
      }
      typeAlignment = 4;
      break;

    default:
      isTypeValid = false;
      break;
  }
  if (!isTypeValid) {
    ErrorInvalidEnumInfo("type", type);
    return;
  }

  ////

  // `alignment` should always be a power of two.
  MOZ_ASSERT(IsPowerOfTwo(typeAlignment));
  const GLsizei typeAlignmentMask = typeAlignment - 1;

  if (stride & typeAlignmentMask || byteOffset & typeAlignmentMask) {
    ErrorInvalidOperation(
        "`stride` and `byteOffset` must satisfy the alignment"
        " requirement of `type`.");
    return;
  }

  ////

  const auto& buffer = mBoundArrayBuffer;
  if (!buffer && byteOffset) {
    ErrorInvalidOperation("If ARRAY_BUFFER is null, byteOffset must be zero.");
    return;
  }

  ////

  WebGLVertexAttribData& vd = mBoundVertexArray->mAttribs[index];
  vd.VertexAttribPointer(isFuncInt, buffer, AutoAssertCast(size), type,
                         normalized, stride, byteOffset);
  vd.DoVertexAttribPointer(gl, index);
  mBoundVertexArray->InvalidateCaches();
}

////////////////////////////////////////

void WebGLContext::VertexAttribDivisor(GLuint index, GLuint divisor) {
  const FuncScope funcScope(*this, "vertexAttribDivisor");
  if (IsContextLost()) return;

  if (!ValidateAttribIndex(*this, index)) return;

  MOZ_ASSERT(mBoundVertexArray);
  mBoundVertexArray->mAttribs[index].mDivisor = divisor;
  mBoundVertexArray->InvalidateCaches();

  gl->fVertexAttribDivisor(index, divisor);
}

}  // namespace mozilla
