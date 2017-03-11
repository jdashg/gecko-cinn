/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLShader.h"

#include "angle/ShaderLang.h"
#include "GLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/SizePrintfMacros.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "prenv.h"
#include "WebGLContext.h"
#include "WebGLObjectModel.h"
#include "WebGLShaderValidator.h"
#include "WebGLValidateStrings.h"

namespace mozilla {

template<size_t N>
static bool
SubstringStartsWith(const std::string& testStr, size_t offset, const char (& refStr)[N])
{
    for (size_t i = 0; i < N-1; i++) {
        if (testStr[offset + i] != refStr[i])
            return false;
    }
    return true;
}

/* On success, writes to out_translatedSource.
 * On failure, writes to out_translationLog.
 *
 * Requirements:
 *   #version is either omitted, `#version 100`, or `version 300 es`.
 */
static bool
TranslateWithoutValidation(const nsACString& sourceNS, bool isWebGL2,
                           nsACString* const out_translationLog,
                           nsACString* const out_translatedSource)
{
    std::string source = sourceNS.BeginReading();

    size_t versionStrStart = source.find("#version");
    size_t versionStrLen;
    uint32_t glesslVersion;

    if (versionStrStart != std::string::npos) {
        static const char versionStr100[] = "#version 100\n";
        static const char versionStr300es[] = "#version 300 es\n";

        if (isWebGL2 && SubstringStartsWith(source, versionStrStart, versionStr300es)) {
            glesslVersion = 300;
            versionStrLen = strlen(versionStr300es);

        } else if (SubstringStartsWith(source, versionStrStart, versionStr100)) {
            glesslVersion = 100;
            versionStrLen = strlen(versionStr100);

        } else {
            nsPrintfCString error("#version, if declared, must be %s.",
                                  isWebGL2 ? "`100` or `300 es`"
                                           : "`100`");
            *out_translationLog = error;
            return false;
        }
    } else {
        versionStrStart = 0;
        versionStrLen = 0;
        glesslVersion = 100;
    }

    std::string reversionedSource = source;
    reversionedSource.erase(versionStrStart, versionStrLen);

    switch (glesslVersion) {
    case 100:
        /* According to ARB_ES2_compatibility extension glsl
         * should accept #version 100 for ES 2 shaders. */
        reversionedSource.insert(versionStrStart, "#version 100\n");
        break;
    case 300:
        reversionedSource.insert(versionStrStart, "#version 330\n");
        break;
    default:
        MOZ_CRASH("GFX: Bad `glesslVersion`.");
    }

    out_translatedSource->Assign(reversionedSource.c_str(),
                                 reversionedSource.length());
    return true;
}

static void
GetCompilationStatusAndLog(gl::GLContext* gl, GLuint shader, bool* const out_success,
                           nsACString* const out_log)
{
    GLint compileStatus = LOCAL_GL_FALSE;
    gl->fGetShaderiv(shader, LOCAL_GL_COMPILE_STATUS, &compileStatus);

    // It's simpler if we always get the log.
    GLint lenWithNull = 0;
    gl->fGetShaderiv(shader, LOCAL_GL_INFO_LOG_LENGTH, &lenWithNull);

    if (lenWithNull > 1) {
        // SetLength takes the length without the null.
        out_log->SetLength(lenWithNull - 1);
        gl->fGetShaderInfoLog(shader, lenWithNull, nullptr, out_log->BeginWriting());
    } else {
        out_log->SetLength(0);
    }

    *out_success = (compileStatus == LOCAL_GL_TRUE);
}

////////////////////////////////////////////////////////////////////////////////

static GLuint
CreateShader(gl::GLContext* gl, GLenum type)
{
    gl->MakeCurrent();
    return gl->fCreateShader(type);
}

WebGLShader::WebGLShader(WebGLContext* webgl, GLenum type)
    : WebGLRefCountedObject(webgl)
    , mGLName(CreateShader(webgl->GL(), type))
    , mType(type)
    , mTranslationSuccessful(false)
    , mCompilationSuccessful(false)
{
    mContext->mShaders.insertBack(this);
}

WebGLShader::~WebGLShader()
{
    DeleteOnce();
}

void
WebGLShader::ShaderSource(const nsAString& source)
{
    const char funcName[] = "shaderSource";
    nsString sourceWithoutComments;
    if (!TruncateComments(source, &sourceWithoutComments)) {
        mContext->ErrorOutOfMemory("%s: Failed to alloc for empting comment contents.",
                                   funcName);
        return;
    }

    if (!ValidateGLSLPreprocString(mContext, funcName, sourceWithoutComments))
        return;

    // We checked that the source stripped of comments is in the
    // 7-bit ASCII range, so we can skip the NS_IsAscii() check.
    const NS_LossyConvertUTF16toASCII cleanSource(sourceWithoutComments);

    if (PR_GetEnv("MOZ_WEBGL_DUMP_SHADERS")) {
        printf_stderr("////////////////////////////////////////\n");
        printf_stderr("// MOZ_WEBGL_DUMP_SHADERS:\n");

        // Wow - Roll Your Own Foreach-Lines because printf_stderr has a hard-coded
        // internal size, so long strings are truncated.

        const size_t maxChunkSize = 1024-1; // -1 for null-term.
        const UniqueBuffer buf(moz_xmalloc(maxChunkSize+1)); // +1 for null-term
        const auto bufBegin = (char*)buf.get();

        size_t chunkStart = 0;
        while (chunkStart != cleanSource.Length()) {
            const auto chunkEnd = std::min(chunkStart + maxChunkSize,
                                           size_t(cleanSource.Length()));
            const auto chunkSize = chunkEnd - chunkStart;

            memcpy(bufBegin, cleanSource.BeginReading() + chunkStart, chunkSize);
            bufBegin[chunkSize + 1] = '\0';

            printf_stderr("%s", bufBegin);
            chunkStart += chunkSize;
        }

        printf_stderr("////////////////////////////////////////\n");
    }

    mSource = source;
    mCleanSource = cleanSource;
}

void
WebGLShader::CompileShader()
{
    mCompileInfo = nullptr;
    mTranslationSuccessful = false;
    mCompilationSuccessful = false;

    gl::GLContext* gl = mContext->gl;

    const auto& shaderValidator = mContext->ShaderValidator();
    if (shaderValidator) {
        mCompileInfo = shaderValidator->Compile(mType, mCleanSource.BeginReading(),
                                                &mValidationLog);
        if (!mCompileInfo)
            return;
        mTranslatedSource = mCompileInfo->translatedSource.c_str();
    } else {
        if (!TranslateWithoutValidation(mCleanSource, mContext->IsWebGL2(),
                                        &mValidationLog, &mTranslatedSource))
        {
            return;
        }
    }
    mTranslationSuccessful = true;

    gl->MakeCurrent();

    const char* const parts[] = {
        mTranslatedSource.BeginReading()
    };
    gl->fShaderSource(mGLName, ArrayLength(parts), parts, nullptr);

    gl->fCompileShader(mGLName);

    GetCompilationStatusAndLog(gl, mGLName, &mCompilationSuccessful, &mCompilationLog);
}

void
WebGLShader::GetShaderInfoLog(nsAString* out) const
{
    const nsCString& log = !mTranslationSuccessful ? mValidationLog
                                                   : mCompilationLog;
    CopyASCIItoUTF16(log, *out);
}

JS::Value
WebGLShader::GetShaderParameter(GLenum pname) const
{
    switch (pname) {
    case LOCAL_GL_SHADER_TYPE:
        return JS::NumberValue(mType);

    case LOCAL_GL_DELETE_STATUS:
        return JS::BooleanValue(IsDeleteRequested());

    case LOCAL_GL_COMPILE_STATUS:
        return JS::BooleanValue(mCompilationSuccessful);

    default:
        mContext->ErrorInvalidEnumInfo("getShaderParameter: `pname`", pname);
        return JS::NullValue();
    }
}

void
WebGLShader::GetShaderSource(nsAString* out) const
{
    out->SetIsVoid(false);
    *out = mSource;
}

void
WebGLShader::GetShaderTranslatedSource(nsAString* out) const
{
    if (!mCompilationSuccessful) {
        mContext->ErrorInvalidOperation("getShaderTranslatedSource: Shader has"
                                        " not been successfully compiled.");
        return;
    }

    out->SetIsVoid(false);
    CopyASCIItoUTF16(mTranslatedSource, *out);
}

////////////////////////////////////////////////////////////////////////////////
// Boilerplate

JSObject*
WebGLShader::WrapObject(JSContext* js, JS::Handle<JSObject*> givenProto)
{
    return dom::WebGLShaderBinding::Wrap(js, this, givenProto);
}

size_t
WebGLShader::SizeOfIncludingThis(MallocSizeOf mallocSizeOf) const
{
    return mallocSizeOf(this) +
           mSource.SizeOfExcludingThisIfUnshared(mallocSizeOf) +
           mCleanSource.SizeOfExcludingThisIfUnshared(mallocSizeOf) +
           (mCompileInfo ? mCompileInfo->MemSize() : 0) +
           mValidationLog.SizeOfExcludingThisIfUnshared(mallocSizeOf) +
           mTranslatedSource.SizeOfExcludingThisIfUnshared(mallocSizeOf) +
           mCompilationLog.SizeOfExcludingThisIfUnshared(mallocSizeOf);
}

void
WebGLShader::Delete()
{
    gl::GLContext* gl = mContext->GL();

    gl->MakeCurrent();
    gl->fDeleteShader(mGLName);

    LinkedListElement<WebGLShader>::removeFrom(mContext->mShaders);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLShader)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(WebGLShader, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(WebGLShader, Release)

} // namespace mozilla
