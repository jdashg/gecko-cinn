/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"

#include <queue>

#include "AccessCheck.h"
#include "gfxContext.h"
#include "gfxCrashReporterUtils.h"
#include "gfxPattern.h"
#include "gfxPrefs.h"
#include "gfxUtils.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "GLReadTexImageHelper.h"
#include "ImageContainer.h"
#include "ImageEncoder.h"
#include "Layers.h"
#include "LayerUserData.h"
#include "MozFramebuffer.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/dom/WebGLContextEvent.h"
#include "mozilla/EnumeratedArrayCycleCollection.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessPriorityManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/SizePrintfMacros.h"
#include "mozilla/Telemetry.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsIClassInfoImpl.h"
#include "nsIConsoleService.h"
#include "nsIDOMEvent.h"
#include "nsIGfxInfo.h"
#include "nsIObserverService.h"
#include "nsIVariant.h"
#include "nsIWidget.h"
#include "nsIXPConnect.h"
#include "nsServiceManagerUtils.h"
#include "nsSVGEffects.h"
#include "prenv.h"
#include "ScopedGLHelpers.h"
#include "SharedSurfaceGL.h"
#include "VRManagerChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"

#ifdef MOZ_WIDGET_GONK
#include "mozilla/layers/ShadowLayers.h"
#endif

// Local
#include "CanvasUtils.h"
#include "WebGL1Context.h"
#include "WebGLActiveInfo.h"
#include "WebGLBuffer.h"
#include "WebGLContextLossHandler.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"
#include "WebGLFramebuffer.h"
#include "WebGLMemoryTracker.h"
#include "WebGLObjectModel.h"
#include "WebGLProgram.h"
#include "WebGLQuery.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLSync.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"
#include "WebGLVertexAttribData.h"

#ifdef MOZ_WIDGET_COCOA
#include "nsCocoaFeatures.h"
#endif

#ifdef XP_WIN
#include "WGLLibrary.h"
#endif

// Generated
#include "mozilla/dom/WebGLRenderingContextBinding.h"


namespace mozilla {

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::gl;
using namespace mozilla::layers;

WebGLContextOptions::WebGLContextOptions()
    : alpha(true)
    , depth(true)
    , stencil(false)
    , premultipliedAlpha(true)
    , antialias(true)
    , preserveDrawingBuffer(false)
    , failIfMajorPerformanceCaveat(false)
{
    // Set default alpha state based on preference.
    if (gfxPrefs::WebGLDefaultNoAlpha())
        alpha = false;
}


/*static*/ const uint32_t WebGLContext::kMinMaxColorAttachments = 4;
/*static*/ const uint32_t WebGLContext::kMinMaxDrawBuffers = 4;

WebGLContext::WebGLContext()
    : WebGLContextUnchecked(nullptr)
    , mOptionsFrozen(false)
    , mOptions(mMutableOptions)
    , mMaxPerfWarnings(gfxPrefs::WebGLMaxPerfWarnings())
    , mNumPerfWarnings(0)
    , mMaxAcceptableFBStatusInvals(gfxPrefs::WebGLMaxAcceptableFBStatusInvals())
    , mBufferFetchingIsVerified(false)
    , mBufferFetchingHasPerVertex(false)
    , mMaxFetchedVertices(0)
    , mMaxFetchedInstances(0)
    , mLayerIsMirror(false)
    , mBypassShaderValidation(false)
    , mEmptyTFO(0)
    , mContextLossHandler(this)
    , mNeedsFakeNoAlpha(false)
    , mNeedsFakeNoDepth(false)
    , mNeedsFakeNoStencil(false)
    , mNeedsEmulatedLoneDepthStencil(false)
    , mAllowFBInvalidation(gfxPrefs::WebGLFBInvalidation())
    , mFrameID(0)
{
    mGeneration = 0;
    mInvalidated = false;
    mCapturedFrameInvalidated = false;
    mShouldPresent = true;
    mResetLayer = true;
    mMinCapability = false;
    mDisableExtensions = false;
    mIsMesa = false;
    mEmitContextLostErrorOnce = false;
    mWebGLError = 0;
    mUnderlyingGLError = 0;

    mActiveTexture = 0;

    mStencilRefFront = 0;
    mStencilRefBack = 0;
    mStencilValueMaskFront = 0;
    mStencilValueMaskBack = 0;
    mStencilWriteMaskFront = 0;
    mStencilWriteMaskBack = 0;
    mDepthWriteMask = 0;
    mStencilClearValue = 0;
    mDepthClearValue = 0;
    mContextLostErrorSet = false;

    mViewportX = 0;
    mViewportY = 0;
    mViewportWidth = 0;
    mViewportHeight = 0;

    mDitherEnabled = 1;
    mRasterizerDiscardEnabled = 0; // OpenGL ES 3.0 spec p244
    mScissorTestEnabled = 0;
    mDepthTestEnabled = 0;
    mStencilTestEnabled = 0;

    if (NS_IsMainThread()) {
        // XXX mtseng: bug 709490, not thread safe
        WebGLMemoryTracker::AddWebGLContext(this);
    }

    mAllowContextRestore = true;
    mLastLossWasSimulated = false;
    mContextStatus = ContextNotLost;
    mLoseContextOnMemoryPressure = false;
    mCanLoseContextInForeground = true;
    mRestoreWhenVisible = false;

    mAlreadyGeneratedWarnings = 0;
    mAlreadyWarnedAboutFakeVertexAttrib0 = false;
    mAlreadyWarnedAboutViewportLargerThanDest = false;

    mMaxWarnings = gfxPrefs::WebGLMaxWarningsPerContext();
    if (mMaxWarnings < -1) {
        GenerateWarning("webgl.max-warnings-per-context size is too large (seems like a negative value wrapped)");
        mMaxWarnings = 0;
    }

    mLastUseIndex = 0;

    InvalidateBufferFetching();

    mDisableFragHighP = false;

    mDrawCallsSinceLastFlush = 0;
}

WebGLContext::~WebGLContext()
{
    RemovePostRefreshObserver();

    DestroyResourcesAndContext();
    if (NS_IsMainThread()) {
        // XXX mtseng: bug 709490, not thread safe
        WebGLMemoryTracker::RemoveWebGLContext(this);
    }
}

template<typename T>
void
ClearLinkedList(LinkedList<T>& list)
{
    while (!list.isEmpty()) {
        list.getLast()->DeleteOnce();
    }
}

void
WebGLContext::DestroyResourcesAndContext()
{
    if (!gl)
        return;

    gl->MakeCurrent();

    mBound2DTextures.Clear();
    mBoundCubeMapTextures.Clear();
    mBound3DTextures.Clear();
    mBound2DArrayTextures.Clear();
    mBoundSamplers.Clear();
    mBoundArrayBuffer = nullptr;
    mBoundCopyReadBuffer = nullptr;
    mBoundCopyWriteBuffer = nullptr;
    mBoundPixelPackBuffer = nullptr;
    mBoundPixelUnpackBuffer = nullptr;
    mBoundUniformBuffer = nullptr;
    mCurrentProgram = nullptr;
    mActiveProgramLinkInfo = nullptr;
    mBoundDrawFramebuffer = nullptr;
    mBoundReadFramebuffer = nullptr;
    mBoundRenderbuffer = nullptr;
    mBoundVertexArray = nullptr;
    mDefaultVertexArray = nullptr;
    mBoundTransformFeedback = nullptr;
    mDefaultTransformFeedback = nullptr;

    mQuerySlot_SamplesPassed = nullptr;
    mQuerySlot_TFPrimsWritten = nullptr;
    mQuerySlot_TimeElapsed = nullptr;

    mIndexedUniformBufferBindings.clear();

    //////

    ClearLinkedList(mBuffers);
    ClearLinkedList(mFramebuffers);
    ClearLinkedList(mPrograms);
    ClearLinkedList(mQueries);
    ClearLinkedList(mRenderbuffers);
    ClearLinkedList(mSamplers);
    ClearLinkedList(mShaders);
    ClearLinkedList(mSyncs);
    ClearLinkedList(mTextures);
    ClearLinkedList(mTransformFeedbacks);
    ClearLinkedList(mVertexArrays);

    //////

    if (mEmptyTFO) {
        gl->fDeleteTransformFeedbacks(1, &mEmptyTFO);
        mEmptyTFO = 0;
    }

    //////

    mFakeBlack_2D_0000       = nullptr;
    mFakeBlack_2D_0001       = nullptr;
    mFakeBlack_CubeMap_0000  = nullptr;
    mFakeBlack_CubeMap_0001  = nullptr;
    mFakeBlack_3D_0000       = nullptr;
    mFakeBlack_3D_0001       = nullptr;
    mFakeBlack_2D_Array_0000 = nullptr;
    mFakeBlack_2D_Array_0001 = nullptr;

    if (mFakeVertexAttrib0BufferObject) {
        gl->fDeleteBuffers(1, &mFakeVertexAttrib0BufferObject);
        mFakeVertexAttrib0BufferObject = 0;
    }

    // disable all extensions except "WEBGL_lose_context". see bug #927969
    // spec: http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
    for (size_t i = 0; i < size_t(WebGLExtensionID::Max); ++i) {
        WebGLExtensionID extension = WebGLExtensionID(i);

        if (!IsExtensionEnabled(extension) || (extension == WebGLExtensionID::WEBGL_lose_context))
            continue;

        mExtensions[extension]->MarkLost();
        mExtensions[extension] = nullptr;
    }

    ////

    mAntialiasedFB = nullptr;
    mPreservedFB = nullptr;
    SetSharedFB(nullptr);
    mFrontBuffer = nullptr;
    mIndirectReadFB = nullptr;

    mSurfFactory.Reset(nullptr);

    ////

    if (GLContext::ShouldSpew()) {
        printf_stderr("--- WebGL context destroyed: %p\n", gl.get());
    }

    MOZ_ASSERT(gl);
    mGL_OnlyClearInDestroyResourcesAndContext = nullptr;
    MOZ_ASSERT(!gl);
}

void
WebGLContext::Invalidate()
{
    if (!mCanvasElement)
        return;

    mCapturedFrameInvalidated = true;

    if (mInvalidated)
        return;

    nsSVGEffects::InvalidateDirectRenderingObservers(mCanvasElement);

    mInvalidated = true;
    mCanvasElement->InvalidateCanvasContent(nullptr);
}

void
WebGLContext::OnVisibilityChange()
{
    if (!IsContextLost()) {
        return;
    }

    if (!mRestoreWhenVisible || mLastLossWasSimulated) {
        return;
    }

    ForceRestoreContext();
}

void
WebGLContext::OnMemoryPressure()
{
    bool shouldLoseContext = mLoseContextOnMemoryPressure;

    if (!mCanLoseContextInForeground &&
        ProcessPriorityManager::CurrentProcessIsForeground())
    {
        shouldLoseContext = false;
    }

    if (shouldLoseContext)
        ForceLoseContext();
}

//
// nsICanvasRenderingContextInternal
//

NS_IMETHODIMP
WebGLContext::SetContextOptions(JSContext* cx, JS::Handle<JS::Value> options,
                                ErrorResult& aRvForDictionaryInit)
{
    if (mOptionsFrozen) {
        if (options.isNullOrUndefined())
            return NS_OK;
        return NS_ERROR_FAILURE;
    }

    WebGLContextAttributes attributes;
    if (!attributes.Init(cx, options)) {
        aRvForDictionaryInit.Throw(NS_ERROR_UNEXPECTED);
        return NS_ERROR_UNEXPECTED;
    }

    mMutableOptions.stencil                      = attributes.mStencil;
    mMutableOptions.depth                        = attributes.mDepth;
    mMutableOptions.premultipliedAlpha           = attributes.mPremultipliedAlpha;
    mMutableOptions.antialias                    = attributes.mAntialias;
    mMutableOptions.preserveDrawingBuffer        = attributes.mPreserveDrawingBuffer;
    mMutableOptions.failIfMajorPerformanceCaveat = attributes.mFailIfMajorPerformanceCaveat;

    if (attributes.mAlpha.WasPassed()) {
        mMutableOptions.alpha = attributes.mAlpha.Value();
    }

    return NS_OK;
}

/* So there are a number of points of failure here. We might fail based
 * on EGL vs. WGL, or we might fail to alloc a too-large size, or we
 * might not be able to create a context with a certain combo of context
 * creation attribs.
 *
 * We don't want to test the complete fallback matrix. (for now, at
 * least) Instead, attempt creation in this order:
 * 1. By platform API. (e.g. EGL vs. WGL)
 * 2. By context creation attribs.
 * 3. By size.
 *
 * That is, try to create headless contexts based on the platform API.
 * Next, create dummy-sized backbuffers for the contexts with the right
 * caps. Finally, resize the backbuffer to an acceptable size given the
 * requested size.
 */

static bool
IsFeatureInBlacklist(const nsCOMPtr<nsIGfxInfo>& gfxInfo, int32_t feature,
                     nsCString* const out_blacklistId)
{
    int32_t status;
    if (!NS_SUCCEEDED(gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo, feature,
                                                           *out_blacklistId, &status)))
    {
        return false;
    }

    return status != nsIGfxInfo::FEATURE_STATUS_OK;
}

static bool
HasAcceleratedLayers(const nsCOMPtr<nsIGfxInfo>& gfxInfo)
{
    int32_t status;

    nsCString discardFailureId;
    gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                         nsIGfxInfo::FEATURE_DIRECT3D_9_LAYERS,
                                         discardFailureId,
                                         &status);
    if (status)
        return true;
    gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                         nsIGfxInfo::FEATURE_DIRECT3D_10_LAYERS,
                                         discardFailureId,
                                         &status);
    if (status)
        return true;
    gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                         nsIGfxInfo::FEATURE_DIRECT3D_10_1_LAYERS,
                                         discardFailureId,
                                         &status);
    if (status)
        return true;
    gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                         nsIGfxInfo::FEATURE_DIRECT3D_11_LAYERS,
                                         discardFailureId,
                                         &status);
    if (status)
        return true;
    gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                         nsIGfxInfo::FEATURE_OPENGL_LAYERS,
                                         discardFailureId,
                                         &status);
    if (status)
        return true;

    return false;
}

////////////////////////////////////////

static RefPtr<gl::GLContext>
CreateGLWithEGL(WebGLContext* const webgl, const gl::CreateContextFlags flags,
                std::vector<WebGLContext::FailureReason>* const out_failReasons)
{
    nsCString failureId;
    RefPtr<GLContext> gl = gl::GLContextProviderEGL::CreateHeadless(flags, &failureId);
    if (gl && gl->IsANGLE()) {
        gl = nullptr;
    }

    if (!gl) {
        out_failReasons->push_back(WebGLContext::FailureReason(
            failureId,
            "Error during EGL OpenGL init."
        ));
        return nullptr;
    }

    return gl;
}

static RefPtr<GLContext>
CreateGLWithANGLE(WebGLContext* const webgl, const gl::CreateContextFlags flags,
                  std::vector<WebGLContext::FailureReason>* const out_failReasons)
{
    nsCString failureId;
    RefPtr<GLContext> gl = gl::GLContextProviderEGL::CreateHeadless(flags, &failureId);
    if (gl && !gl->IsANGLE()) {
        gl = nullptr;
    }

    if (!gl) {
        out_failReasons->push_back(WebGLContext::FailureReason(
            failureId,
            "Error during ANGLE OpenGL init."
        ));
        return nullptr;
    }

    return gl;
}

static RefPtr<gl::GLContext>
CreateGLWithDefault(WebGLContext* const webgl, const gl::CreateContextFlags flags,
                    std::vector<WebGLContext::FailureReason>* const out_failReasons)
{
    nsCString failureId;
    RefPtr<GLContext> gl = gl::GLContextProvider::CreateHeadless(flags, &failureId);
    if (gl && gl->IsANGLE()) {
        gl = nullptr;
    }

    if (!gl) {
        out_failReasons->push_back(WebGLContext::FailureReason(
            failureId,
            "Error during native OpenGL init."
        ));
        return nullptr;
    }

    return gl;
}

////////////////////////////////////////

bool
WebGLContext::CreateAndInitGLWith(FnCreateGL_T* const fnCreateGL,
                                  const gl::CreateContextFlags flags,
                                  std::vector<FailureReason>* const out_failReasons)
{
    MOZ_ASSERT(!(flags & CreateContextFlags::DEPTH_STENCIL_CONFIG));

    RefPtr<gl::GLContext> newGL;
    do {
        uint32_t requestedSamples = gfxPrefs::MSAALevel();
        if (!requestedSamples) {
            mMutableOptions.antialias = false;
        }
        if (mMutableOptions.antialias) {
            newGL = fnCreateGL(this, flags, out_failReasons);
            if (newGL) {
                const auto maxSamples = newGL->GetIntAs<uint32_t>(LOCAL_GL_MAX_SAMPLES);
                if (requestedSamples > maxSamples) {
                    requestedSamples = maxSamples;
                }
                if (requestedSamples &&
                    bool(gl::MozFramebuffer::Create(newGL, gfx::IntSize(1, 1),
                                                    requestedSamples, false)))
                {
                    mAntialiasSamples = requestedSamples;
                    break;
                }
            }

            mMutableOptions.antialias = false;
        }
        mAntialiasSamples = 0;

        const bool frontbufferDepthStencil = mOptions.FrontbufferHasDepthStencil();
        if (newGL &&
            frontbufferDepthStencil &&
            !newGL->IsConfigDepthStencilFlexible())
        {
            newGL = nullptr;
        }
        if (!newGL) {
            auto frontbufferFlags = flags;
            if (frontbufferDepthStencil) {
                frontbufferFlags |= gl::CreateContextFlags::DEPTH_STENCIL_CONFIG;
            }
            newGL = fnCreateGL(this, frontbufferFlags, out_failReasons);
        }
        if (newGL)
            break;

        out_failReasons->push_back(FailureReason("FEATURE_FAILURE_WEBGL_EXHAUSTED_CAPS",
                                                 "Exhausted GL driver caps."));
        return false;
    } while (false);
    MOZ_ASSERT(newGL);

    mGL_OnlyClearInDestroyResourcesAndContext = newGL;
    MOZ_ASSERT(gl);

    FailureReason reason;
    if (InitAndValidateGL(&reason)) {
        mOptionsFrozen = true;
        return true;
    }

    DestroyResourcesAndContext();
    MOZ_ASSERT(!gl);

    // The fail reason here should be specific enough for now.
    out_failReasons->push_back(reason);
    return false;
}

bool
WebGLContext::CreateAndInitGL(bool forceEnabled,
                              std::vector<FailureReason>* const out_failReasons)
{
    // WebGL2 is separately blocked:
    if (IsWebGL2()) {
        const nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
        const auto feature = nsIGfxInfo::FEATURE_WEBGL2;

        FailureReason reason;
        if (IsFeatureInBlacklist(gfxInfo, feature, &reason.key)) {
            reason.info = "Refused to create WebGL2 context because of blacklist"
                          " entry: ";
            reason.info.Append(reason.key);
            out_failReasons->push_back(reason);
            GenerateWarning("%s", reason.info.BeginReading());
            return false;
        }
    }

    if (mOptions.antialias && !gfxPrefs::WebGLForceMSAA()) {
        const nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();

        nsCString blocklistId;
        if (IsFeatureInBlacklist(gfxInfo, nsIGfxInfo::FEATURE_WEBGL_MSAA, &blocklistId)) {
            GenerateWarning("Disallowing antialiased backbuffers due to blacklisting.");
            mMutableOptions.antialias = false;
        }
    }

    gl::CreateContextFlags flags = (gl::CreateContextFlags::NO_VALIDATION |
                                    gl::CreateContextFlags::PREFER_ROBUSTNESS);
    bool tryNativeGL = true;
    bool tryANGLE = false;

    if (forceEnabled) {
        flags |= gl::CreateContextFlags::FORCE_ENABLE_HARDWARE;
    }

    if (IsWebGL2()) {
        flags |= gl::CreateContextFlags::PREFER_ES3;
    } else {
        flags |= gl::CreateContextFlags::REQUIRE_COMPAT_PROFILE;
    }

    //////

    const bool useEGL = PR_GetEnv("MOZ_WEBGL_FORCE_EGL");

#ifdef XP_WIN
    tryNativeGL = false;
    tryANGLE = true;

    if (gfxPrefs::WebGLDisableWGL()) {
        tryNativeGL = false;
    }

    if (gfxPrefs::WebGLDisableANGLE() || PR_GetEnv("MOZ_WEBGL_FORCE_OPENGL") || useEGL) {
        tryNativeGL = true;
        tryANGLE = false;
    }
#endif

    if (tryNativeGL && !forceEnabled) {
        const nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
        const auto feature = nsIGfxInfo::FEATURE_WEBGL_OPENGL;

        FailureReason reason;
        if (IsFeatureInBlacklist(gfxInfo, feature, &reason.key)) {
            reason.info = "Refused to create native OpenGL context because of blacklist"
                          " entry: ";
            reason.info.Append(reason.key);

            out_failReasons->push_back(reason);

            GenerateWarning("%s", reason.info.BeginReading());
            tryNativeGL = false;
        }
    }

    //////

    if (tryNativeGL) {
        if (useEGL)
            return CreateAndInitGLWith(CreateGLWithEGL, flags, out_failReasons);

        if (CreateAndInitGLWith(CreateGLWithDefault, flags, out_failReasons))
            return true;
    }

    //////

    if (tryANGLE)
        return CreateAndInitGLWith(CreateGLWithANGLE, flags, out_failReasons);

    //////

    out_failReasons->push_back(FailureReason("FEATURE_FAILURE_WEBGL_EXHAUSTED_DRIVERS",
                                             "Exhausted GL driver options."));
    return false;
}

////////////////////////////////////////

void
WebGLContext::ResizeBackbuffer(const uint32_t requestedWidth,
                               const uint32_t requestedHeight)
{
    mAntialiasedFB = nullptr;
    mPreservedFB = nullptr;
    SetSharedFB(nullptr);

    mWidth = std::max<uint32_t>(1, requestedWidth);
    mHeight = std::max<uint32_t>(1, requestedHeight);
}

////////////////////////////////////////

void
WebGLContext::ThrowEvent_WebGLContextCreationError(const nsACString& text)
{
    RefPtr<EventTarget> target = mCanvasElement;
    if (!target && mOffscreenCanvas) {
        target = mOffscreenCanvas;
    } else if (!target) {
        GenerateWarning("Failed to create WebGL context: %s", text.BeginReading());
        return;
    }

    const auto kEventName = NS_LITERAL_STRING("webglcontextcreationerror");

    WebGLContextEventInit eventInit;
    // eventInit.mCancelable = true; // The spec says this, but it's silly.
    eventInit.mStatusMessage = NS_ConvertASCIItoUTF16(text);

    const RefPtr<WebGLContextEvent> event = WebGLContextEvent::Constructor(target,
                                                                           kEventName,
                                                                           eventInit);
    event->SetTrusted(true);

    bool didPreventDefault;
    target->DispatchEvent(event, &didPreventDefault);

    //////

    GenerateWarning("Failed to create WebGL context: %s", text.BeginReading());
}

NS_IMETHODIMP
WebGLContext::SetDimensions(int32_t signedWidth, int32_t signedHeight)
{
    if (signedWidth < 0 || signedHeight < 0) {
        if (!gl) {
            Telemetry::Accumulate(Telemetry::CANVAS_WEBGL_FAILURE_ID,
                                  NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_SIZE"));
        }
        GenerateWarning("Canvas size is too large (seems like a negative value wrapped)");
        return NS_ERROR_OUT_OF_MEMORY;
    }

    uint32_t width = signedWidth;
    uint32_t height = signedHeight;

    // Early success return cases

    // May have a OffscreenCanvas instead of an HTMLCanvasElement
    if (GetCanvas())
        GetCanvas()->InvalidateCanvas();

    // Zero-sized surfaces can cause problems.
    if (width == 0)
        width = 1;

    if (height == 0)
        height = 1;

    // If we already have a gl context, then we just need to resize it
    if (gl) {
        if ((uint32_t)mWidth == width &&
            (uint32_t)mHeight == height)
        {
            return NS_OK;
        }

        if (IsContextLost())
            return NS_OK;

        MakeContextCurrent();

        // If we've already drawn, we should commit the current buffer.
        PresentScreenBuffer();

        if (IsContextLost()) {
            GenerateWarning("WebGL context was lost due to swap failure.");
            return NS_OK;
        }

        // ResizeOffscreen scraps the current prod buffer before making a new one.
        ResizeBackbuffer(width, height);

        // everything's good, we're done here
        mResetLayer = true;
        return NS_OK;
    }

    nsCString failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_UNKOWN");
    auto autoTelemetry = mozilla::MakeScopeExit([&] {
        Telemetry::Accumulate(Telemetry::CANVAS_WEBGL_FAILURE_ID,
                              failureId);
    });

    // End of early return cases.
    // At this point we know that we're not just resizing an existing context,
    // we are initializing a new context.

    // if we exceeded either the global or the per-principal limit for WebGL contexts,
    // lose the oldest-used context now to free resources. Note that we can't do that
    // in the WebGLContext constructor as we don't have a canvas element yet there.
    // Here is the right place to do so, as we are about to create the OpenGL context
    // and that is what can fail if we already have too many.
    LoseOldestWebGLContextIfLimitExceeded();

    // We're going to create an entirely new context.  If our
    // generation is not 0 right now (that is, if this isn't the first
    // context we're creating), we may have to dispatch a context lost
    // event.

    // If incrementing the generation would cause overflow,
    // don't allow it.  Allowing this would allow us to use
    // resource handles created from older context generations.
    if (!(mGeneration + 1).isValid()) {
        // exit without changing the value of mGeneration
        failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_TOO_MANY");
        const nsLiteralCString text("Too many WebGL contexts created this run.");
        ThrowEvent_WebGLContextCreationError(text);
        return NS_ERROR_FAILURE;
    }

    // increment the generation number - Do this early because later
    // in CreateOffscreenGL(), "default" objects are created that will
    // pick up the old generation.
    ++mGeneration;

    bool disabled = gfxPrefs::WebGLDisabled();

    // TODO: When we have software webgl support we should use that instead.
    disabled |= gfxPlatform::InSafeMode();

    if (disabled) {
        if (gfxPlatform::InSafeMode()) {
            failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_SAFEMODE");
        } else {
            failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_DISABLED");
        }
        const nsLiteralCString text("WebGL is currently disabled.");
        ThrowEvent_WebGLContextCreationError(text);
        return NS_ERROR_FAILURE;
    }

    if (gfxPrefs::WebGLDisableFailIfMajorPerformanceCaveat()) {
        mMutableOptions.failIfMajorPerformanceCaveat = false;
    }

    if (mOptions.failIfMajorPerformanceCaveat) {
        nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
        if (!HasAcceleratedLayers(gfxInfo)) {
            failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_PERF_CAVEAT");
            const nsLiteralCString text("failIfMajorPerformanceCaveat: Compositor is not"
                                        " hardware-accelerated.");
            ThrowEvent_WebGLContextCreationError(text);
            return NS_ERROR_FAILURE;
        }
    }

    // Alright, now let's start trying.
    bool forceEnabled = gfxPrefs::WebGLForceEnabled();
    ScopedGfxFeatureReporter reporter("WebGL", forceEnabled);

    MOZ_ASSERT(!gl);
    std::vector<FailureReason> failReasons;
    if (!CreateAndInitGL(forceEnabled, &failReasons)) {
        nsCString text("WebGL creation failed: ");
        for (const auto& cur : failReasons) {
            Telemetry::Accumulate(Telemetry::CANVAS_WEBGL_FAILURE_ID, cur.key);

            text.AppendASCII("\n* ");
            text.Append(cur.info);
        }
        failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_REASON");
        ThrowEvent_WebGLContextCreationError(text);
        return NS_ERROR_FAILURE;
    }
    MOZ_ASSERT(gl);

    if (mOptions.failIfMajorPerformanceCaveat) {
        if (gl->IsWARP()) {
            DestroyResourcesAndContext();
            MOZ_ASSERT(!gl);

            failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_PERF_WARP");
            const nsLiteralCString text("failIfMajorPerformanceCaveat: Driver is not"
                                        " hardware-accelerated.");
            ThrowEvent_WebGLContextCreationError(text);
            return NS_ERROR_FAILURE;
        }

#ifdef XP_WIN
        if (gl->GetContextType() == gl::GLContextType::WGL &&
            !gl::sWGLLib.HasDXInterop2())
        {
            DestroyResourcesAndContext();
            MOZ_ASSERT(!gl);

            failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_DXGL_INTEROP2");
            const nsLiteralCString text("Caveat: WGL without DXGLInterop2.");
            ThrowEvent_WebGLContextCreationError(text);
            return NS_ERROR_FAILURE;
        }
#endif
    }

    ////

    mDefaultFB_DrawBuffer0 = LOCAL_GL_BACK;
    mDefaultFB_ReadBuffer = LOCAL_GL_BACK;

    ////

    const bool shsurfDepthStencil = mOptions.FrontbufferHasDepthStencil();

    layers::TextureFlags flags = layers::TextureFlags::ORIGIN_BOTTOM_LEFT;
    if (mOptions.alpha && !mOptions.premultipliedAlpha) {
        flags |= layers::TextureFlags::NON_PREMULTIPLIED;
    }
    mSurfFactory.Reset(AsUnique(new SurfaceFactory_Basic(gl, shsurfDepthStencil, nullptr,
                                                         flags)));

    ////

    ResizeBackbuffer(width, height);

    // Update our internal stuff:
    if (gl->WorkAroundDriverBugs()) {
#ifdef MOZ_WIDGET_COCOA
        if (!nsCocoaFeatures::IsAtLeastVersion(10, 12) &&
            gl->Vendor() == GLVendor::Intel)
        {
            mNeedsEmulatedLoneDepthStencil = true;
        }
#endif
    }

    MakeContextCurrent();

    if (!EnsureDefaultFBsResized(nullptr)) {
        const nsLiteralCString text("Failed to allocated backbuffers.");
        ThrowEvent_WebGLContextCreationError(text);
        failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_BACKBUFFER");
        return NS_ERROR_FAILURE;
    }

    // Present the cleared initial buffer
    mShouldPresent = true;
    mResetLayer = true;

    //////
    // Initial setup.

    gl->fViewport(0, 0, mWidth, mHeight);
    mViewportX = mViewportY = 0;
    mViewportWidth = mWidth;
    mViewportHeight = mHeight;

    gl->fScissor(0, 0, mWidth, mHeight);

    ////

    reporter.SetSuccessful();
    failureId = NS_LITERAL_CSTRING("SUCCESS");
    return NS_OK;
}

void
WebGLContext::LoseOldestWebGLContextIfLimitExceeded()
{
#ifdef MOZ_GFX_OPTIMIZE_MOBILE
    // some mobile devices can't have more than 8 GL contexts overall
    const size_t kMaxWebGLContextsPerPrincipal = 2;
    const size_t kMaxWebGLContexts             = 4;
#else
    const size_t kMaxWebGLContextsPerPrincipal = 16;
    const size_t kMaxWebGLContexts             = 32;
#endif
    MOZ_ASSERT(kMaxWebGLContextsPerPrincipal < kMaxWebGLContexts);

    if (!NS_IsMainThread()) {
        // XXX mtseng: bug 709490, WebGLMemoryTracker is not thread safe.
        return;
    }

    // it's important to update the index on a new context before losing old contexts,
    // otherwise new unused contexts would all have index 0 and we couldn't distinguish older ones
    // when choosing which one to lose first.
    UpdateLastUseIndex();

    WebGLMemoryTracker::ContextsArrayType& contexts = WebGLMemoryTracker::Contexts();

    // quick exit path, should cover a majority of cases
    if (contexts.Length() <= kMaxWebGLContextsPerPrincipal)
        return;

    // note that here by "context" we mean "non-lost context". See the check for
    // IsContextLost() below. Indeed, the point of this function is to maybe lose
    // some currently non-lost context.

    uint64_t oldestIndex = UINT64_MAX;
    uint64_t oldestIndexThisPrincipal = UINT64_MAX;
    const WebGLContext* oldestContext = nullptr;
    const WebGLContext* oldestContextThisPrincipal = nullptr;
    size_t numContexts = 0;
    size_t numContextsThisPrincipal = 0;

    for(size_t i = 0; i < contexts.Length(); ++i) {
        // don't want to lose ourselves.
        if (contexts[i] == this)
            continue;

        if (contexts[i]->IsContextLost())
            continue;

        if (!contexts[i]->GetCanvas()) {
            // Zombie context: the canvas is already destroyed, but something else
            // (typically the compositor) is still holding on to the context.
            // Killing zombies is a no-brainer.
            const_cast<WebGLContext*>(contexts[i])->LoseContext();
            continue;
        }

        numContexts++;
        if (contexts[i]->mLastUseIndex < oldestIndex) {
            oldestIndex = contexts[i]->mLastUseIndex;
            oldestContext = contexts[i];
        }

        nsIPrincipal* ourPrincipal = GetCanvas()->NodePrincipal();
        nsIPrincipal* theirPrincipal = contexts[i]->GetCanvas()->NodePrincipal();
        bool samePrincipal;
        nsresult rv = ourPrincipal->Equals(theirPrincipal, &samePrincipal);
        if (NS_SUCCEEDED(rv) && samePrincipal) {
            numContextsThisPrincipal++;
            if (contexts[i]->mLastUseIndex < oldestIndexThisPrincipal) {
                oldestIndexThisPrincipal = contexts[i]->mLastUseIndex;
                oldestContextThisPrincipal = contexts[i];
            }
        }
    }

    if (numContextsThisPrincipal > kMaxWebGLContextsPerPrincipal) {
        GenerateWarning("Exceeded %" PRIuSIZE " live WebGL contexts for this principal, losing the "
                        "least recently used one.", kMaxWebGLContextsPerPrincipal);
        MOZ_ASSERT(oldestContextThisPrincipal); // if we reach this point, this can't be null
        const_cast<WebGLContext*>(oldestContextThisPrincipal)->LoseContext();
    } else if (numContexts > kMaxWebGLContexts) {
        GenerateWarning("Exceeded %" PRIuSIZE " live WebGL contexts, losing the least "
                        "recently used one.", kMaxWebGLContexts);
        MOZ_ASSERT(oldestContext); // if we reach this point, this can't be null
        const_cast<WebGLContext*>(oldestContext)->LoseContext();
    }
}

UniquePtr<uint8_t[]>
WebGLContext::GetImageBuffer(int32_t* out_format)
{
    *out_format = 0;

    // Use GetSurfaceSnapshot() to make sure that appropriate y-flip gets applied
    bool premult;
    RefPtr<SourceSurface> snapshot =
      GetSurfaceSnapshot(mOptions.premultipliedAlpha ? nullptr : &premult);
    if (!snapshot) {
        return nullptr;
    }

    MOZ_ASSERT(mOptions.premultipliedAlpha || !premult, "We must get unpremult when we ask for it!");

    RefPtr<DataSourceSurface> dataSurface = snapshot->GetDataSurface();

    return gfxUtils::GetImageBuffer(dataSurface, mOptions.premultipliedAlpha,
                                    out_format);
}

NS_IMETHODIMP
WebGLContext::GetInputStream(const char* mimeType,
                             const char16_t* encoderOptions,
                             nsIInputStream** out_stream)
{
    NS_ASSERTION(gl, "GetInputStream on invalid context?");
    if (!gl)
        return NS_ERROR_FAILURE;

    // Use GetSurfaceSnapshot() to make sure that appropriate y-flip gets applied
    bool premult;
    RefPtr<SourceSurface> snapshot =
      GetSurfaceSnapshot(mOptions.premultipliedAlpha ? nullptr : &premult);
    if (!snapshot)
        return NS_ERROR_FAILURE;

    MOZ_ASSERT(mOptions.premultipliedAlpha || !premult, "We must get unpremult when we ask for it!");

    RefPtr<DataSourceSurface> dataSurface = snapshot->GetDataSurface();
    return gfxUtils::GetInputStream(dataSurface, mOptions.premultipliedAlpha, mimeType,
                                    encoderOptions, out_stream);
}

void
WebGLContext::UpdateLastUseIndex()
{
    static CheckedInt<uint64_t> sIndex = 0;

    sIndex++;

    // should never happen with 64-bit; trying to handle this would be riskier than
    // not handling it as the handler code would never get exercised.
    if (!sIndex.isValid())
        MOZ_CRASH("Can't believe it's been 2^64 transactions already!");
    mLastUseIndex = sIndex.value();
}

static uint8_t gWebGLLayerUserData;
static uint8_t gWebGLMirrorLayerUserData;

class WebGLContextUserData : public LayerUserData
{
public:
    explicit WebGLContextUserData(HTMLCanvasElement* canvas)
        : mCanvas(canvas)
    {}

    /* PreTransactionCallback gets called by the Layers code every time the
     * WebGL canvas is going to be composited.
     */
    static void PreTransactionCallback(void* data) {
        WebGLContextUserData* userdata = static_cast<WebGLContextUserData*>(data);
        HTMLCanvasElement* canvas = userdata->mCanvas;
        WebGLContext* webgl = static_cast<WebGLContext*>(canvas->GetContextAtIndex(0));

        // Prepare the context for composition
        webgl->BeginComposition();
    }

    /** DidTransactionCallback gets called by the Layers code everytime the WebGL canvas gets composite,
      * so it really is the right place to put actions that have to be performed upon compositing
      */
    static void DidTransactionCallback(void* data) {
        WebGLContextUserData* userdata = static_cast<WebGLContextUserData*>(data);
        HTMLCanvasElement* canvas = userdata->mCanvas;
        WebGLContext* webgl = static_cast<WebGLContext*>(canvas->GetContextAtIndex(0));

        // Clean up the context after composition
        webgl->EndComposition();
    }

private:
    RefPtr<HTMLCanvasElement> mCanvas;
};

already_AddRefed<layers::Layer>
WebGLContext::GetCanvasLayer(nsDisplayListBuilder* builder,
                             Layer* oldLayer,
                             LayerManager* manager,
                             bool aMirror /*= false*/)
{
    if (IsContextLost())
        return nullptr;

    if (!mResetLayer && oldLayer &&
        oldLayer->HasUserData(aMirror ? &gWebGLMirrorLayerUserData : &gWebGLLayerUserData)) {
        RefPtr<layers::Layer> ret = oldLayer;
        return ret.forget();
    }

    RefPtr<CanvasLayer> canvasLayer = manager->CreateCanvasLayer();
    if (!canvasLayer) {
        NS_WARNING("CreateCanvasLayer returned null!");
        return nullptr;
    }

    WebGLContextUserData* userData = nullptr;
    if (builder->IsPaintingToWindow() && mCanvasElement && !aMirror) {
        // Make the layer tell us whenever a transaction finishes (including
        // the current transaction), so we can clear our invalidation state and
        // start invalidating again. We need to do this for the layer that is
        // being painted to a window (there shouldn't be more than one at a time,
        // and if there is, flushing the invalidation state more often than
        // necessary is harmless).

        // The layer will be destroyed when we tear down the presentation
        // (at the latest), at which time this userData will be destroyed,
        // releasing the reference to the element.
        // The userData will receive DidTransactionCallbacks, which flush the
        // the invalidation state to indicate that the canvas is up to date.
        userData = new WebGLContextUserData(mCanvasElement);
        canvasLayer->SetDidTransactionCallback(
            WebGLContextUserData::DidTransactionCallback, userData);
        canvasLayer->SetPreTransactionCallback(
            WebGLContextUserData::PreTransactionCallback, userData);
    }

    canvasLayer->SetUserData(aMirror ? &gWebGLMirrorLayerUserData : &gWebGLLayerUserData, userData);

    ////

    bool isAlphaPremult = mOptions.premultipliedAlpha;
    if (!isAlphaPremult && !mOptions.alpha) {
        isAlphaPremult = true;
    }
    CanvasLayer::Data data(gfx::IntSize(mWidth, mHeight));
    data.mWebGL = this;

    canvasLayer->Initialize(data);
    uint32_t flags = 0;
    if (!mOptions.alpha) {
        flags |= Layer::CONTENT_OPAQUE;
    }
    canvasLayer->SetContentFlags(flags);
    canvasLayer->Updated();

    mResetLayer = false;
    // We only wish to update mLayerIsMirror when a new layer is returned.
    // If a cached layer is returned above, aMirror is not changing since
    // the last cached layer was created and mLayerIsMirror is still valid.
    mLayerIsMirror = aMirror;

    return canvasLayer.forget();
}

layers::LayersBackend
WebGLContext::GetCompositorBackendType() const
{
    if (mCanvasElement) {
        return mCanvasElement->GetCompositorBackendType();
    } else if (mOffscreenCanvas) {
        return mOffscreenCanvas->GetCompositorBackendType();
    }

    return LayersBackend::LAYERS_NONE;
}

void
WebGLContext::Commit()
{
    if (mOffscreenCanvas) {
        mOffscreenCanvas->CommitFrameToCompositor();
    }
}

void
WebGLContext::GetCanvas(Nullable<dom::OwningHTMLCanvasElementOrOffscreenCanvas>& retval)
{
    if (mCanvasElement) {
        MOZ_RELEASE_ASSERT(!mOffscreenCanvas, "GFX: Canvas is offscreen.");

        if (mCanvasElement->IsInNativeAnonymousSubtree()) {
          retval.SetNull();
        } else {
          retval.SetValue().SetAsHTMLCanvasElement() = mCanvasElement;
        }
    } else if (mOffscreenCanvas) {
        retval.SetValue().SetAsOffscreenCanvas() = mOffscreenCanvas;
    } else {
        retval.SetNull();
    }
}

void
WebGLContext::GetContextAttributes(dom::Nullable<dom::WebGLContextAttributes>& retval)
{
    retval.SetNull();
    if (IsContextLost())
        return;

    dom::WebGLContextAttributes& result = retval.SetValue();

    result.mAlpha.Construct(mOptions.alpha);
    result.mDepth = mOptions.depth;
    result.mStencil = mOptions.stencil;
    result.mAntialias = mOptions.antialias;
    result.mPremultipliedAlpha = mOptions.premultipliedAlpha;
    result.mPreserveDrawingBuffer = mOptions.preserveDrawingBuffer;
    result.mFailIfMajorPerformanceCaveat = mOptions.failIfMajorPerformanceCaveat;
}

NS_IMETHODIMP
WebGLContext::MozGetUnderlyingParamString(uint32_t pname, nsAString& retval)
{
    if (IsContextLost())
        return NS_OK;

    retval.SetIsVoid(true);

    MakeContextCurrent();

    switch (pname) {
    case LOCAL_GL_VENDOR:
    case LOCAL_GL_RENDERER:
    case LOCAL_GL_VERSION:
    case LOCAL_GL_SHADING_LANGUAGE_VERSION:
    case LOCAL_GL_EXTENSIONS:
        {
            const char* s = (const char*)gl->fGetString(pname);
            retval.Assign(NS_ConvertASCIItoUTF16(nsDependentCString(s)));
            break;
        }

    default:
        return NS_ERROR_INVALID_ARG;
    }

    return NS_OK;
}

////////////////////////////////////////

void
WebGLContext::BackbufferFormats(const webgl::FormatInfo** const out_color,
                                const webgl::FormatInfo** const out_depth,
                                const webgl::FormatInfo** const out_stencil) const
{
    const auto effFormat = (mOptions.alpha ? webgl::EffectiveFormat::RGBA8
                                           : webgl::EffectiveFormat::RGB8);
    *out_color = webgl::GetFormat(effFormat);

    *out_depth = nullptr;
    *out_stencil = nullptr;
    if (mOptions.depth && mOptions.stencil) {
        *out_depth = webgl::GetFormat(webgl::EffectiveFormat::DEPTH24_STENCIL8);
        *out_stencil = *out_depth;
    } else {
        if (mOptions.depth) {
            *out_depth = webgl::GetFormat(webgl::EffectiveFormat::DEPTH_COMPONENT24);
        }
        if (mOptions.stencil) {
            *out_stencil = webgl::GetFormat(webgl::EffectiveFormat::STENCIL_INDEX8);
        }
    }
}

////////////////////////////////////////

GLuint
WebGLContext::DefaultDrawFB() const
{
    MOZ_ASSERT(mAntialiasedFB || mPreservedFB || mSharedFB);
    if (mAntialiasedFB)
        return mAntialiasedFB->mFB;

    return DefaultReadFB();
}

GLuint
WebGLContext::DefaultReadFB() const
{
    MOZ_ASSERT(mPreservedFB || mSharedFB);
    if (mPreservedFB)
        return mPreservedFB->mFB;

    return mSharedFB->Surf()->mFB;
}

////

static void
DriverInvalidateFB(gl::GLContext* const gl, const GLenum target, const GLuint fb,
                   const bool keepColor)
{
    gl->fBindFramebuffer(target, fb);

    const GLenum kForUserFB[] = {
        LOCAL_GL_COLOR_ATTACHMENT0,
        LOCAL_GL_DEPTH_ATTACHMENT,
        LOCAL_GL_STENCIL_ATTACHMENT,
    };
    const GLenum kForDefaultFB[] = {
        LOCAL_GL_COLOR,
        LOCAL_GL_DEPTH,
        LOCAL_GL_STENCIL,
    };

    GLsizei numAttachments = 3;
    auto attachments = (fb ? kForUserFB : kForDefaultFB);
    if (keepColor) {
        attachments += 1;
        numAttachments -= 1;
    }

    if (gl->IsSupported(gl::GLFeature::invalidate_framebuffer)) {
        gl->fInvalidateFramebuffer(target, numAttachments, attachments);
        return;
    }
    if (gl->IsExtensionSupported(gl::GLContext::EXT_discard_framebuffer)) {
        if (target == LOCAL_GL_FRAMEBUFFER ||
            gl->IsExtensionSupported(gl::GLContext::APPLE_framebuffer_multisample))
        {
            gl->fDiscardFramebufferEXT(target, numAttachments, attachments);
            return;
        }
    }
}

static void
ResolveFramebuffer(gl::GLContext* const gl, const uint32_t width, const uint32_t height)
{
    MOZ_ASSERT(gl->IsSupported(GLFeature::split_framebuffer));

    if (gl->IsSupported(gl::GLFeature::framebuffer_blit)) {
        gl->fBlitFramebuffer(0, 0, width, height,
                             0, 0, width, height,
                             LOCAL_GL_COLOR_BUFFER_BIT, LOCAL_GL_NEAREST);
    } else if (gl->IsExtensionSupported(gl::GLContext::APPLE_framebuffer_multisample)) {
        gl->fResolveMultisampleFramebufferAPPLE();
    } else {
        MOZ_CRASH("GFX: No available blit methods.");
    }
}

static void
AssertCurFB(gl::GLContext* const gl, const GLenum target, const GLuint expected)
{
#ifdef DEBUG
    GLuint actual;
    if (target == LOCAL_GL_READ_FRAMEBUFFER) {
        actual = gl->GetIntAs<GLuint>(LOCAL_GL_READ_FRAMEBUFFER_BINDING);
    } else {
        // DRAW_FRAMEBUFFER_BINDING is the same enum as FRAMEBUFFER_BINDING.
        actual = gl->GetIntAs<GLuint>(LOCAL_GL_FRAMEBUFFER_BINDING);
    }
    MOZ_ASSERT(actual == expected, "fb misprediction");

    if (gl->IsSupported(GLFeature::split_framebuffer) &&
        target == LOCAL_GL_FRAMEBUFFER)
    {
        actual = gl->GetIntAs<GLuint>(LOCAL_GL_READ_FRAMEBUFFER_BINDING);
        MOZ_ASSERT(actual == expected, "unexpected fb split");
    }
#endif
}

////

static void
DefaultFB_DrawBuffer(gl::GLContext* const gl, const GLenum mode, const GLuint driverFB)
{
    MOZ_ASSERT(mode != LOCAL_GL_COLOR_ATTACHMENT0);

    auto driverMode = mode;
    if (driverFB != 0 && mode == LOCAL_GL_BACK) {
        driverMode = LOCAL_GL_COLOR_ATTACHMENT0;
    }
    gl->fDrawBuffers(1, &driverMode);
}

////

bool
WebGLContext::DoBindDrawFB(const char* const funcName, const GLenum target)
{
    MOZ_ASSERT(target != LOCAL_GL_READ_FRAMEBUFFER);
    GLuint driverFB;
    if (mBoundDrawFramebuffer) {
        if (!mBoundDrawFramebuffer->ValidateAndInitAttachments(funcName, true))
            return false;
        driverFB = mBoundDrawFramebuffer->mGLName;
    } else {
        if (!PrepareDefaultDrawFB(funcName))
            return false;
        driverFB = DefaultDrawFB();
    }
    gl->fBindFramebuffer(target, driverFB);

    const bool isDefaultFB = !mBoundDrawFramebuffer;
    if (!mDrawState_IsDefaultFB || mDrawState_IsDefaultFB.value() != isDefaultFB) {
        mDrawState_IsDefaultFB = Some(isDefaultFB);

        bool fakeNoAlpha = false;
        bool fakeNoDepth = false;
        bool fakeNoStencil = false;
        if (isDefaultFB) {
            fakeNoAlpha = !mOptions.alpha;
            if (mOptions.HasDepthStencil()) {
                fakeNoDepth = !mOptions.depth;
                fakeNoStencil = !mOptions.stencil;
            }
        }

        gl->fColorMask(mColorWriteMask[0],
                       mColorWriteMask[1],
                       mColorWriteMask[2],
                       fakeNoAlpha ? false : mColorWriteMask[3]);

        // Behavior is the same when depth or stencil is missing and the respective test
        // bool is disabled.
        gl->SetEnable(LOCAL_GL_DEPTH_TEST, fakeNoDepth ? false : mDepthTestEnabled);
        gl->SetEnable(LOCAL_GL_STENCIL_TEST, fakeNoStencil ? false : mStencilTestEnabled);
    }

    if (isDefaultFB) {
        mShouldPresent = true;
        Invalidate();

        if (mAntialiasedFB) {
            // Since we're expecting a draw, expect it to get dirty.
            mAntialiasedFB_IsDirty = true;
        }
    }
    return true;
}

bool
WebGLContext::DoBindReadFB(const char* const funcName, const bool mayNeedIndirect,
                           const bool isFBOperation, const GLenum target)
{
    MOZ_ASSERT(target != LOCAL_GL_DRAW_FRAMEBUFFER);
    bool needsIndirect = false;
    GLuint driverFB;
    if (mBoundReadFramebuffer) {
        if (!mBoundReadFramebuffer->ValidateAndInitAttachments(funcName, isFBOperation))
            return false;
        driverFB = mBoundReadFramebuffer->mGLName;
    } else {
        if (!PrepareDefaultReadFB(funcName))
            return false;
        driverFB = DefaultReadFB();

        needsIndirect = (mayNeedIndirect &&
                         mSharedFB &&
                         mSharedFB->Surf()->NeedsIndirectReads());
    }

    gl->fBindFramebuffer(target, driverFB);

    if (needsIndirect) {
        mIndirectReadFB = gl::MozFramebuffer::Create(gl, mSharedFB->Surf()->mSize, 0,
                                                     false);
        if (!mIndirectReadFB) {
            GenerateWarning("%s: Failed to create indirect read FB.", funcName);
            ForceLoseContext();
            return false;
        }

        const gl::ScopedBindTexture bindTex(gl, mIndirectReadFB->ColorTex());
        gl->fCopyTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0,0, 0,0,
                               mIndirectReadFB->mSize.width,
                               mIndirectReadFB->mSize.height);

        gl->fBindFramebuffer(target, mIndirectReadFB->mFB);
    } else {
        mIndirectReadFB = nullptr;
    }
    return true;
}

bool
WebGLContext::DoBindBothFBs(const char* const funcName)
{
    // BindRead first, since BindDraw sets mAntialiasedFB_IsDirty.
    if (!DoBindReadFB(funcName, false, true, LOCAL_GL_READ_FRAMEBUFFER) ||
        !DoBindDrawFB(funcName, LOCAL_GL_DRAW_FRAMEBUFFER))
    {
        return false;
    }
    const auto readFB = (mBoundReadFramebuffer ? mBoundReadFramebuffer->mGLName
                                               : DefaultReadFB());
    const auto drawFB = (mBoundDrawFramebuffer ? mBoundDrawFramebuffer->mGLName
                                               : DefaultDrawFB());
    gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, readFB);
    gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, drawFB);
    return true;
}

////////////////////

bool
WebGLContext::PrepareDefaultDrawFB(const char* const funcName)
{
    if (!EnsureDefaultFBsResized(funcName))
        return false;

    if (mDefaultDrawFB_IsInvalidated) {
        mDefaultDrawFB_IsInvalidated = false;
        mAntialiasedFB_IsDirty = bool(mAntialiasedFB);

        const auto drawFB = DefaultDrawFB();
        gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, drawFB);

        if (mDefaultFB_DrawBuffer0 != LOCAL_GL_BACK) {
            DefaultFB_DrawBuffer(gl, LOCAL_GL_BACK, drawFB);
        }
        const bool fakeNoAlpha = !mOptions.alpha;
        ClearCurFBToDefaultValues(LOCAL_GL_FRAMEBUFFER, drawFB, fakeNoAlpha);

        if (mDefaultFB_DrawBuffer0 != LOCAL_GL_BACK) {
            DefaultFB_DrawBuffer(gl, mDefaultFB_DrawBuffer0, drawFB);
        }
    }
    return true;
}

bool
WebGLContext::PrepareDefaultReadFB(const char* const funcName)
{
    if (!PrepareDefaultDrawFB(funcName))
        return false;

    if (!mSharedFB) {
        const auto size = gfx::IntSize(mWidth, mHeight);
        const auto& sharedFB = mSurfFactory->NewTexClient(size);
        if (!sharedFB) {
            GenerateWarning("%s: Failed to allocate default read framebuffer.", funcName);
            return false;
        }
        SetSharedFB(sharedFB);
    }

    if (mAntialiasedFB_IsDirty) {
        mAntialiasedFB_IsDirty = false;

        const auto srcFB = DefaultDrawFB();
        const auto destFB = DefaultReadFB();
        gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, srcFB);
        gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destFB);

        if (mScissorTestEnabled) {
            gl->fDisable(LOCAL_GL_SCISSOR_TEST);
        }
        // Leave DITHER alone.
        ResolveFramebuffer(gl, mWidth, mHeight);
        if (mScissorTestEnabled) {
            gl->fEnable(LOCAL_GL_SCISSOR_TEST);
        }
    }
    return true;
}

////////////////////

bool
WebGLContext::EnsureDefaultFBsResized(const char* const funcName)
{
    if (mSharedFB || mAntialiasedFB || mPreservedFB)
        return true;

    const bool depthStencil = mOptions.HasDepthStencil();

    const auto fnCreate = [&](const gfx::IntSize& size) {
        const auto& sharedFB = mSurfFactory->NewTexClient(size);
        if (!sharedFB)
            return false;
        SetSharedFB(sharedFB);

        if (mOptions.antialias) {
            mAntialiasedFB = gl::MozFramebuffer::Create(gl, size, mAntialiasSamples,
                                                        depthStencil);
            if (!mAntialiasedFB)
                return false;
        } else if (mOptions.preserveDrawingBuffer) {
            mPreservedFB = gl::MozFramebuffer::Create(gl, size, 0, depthStencil);
            if (!mPreservedFB)
                return false;
        }
        return true;
    };

    const uint32_t maxSize = std::min(mImplMaxRenderbufferSize, mImplMaxTextureSize);
    const auto requestedWidth = mWidth;
    const auto requestedHeight = mHeight;

    // Fallback to smaller size on failure.
    bool firstRun = true;
    while (true) {
        if (firstRun) {
            firstRun = false;
        } else {
            mWidth /= 2;
            mHeight /= 2;
        }

        if (!mWidth && !mHeight) {
            if (funcName) {
                GenerateWarning("%s: Failed to allocate default framebuffers, losing"
                                " context...",
                                funcName);
                ForceLoseContext();
            }
            SetSharedFB(nullptr);
            mAntialiasedFB = nullptr;
            mPreservedFB = nullptr;
            return false;
        }

        mWidth = std::max(1, mWidth);
        mHeight = std::max(1, mHeight);

        if (uint32_t(mWidth) > maxSize ||
            uint32_t(mHeight) > maxSize)
        {
            continue;
        }

        const auto size = gfx::IntSize(mWidth, mHeight);
        if (fnCreate(size))
            break;
    }

    if (mWidth != requestedWidth ||
        mHeight != requestedHeight)
    {
        GenerateWarning("Requested size %dx%d was too large, resized to %dx%d.",
                        requestedWidth, requestedHeight,
                        mWidth, mHeight);
    }

    mDefaultDrawFB_IsInvalidated = true;
    return true;
}

////

void
WebGLContext::SetSharedFB(layers::SharedSurfaceTextureClient* const sharedFB)
{
    if (mSharedFB) {
        const auto& surf = mSharedFB->Surf();
        surf->mGL->PopSurfaceLock(surf);
        surf->ProducerRelease();
    }
    mSharedFB = sharedFB;
    if (mSharedFB) {
        const auto& surf = mSharedFB->Surf();
        surf->ProducerAcquire();
        surf->mGL->PushSurfaceLock(surf);
    }
}

////////////////////////////////////////

void
WebGLContext::ClearCurFBToDefaultValues(const GLenum target, const GLuint curFB,
                                        const bool fakeNoAlpha) const
{
    MOZ_ASSERT(target != LOCAL_GL_READ_FRAMEBUFFER);
    AssertCurFB(gl, target, curFB);

    // Fun GL fact: No need to worry about the viewport here, glViewport is just
    // setting up a coordinates transformation, it doesn't affect glClear at all.

    ////
    // Prepare GL state for clearing.

    if (mScissorTestEnabled) {
        gl->fDisable(LOCAL_GL_SCISSOR_TEST);
    }
    if (mRasterizerDiscardEnabled) {
        gl->fDisable(LOCAL_GL_RASTERIZER_DISCARD);
    }

    gl->fClearColor(0, 0, 0, (fakeNoAlpha ? 1 : 0));
    const bool changeClearDepth = (mDepthClearValue != 1.0f);
    if (changeClearDepth) {
        gl->fClearDepth(1.0f);
    }
    const bool changeClearStencil = (mStencilClearValue != 0);
    if (changeClearStencil) {
        gl->fClearStencil(0);
    }

    gl->fColorMask(true, true, true, true);
    const bool changeDepthMask = (mDepthWriteMask != true);
    if (changeDepthMask) {
        gl->fDepthMask(true);
    }
    // "The clear operation always uses the front stencil write mask
    //  when clearing the stencil buffer."
    const bool changeStencilMask = (mStencilWriteMaskFront != 0xffffffff);
    if (changeStencilMask) {
        gl->fStencilMaskSeparate(LOCAL_GL_FRONT, 0xffffffff);
    }

    ////
    // Do the clear!

    gl->fClear(LOCAL_GL_COLOR_BUFFER_BIT |
               LOCAL_GL_DEPTH_BUFFER_BIT |
               LOCAL_GL_STENCIL_BUFFER_BIT);

    ////
    // And reset!

    if (mScissorTestEnabled) {
        gl->fEnable(LOCAL_GL_SCISSOR_TEST);
    }
    if (mRasterizerDiscardEnabled) {
        gl->fEnable(LOCAL_GL_RASTERIZER_DISCARD);
    }

    gl->fClearColor(mColorClearValue[0],
                    mColorClearValue[1],
                    mColorClearValue[2],
                    mColorClearValue[3]);
    if (changeClearDepth) {
        gl->fClearDepth(mDepthClearValue);
    }
    if (changeClearStencil) {
        gl->fClearStencil(mStencilClearValue);
    }

    if (mDrawState_IsDefaultFB) {
        const auto& isDefaultFB = mDrawState_IsDefaultFB.value();
        bool alphaMask = mColorWriteMask[3];
        if (isDefaultFB && !mOptions.alpha) {
            alphaMask = false;
        }
        gl->fColorMask(mColorWriteMask[0],
                       mColorWriteMask[1],
                       mColorWriteMask[2],
                       alphaMask);
    }
    if (changeDepthMask) {
        gl->fDepthMask(mDepthWriteMask);
    }
    if (changeStencilMask) {
        gl->fStencilMaskSeparate(LOCAL_GL_FRONT, mStencilWriteMaskFront);
    }
}

// For an overview of how WebGL compositing works, see:
// https://wiki.mozilla.org/Platform/GFX/WebGL/Compositing
void
WebGLContext::PresentScreenBuffer()
{
    if (IsContextLost())
        return;

    if (!mShouldPresent)
        return;
    mShouldPresent = false;

    gl->MakeCurrent();

    if (!PrepareDefaultReadFB("PresentScreenBuffer"))
        return;

    if (!mOptions.preserveDrawingBuffer) {
        if (mAntialiasedFB) {
            // If we just resolved, READ_FRAMEBUFFER will be already set to this.
            const bool keepColor = false;
            DriverInvalidateFB(gl, LOCAL_GL_READ_FRAMEBUFFER, mAntialiasedFB->mFB,
                               keepColor);

        } else if (mOptions.HasDepthStencil()) {
            // FRAMEBUFFER probably will be already set to this.
            const bool keepColor = true;
            DriverInvalidateFB(gl, LOCAL_GL_FRAMEBUFFER, mSharedFB->Surf()->mFB,
                               keepColor);
        }
        mDefaultDrawFB_IsInvalidated = true;
    }

    if (mPreservedFB) {
        const auto& surf = mSharedFB->Surf();
        surf->CopyFrom(mPreservedFB.get());
    }

    mFrontBuffer = mSharedFB;
    SetSharedFB(nullptr);

    mDrawCallsSinceLastFlush = 0;
}

////////////////////////////////////////

// Prepare the context for capture before compositing
RefPtr<layers::CanvasLayer::FrameData>
WebGLContext::GetNewFrame()
{
    // Present our screenbuffer, if needed.
    PresentScreenBuffer();
    return new layers::CanvasLayer::FrameData(mFrontBuffer);
}

// Clean up the context after captured for compositing
void
WebGLContext::EndComposition()
{
    // Mark ourselves as no longer invalidated.
    MarkContextClean();
    UpdateLastUseIndex();
}

bool
WebGLContext::Has64BitTimestamps() const
{
    // 'sync' provides glGetInteger64v either by supporting ARB_sync, GL3+, or GLES3+.
    return gl->IsSupported(GLFeature::sync);
}

static bool
CheckContextLost(GLContext* gl, bool* const out_isGuilty)
{
    MOZ_ASSERT(gl);
    MOZ_ASSERT(out_isGuilty);

    bool isEGL = gl->GetContextType() == gl::GLContextType::EGL;

    GLenum resetStatus = LOCAL_GL_NO_ERROR;
    if (gl->IsSupported(GLFeature::robustness)) {
        gl->MakeCurrent();
        resetStatus = gl->fGetGraphicsResetStatus();
    } else if (isEGL) {
        // Simulate a ARB_robustness guilty context loss for when we
        // get an EGL_CONTEXT_LOST error. It may not actually be guilty,
        // but we can't make any distinction.
        if (!gl->MakeCurrent(true) && gl->IsContextLost()) {
            resetStatus = LOCAL_GL_UNKNOWN_CONTEXT_RESET_ARB;
        }
    }

    if (resetStatus == LOCAL_GL_NO_ERROR) {
        *out_isGuilty = false;
        return false;
    }

    // Assume guilty unless we find otherwise!
    bool isGuilty = true;
    switch (resetStatus) {
    case LOCAL_GL_INNOCENT_CONTEXT_RESET_ARB:
        // Either nothing wrong, or not our fault.
        isGuilty = false;
        break;
    case LOCAL_GL_GUILTY_CONTEXT_RESET_ARB:
        NS_WARNING("WebGL content on the page definitely caused the graphics"
                   " card to reset.");
        break;
    case LOCAL_GL_UNKNOWN_CONTEXT_RESET_ARB:
        NS_WARNING("WebGL content on the page might have caused the graphics"
                   " card to reset");
        // If we can't tell, assume guilty.
        break;
    default:
        MOZ_ASSERT(false, "Unreachable.");
        // If we do get here, let's pretend to be guilty as an escape plan.
        break;
    }

    if (isGuilty) {
        NS_WARNING("WebGL context on this page is considered guilty, and will"
                   " not be restored.");
    }

    *out_isGuilty = isGuilty;
    return true;
}

bool
WebGLContext::TryToRestoreContext()
{
    if (NS_FAILED(SetDimensions(mWidth, mHeight)))
        return false;

    return true;
}

void
WebGLContext::RunContextLossTimer()
{
    mContextLossHandler.RunTimer();
}

class UpdateContextLossStatusTask : public CancelableRunnable
{
    RefPtr<WebGLContext> mWebGL;

public:
    explicit UpdateContextLossStatusTask(WebGLContext* webgl)
        : mWebGL(webgl)
    {
    }

    NS_IMETHOD Run() override {
        if (mWebGL)
            mWebGL->UpdateContextLossStatus();

        return NS_OK;
    }

    nsresult Cancel() override {
        mWebGL = nullptr;
        return NS_OK;
    }
};

void
WebGLContext::EnqueueUpdateContextLossStatus()
{
    nsCOMPtr<nsIRunnable> task = new UpdateContextLossStatusTask(this);
    NS_DispatchToCurrentThread(task);
}

// We use this timer for many things. Here are the things that it is activated for:
// 1) If a script is using the MOZ_WEBGL_lose_context extension.
// 2) If we are using EGL and _NOT ANGLE_, we query periodically to see if the
//    CONTEXT_LOST_WEBGL error has been triggered.
// 3) If we are using ANGLE, or anything that supports ARB_robustness, query the
//    GPU periodically to see if the reset status bit has been set.
// In all of these situations, we use this timer to send the script context lost
// and restored events asynchronously. For example, if it triggers a context loss,
// the webglcontextlost event will be sent to it the next time the robustness timer
// fires.
// Note that this timer mechanism is not used unless one of these 3 criteria
// are met.
// At a bare minimum, from context lost to context restores, it would take 3
// full timer iterations: detection, webglcontextlost, webglcontextrestored.
void
WebGLContext::UpdateContextLossStatus()
{
    if (!mCanvasElement && !mOffscreenCanvas) {
        // the canvas is gone. That happens when the page was closed before we got
        // this timer event. In this case, there's nothing to do here, just don't crash.
        return;
    }
    if (mContextStatus == ContextNotLost) {
        // We don't know that we're lost, but we might be, so we need to
        // check. If we're guilty, don't allow restores, though.

        bool isGuilty = true;
        MOZ_ASSERT(gl); // Shouldn't be missing gl if we're NotLost.
        bool isContextLost = CheckContextLost(gl, &isGuilty);

        if (isContextLost) {
            if (isGuilty)
                mAllowContextRestore = false;

            ForceLoseContext();
        }

        // Fall through.
    }

    if (mContextStatus == ContextLostAwaitingEvent) {
        // The context has been lost and we haven't yet triggered the
        // callback, so do that now.
        const auto kEventName = NS_LITERAL_STRING("webglcontextlost");
        const bool kCanBubble = true;
        const bool kIsCancelable = true;
        bool useDefaultHandler;

        if (mCanvasElement) {
            nsContentUtils::DispatchTrustedEvent(
                mCanvasElement->OwnerDoc(),
                static_cast<nsIDOMHTMLCanvasElement*>(mCanvasElement),
                kEventName,
                kCanBubble,
                kIsCancelable,
                &useDefaultHandler);
        } else {
            // OffscreenCanvas case
            RefPtr<Event> event = new Event(mOffscreenCanvas, nullptr, nullptr);
            event->InitEvent(kEventName, kCanBubble, kIsCancelable);
            event->SetTrusted(true);
            mOffscreenCanvas->DispatchEvent(event, &useDefaultHandler);
        }

        // We sent the callback, so we're just 'regular lost' now.
        mContextStatus = ContextLost;
        // If we're told to use the default handler, it means the script
        // didn't bother to handle the event. In this case, we shouldn't
        // auto-restore the context.
        if (useDefaultHandler)
            mAllowContextRestore = false;

        // Fall through.
    }

    if (mContextStatus == ContextLost) {
        // Context is lost, and we've already sent the callback. We
        // should try to restore the context if we're both allowed to,
        // and supposed to.

        // Are we allowed to restore the context?
        if (!mAllowContextRestore)
            return;

        // If we're only simulated-lost, we shouldn't auto-restore, and
        // instead we should wait for restoreContext() to be called.
        if (mLastLossWasSimulated)
            return;

        // Restore when the app is visible
        if (mRestoreWhenVisible)
            return;

        ForceRestoreContext();
        return;
    }

    if (mContextStatus == ContextLostAwaitingRestore) {
        // Context is lost, but we should try to restore it.

        if (!mAllowContextRestore) {
            // We might decide this after thinking we'd be OK restoring
            // the context, so downgrade.
            mContextStatus = ContextLost;
            return;
        }

        if (!TryToRestoreContext()) {
            // Failed to restore. Try again later.
            mContextLossHandler.RunTimer();
            return;
        }

        // Revival!
        mContextStatus = ContextNotLost;

        if (mCanvasElement) {
            nsContentUtils::DispatchTrustedEvent(
                mCanvasElement->OwnerDoc(),
                static_cast<nsIDOMHTMLCanvasElement*>(mCanvasElement),
                NS_LITERAL_STRING("webglcontextrestored"),
                true,
                true);
        } else {
            RefPtr<Event> event = new Event(mOffscreenCanvas, nullptr, nullptr);
            event->InitEvent(NS_LITERAL_STRING("webglcontextrestored"), true, true);
            event->SetTrusted(true);
            bool unused;
            mOffscreenCanvas->DispatchEvent(event, &unused);
        }

        mEmitContextLostErrorOnce = true;
        return;
    }
}

void
WebGLContext::ForceLoseContext(bool simulateLosing)
{
    printf_stderr("WebGL(%p)::ForceLoseContext\n", this);
    MOZ_ASSERT(!IsContextLost());
    mContextStatus = ContextLostAwaitingEvent;
    mContextLostErrorSet = false;

    // Burn it all!
    DestroyResourcesAndContext();
    mLastLossWasSimulated = simulateLosing;

    // Queue up a task, since we know the status changed.
    EnqueueUpdateContextLossStatus();
}

void
WebGLContext::ForceRestoreContext()
{
    printf_stderr("WebGL(%p)::ForceRestoreContext\n", this);
    mContextStatus = ContextLostAwaitingRestore;
    mAllowContextRestore = true; // Hey, you did say 'force'.

    // Queue up a task, since we know the status changed.
    EnqueueUpdateContextLossStatus();
}

void
WebGLContext::MakeContextCurrent() const
{
    gl->MakeCurrent();
}

already_AddRefed<mozilla::gfx::SourceSurface>
WebGLContext::GetSurfaceSnapshot(bool* out_premultAlpha)
{
    if (!gl)
        return nullptr;

    gl->MakeCurrent();

    if (!PrepareDefaultReadFB("GetSurfaceSnapshot"))
        return nullptr;

    const gfx::IntSize size(mWidth, mHeight);

    RefPtr<DataSourceSurface> surf;
    surf = Factory::CreateDataSourceSurfaceWithStride(size, SurfaceFormat::R8G8B8A8,
                                                      mWidth * 4);
    if (NS_WARN_IF(!surf))
        return nullptr;

    gl->MakeCurrent();
    {
        const ScopedBindFramebuffer autoFB(gl, DefaultReadFB());
        ReadPixelsIntoDataSurface(gl, surf);
    }

    if (out_premultAlpha) {
        *out_premultAlpha = true;
    }
    bool srcPremultAlpha = mOptions.premultipliedAlpha;
    if (!srcPremultAlpha) {
        if (out_premultAlpha) {
            *out_premultAlpha = false;
        } else if(mOptions.alpha) {
            gfxUtils::PremultiplyDataSurface(surf, surf);
        }
    }

    const auto backend = gfxPlatform::GetPlatform()->GetSoftwareBackend();
    const RefPtr<DrawTarget> dt = Factory::CreateDrawTarget(backend, size,
                                                            SurfaceFormat::B8G8R8A8);
    if (!dt)
        return nullptr;

    dt->SetTransform(Matrix::Translation(0.0, mHeight).PreScale(1.0, -1.0));

    dt->DrawSurface(surf,
                    Rect(0, 0, mWidth, mHeight),
                    Rect(0, 0, mWidth, mHeight),
                    DrawSurfaceOptions(),
                    DrawOptions(1.0f, CompositionOp::OP_SOURCE));

    return dt->Snapshot();
}

void
WebGLContext::DidRefresh()
{
    if (gl) {
        gl->FlushIfHeavyGLCallsSinceLastFlush();
    }
}

bool
WebGLContext::ValidateCurFBForRead(const char* funcName,
                                   const webgl::FormatUsageInfo** const out_format,
                                   uint32_t* const out_width, uint32_t* const out_height)
{
    if (!mBoundReadFramebuffer) {
        if (mDefaultFB_ReadBuffer == LOCAL_GL_NONE) {
            ErrorInvalidOperation("%s: Can't read from backbuffer when readBuffer mode is"
                                  " NONE.",
                                  funcName);
            return false;
        }

        // FIXME - here we're assuming that the default framebuffer is backed by
        // UNSIGNED_BYTE that might not always be true, say if we had a 16bpp default
        // framebuffer.
        auto effFormat = mOptions.alpha ? webgl::EffectiveFormat::RGBA8
                                        : webgl::EffectiveFormat::RGB8;

        *out_format = mFormatUsage->GetUsage(effFormat);
        MOZ_ASSERT(*out_format);

        *out_width = mWidth;
        *out_height = mHeight;
        return true;
    }

    return mBoundReadFramebuffer->ValidateForRead(funcName, out_format, out_width,
                                                  out_height);
}

////////////////////////////////////////////////////////////////////////////////

IndexedBufferBinding::IndexedBufferBinding()
    : mRangeStart(0)
    , mRangeSize(0)
{ }

uint64_t
IndexedBufferBinding::ByteCount() const
{
    if (!mBufferBinding)
        return 0;

    uint64_t bufferSize = mBufferBinding->ByteLength();
    if (!mRangeSize) // BindBufferBase
        return bufferSize;

    if (mRangeStart >= bufferSize)
        return 0;
    bufferSize -= mRangeStart;

    return std::min(bufferSize, mRangeSize);
}

////////////////////////////////////////

ScopedUnpackReset::ScopedUnpackReset(WebGLContext* webgl)
    : ScopedGLWrapper<ScopedUnpackReset>(webgl->gl)
    , mWebGL(webgl)
{
    if (mWebGL->mPixelStore_UnpackAlignment != 4) mGL->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 4);

    if (mWebGL->IsWebGL2()) {
        if (mWebGL->mPixelStore_UnpackRowLength   != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH  , 0);
        if (mWebGL->mPixelStore_UnpackImageHeight != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_IMAGE_HEIGHT, 0);
        if (mWebGL->mPixelStore_UnpackSkipPixels  != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_PIXELS , 0);
        if (mWebGL->mPixelStore_UnpackSkipRows    != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_ROWS   , 0);
        if (mWebGL->mPixelStore_UnpackSkipImages  != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES , 0);

        if (mWebGL->mBoundPixelUnpackBuffer) mGL->fBindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, 0);
    }
}

void
ScopedUnpackReset::UnwrapImpl()
{
    mGL->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, mWebGL->mPixelStore_UnpackAlignment);

    if (mWebGL->IsWebGL2()) {
        mGL->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH  , mWebGL->mPixelStore_UnpackRowLength  );
        mGL->fPixelStorei(LOCAL_GL_UNPACK_IMAGE_HEIGHT, mWebGL->mPixelStore_UnpackImageHeight);
        mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_PIXELS , mWebGL->mPixelStore_UnpackSkipPixels );
        mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_ROWS   , mWebGL->mPixelStore_UnpackSkipRows   );
        mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES , mWebGL->mPixelStore_UnpackSkipImages );

        GLuint pbo = 0;
        if (mWebGL->mBoundPixelUnpackBuffer) {
            pbo = mWebGL->mBoundPixelUnpackBuffer->mGLName;
        }

        mGL->fBindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, pbo);
    }
}

////////////////////

void
ScopedFBRebinder::UnwrapImpl()
{
    const auto fnName = [&](WebGLFramebuffer* fb) {
        return fb ? fb->mGLName : 0;
    };

    if (mWebGL->IsWebGL2()) {
        mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fnName(mWebGL->mBoundDrawFramebuffer));
        mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, fnName(mWebGL->mBoundReadFramebuffer));
    } else {
        MOZ_ASSERT(mWebGL->mBoundDrawFramebuffer == mWebGL->mBoundReadFramebuffer);
        mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fnName(mWebGL->mBoundDrawFramebuffer));
    }
}

////////////////////

static GLenum
TargetIfLazy(GLenum target)
{
    switch (target) {
    case LOCAL_GL_PIXEL_PACK_BUFFER:
    case LOCAL_GL_PIXEL_UNPACK_BUFFER:
        return target;

    default:
        return 0;
    }
}

ScopedLazyBind::ScopedLazyBind(gl::GLContext* gl, GLenum target, const WebGLBuffer* buf)
    : ScopedGLWrapper<ScopedLazyBind>(gl)
    , mTarget(buf ? TargetIfLazy(target) : 0)
    , mBuf(buf)
{
    if (mTarget) {
        mGL->fBindBuffer(mTarget, mBuf->mGLName);
    }
}

void
ScopedLazyBind::UnwrapImpl()
{
    if (mTarget) {
        mGL->fBindBuffer(mTarget, 0);
    }
}

////////////////////////////////////////

bool
Intersect(const int32_t srcSize, const int32_t read0, const int32_t readSize,
          int32_t* const out_intRead0, int32_t* const out_intWrite0,
          int32_t* const out_intSize)
{
    MOZ_ASSERT(srcSize >= 0);
    MOZ_ASSERT(readSize >= 0);
    const auto read1 = int64_t(read0) + readSize;

    int32_t intRead0 = read0; // Clearly doesn't need validation.
    int64_t intWrite0 = 0;
    int64_t intSize = readSize;

    if (read1 <= 0 || read0 >= srcSize) {
        // Disjoint ranges.
        intSize = 0;
    } else {
        if (read0 < 0) {
            const auto diff = int64_t(0) - read0;
            MOZ_ASSERT(diff >= 0);
            intRead0 = 0;
            intWrite0 = diff;
            intSize -= diff;
        }
        if (read1 > srcSize) {
            const auto diff = int64_t(read1) - srcSize;
            MOZ_ASSERT(diff >= 0);
            intSize -= diff;
        }

        if (!CheckedInt<int32_t>(intWrite0).isValid() ||
            !CheckedInt<int32_t>(intSize).isValid())
        {
            return false;
        }
    }

    *out_intRead0 = intRead0;
    *out_intWrite0 = intWrite0;
    *out_intSize = intSize;
    return true;
}

////////////////////////////////////////////////////////////////////////////////

CheckedUint32
WebGLContext::GetUnpackSize(bool isFunc3D, uint32_t width, uint32_t height,
                            uint32_t depth, uint8_t bytesPerPixel)
{
    if (!width || !height || !depth)
        return 0;

    ////////////////

    const auto& maybeRowLength = mPixelStore_UnpackRowLength;
    const auto& maybeImageHeight = mPixelStore_UnpackImageHeight;

    const auto usedPixelsPerRow = CheckedUint32(mPixelStore_UnpackSkipPixels) + width;
    const auto stridePixelsPerRow = (maybeRowLength ? CheckedUint32(maybeRowLength)
                                                    : usedPixelsPerRow);

    const auto usedRowsPerImage = CheckedUint32(mPixelStore_UnpackSkipRows) + height;
    const auto strideRowsPerImage = (maybeImageHeight ? CheckedUint32(maybeImageHeight)
                                                      : usedRowsPerImage);

    const uint32_t skipImages = (isFunc3D ? mPixelStore_UnpackSkipImages
                                          : 0);
    const CheckedUint32 usedImages = CheckedUint32(skipImages) + depth;

    ////////////////

    CheckedUint32 strideBytesPerRow = bytesPerPixel * stridePixelsPerRow;
    strideBytesPerRow = RoundUpToMultipleOf(strideBytesPerRow,
                                            mPixelStore_UnpackAlignment);

    const CheckedUint32 strideBytesPerImage = strideBytesPerRow * strideRowsPerImage;

    ////////////////

    CheckedUint32 usedBytesPerRow = bytesPerPixel * usedPixelsPerRow;
    // Don't round this to the alignment, since alignment here is really just used for
    // establishing stride, particularly in WebGL 1, where you can't set ROW_LENGTH.

    CheckedUint32 totalBytes = strideBytesPerImage * (usedImages - 1);
    totalBytes += strideBytesPerRow * (usedRowsPerImage - 1);
    totalBytes += usedBytesPerRow;

    return totalBytes;
}

already_AddRefed<layers::SharedSurfaceTextureClient>
WebGLContext::GetVRFrame()
{
    const auto& vrmc = VRManagerChild::Get();
    if (!vrmc)
        return nullptr;

    MOZ_ASSERT(mSurfFactory->mAllocator == vrmc->GetTextureForwarder());

    PresentScreenBuffer();

    auto texClient = FrontBuffer();
    if (!texClient)
        return nullptr;

    if (texClient->GetAllocator() != vrmc) {
        texClient = mSurfFactory->CloneTexClient(texClient->Surf());
    }
    return texClient.forget();
}

bool
WebGLContext::StartVRPresentation()
{
    const auto& vrmc = VRManagerChild::Get();
    if (!vrmc)
        return false;

    const bool ok = mSurfFactory.Morph(vrmc, true);
    MOZ_ASSERT(ok);
    return ok;
}

////////////////////////////////////////////////////////////////////////////////

static inline size_t
SizeOfViewElem(const dom::ArrayBufferView& view)
{
    const auto& elemType = view.Type();
    if (elemType == js::Scalar::MaxTypedArrayViewType) // DataViews.
        return 1;

    return js::Scalar::byteSize(elemType);
}

bool
WebGLContext::ValidateArrayBufferView(const char* funcName,
                                      const dom::ArrayBufferView& view, GLuint elemOffset,
                                      GLuint elemCountOverride, uint8_t** const out_bytes,
                                      size_t* const out_byteLen)
{
    view.ComputeLengthAndData();
    uint8_t* const bytes = view.DataAllowShared();
    const size_t byteLen = view.LengthAllowShared();

    const auto& elemSize = SizeOfViewElem(view);

    size_t elemCount = byteLen / elemSize;
    if (elemOffset > elemCount) {
        ErrorInvalidValue("%s: Invalid offset into ArrayBufferView.", funcName);
        return false;
    }
    elemCount -= elemOffset;

    if (elemCountOverride) {
        if (elemCountOverride > elemCount) {
            ErrorInvalidValue("%s: Invalid sub-length for ArrayBufferView.", funcName);
            return false;
        }
        elemCount = elemCountOverride;
    }

    *out_bytes = bytes + (elemOffset * elemSize);
    *out_byteLen = elemCount * elemSize;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// XPCOM goop

void
ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& callback,
                            const std::vector<IndexedBufferBinding>& field,
                            const char* name, uint32_t flags)
{
    for (const auto& cur : field) {
        ImplCycleCollectionTraverse(callback, cur.mBufferBinding, name, flags);
    }
}

void
ImplCycleCollectionUnlink(std::vector<IndexedBufferBinding>& field)
{
    field.clear();
}

////

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebGLContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebGLContext)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLContext,
  mCanvasElement,
  mOffscreenCanvas,
  mExtensions,
  mBound2DTextures,
  mBoundCubeMapTextures,
  mBound3DTextures,
  mBound2DArrayTextures,
  mBoundSamplers,
  mBoundArrayBuffer,
  mBoundCopyReadBuffer,
  mBoundCopyWriteBuffer,
  mBoundPixelPackBuffer,
  mBoundPixelUnpackBuffer,
  mBoundTransformFeedback,
  mBoundUniformBuffer,
  mCurrentProgram,
  mBoundDrawFramebuffer,
  mBoundReadFramebuffer,
  mBoundRenderbuffer,
  mBoundVertexArray,
  mDefaultVertexArray,
  mQuerySlot_SamplesPassed,
  mQuerySlot_TFPrimsWritten,
  mQuerySlot_TimeElapsed)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebGLContext)
    NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
    NS_INTERFACE_MAP_ENTRY(nsIDOMWebGLRenderingContext)
    NS_INTERFACE_MAP_ENTRY(nsICanvasRenderingContextInternal)
    NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
    // If the exact way we cast to nsISupports here ever changes, fix our
    // ToSupports() method.
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMWebGLRenderingContext)
NS_INTERFACE_MAP_END

} // namespace mozilla
