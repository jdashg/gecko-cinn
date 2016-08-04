/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLMemoryTracker.h"

#include "WebGLContext.h"

namespace mozilla {

struct Accum
{
    uint64_t contexts;
    uint64_t objects;
    uint64_t heapMemory;
    uint64_t gpuMemory;

    Accum() {
        memset(this, 0, sizeof(*this));
    }
};

NS_IMETHODIMP
WebGLMemoryTracker::CollectReports(nsIHandleReportCallback* handleReport,
                                   nsISupports* data, bool)
{
    Accum living, dead;

    for (const auto& context : Contexts()) {
        auto& accum = (context->IsContextLost() ? dead : living);
        accum.contexts++;

        const auto fnCollectObj = [&](WebGLContextBoundObject* cur) {
            accum.objects++;
            accum.heapMemory += cur->HeapMemory();
            accum.gpuMemory += cur->GPUMemory();
        };

        for (const auto& cur : context->mGenerationObjects) {
            fnCollectObj(cur);
        }
        for (const auto& cur : context->mPermanentObjects) {
            fnCollectObj(cur);
        }
    }

#define REPORT(_path, _kind, _units, _amount, _desc)                         \
    do {                                                                     \
      nsresult rv;                                                           \
      rv = handleReport->Callback(EmptyCString(), NS_LITERAL_CSTRING(_path), \
                                   _kind, _units, _amount,                   \
                                   NS_LITERAL_CSTRING(_desc), data);         \
      NS_ENSURE_SUCCESS(rv, rv);                                             \
    } while (0)

    REPORT("webgl-living-context-count", KIND_OTHER, UNITS_COUNT, living.contexts,
           "Number of living (non-lost) WebGL contexts.");

    REPORT("webgl-living-object-count", KIND_OTHER, UNITS_COUNT, living.objects,
           "Number of objects attached to living (non-lost) WebGL contexts.");

    REPORT("webgl-living-heap-memory", KIND_HEAP, UNITS_BYTES, living.heapMemory,
           "Heap memory used by living (non-lost) WebGL contexts.");

    REPORT("webgl-living-gpu-memory", KIND_OTHER, UNITS_BYTES, living.gpuMemory,
           "Estimate of GPU memory used by living (non-lost) WebGL contexts.");


    REPORT("webgl-dead-context-count", KIND_OTHER, UNITS_COUNT, dead.contexts,
           "Number of dead (lost) WebGL contexts.");

    REPORT("webgl-dead-object-count", KIND_OTHER, UNITS_COUNT, dead.objects,
           "Number of objects attached to dead (lost) WebGL contexts.");

    REPORT("webgl-dead-heap-memory", KIND_HEAP, UNITS_BYTES, dead.heapMemory,
           "Heap memory used by dead (lost) WebGL contexts.");

    REPORT("webgl-dead-gpu-memory", KIND_OTHER, UNITS_BYTES, dead.gpuMemory,
           "Estimate of GPU memory used by dead (lost) WebGL contexts.");

#undef REPORT

    return NS_OK;
}

NS_IMPL_ISUPPORTS(WebGLMemoryTracker, nsIMemoryReporter)

StaticRefPtr<WebGLMemoryTracker> WebGLMemoryTracker::sUniqueInstance;

WebGLMemoryTracker*
WebGLMemoryTracker::UniqueInstance()
{
    if (!sUniqueInstance) {
        sUniqueInstance = new WebGLMemoryTracker;
        sUniqueInstance->InitMemoryReporter();
    }
    return sUniqueInstance;
}

WebGLMemoryTracker::WebGLMemoryTracker()
{
}

void
WebGLMemoryTracker::InitMemoryReporter()
{
    RegisterWeakMemoryReporter(this);
}

WebGLMemoryTracker::~WebGLMemoryTracker()
{
    UnregisterWeakMemoryReporter(this);
}

} // namespace mozilla
