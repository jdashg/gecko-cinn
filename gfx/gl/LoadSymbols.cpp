/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LoadSymbols.h"

#include "mozilla/Assertions.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace gl {

void
SymLoadStruct::Load(const pfnGetProcAddressT pfnLookup) const
{
    MOZ_ASSERT(pfnLookup);
    MOZ_ASSERT(out_symPointer);

    for (auto nameItr = symNames; *nameItr; ++nameItr) {
        const auto& name = *nameItr;
        *out_symPointer = pfnLookup(name);
        if (*out_symPointer)
            return;
    }

    *out_symPointer = nullptr;
}

bool
LoadSymbols(const pfnGetProcAddressT pfnLookup, const SymLoadStruct* const structList,
            const bool warnOnFailures)
{
    MOZ_ASSERT(pfnLookup);

    bool ok = true;
    for (auto cur = structList; cur->out_symPointer; ++cur) {
        cur->Load(pfnLookup);
        if (*cur->out_symPointer)
            continue;

        if (warnOnFailures) {
            const nsPrintfCString err("Can't find symbol '%s'.", cur->symNames[0]);
            NS_WARNING(err.BeginReading());
        }

        ok = false;
    }

    return ok;
}

} // namespace gl
} // namespace mozilla
