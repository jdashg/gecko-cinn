/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LOAD_SYMBOLS_H_
#define LOAD_SYMBOLS_H_

#include "GLDefs.h"
#include "nscore.h"
#include "prlink.h"

namespace mozilla {
namespace gl {

typedef PRFuncPtr (GLAPIENTRY *pfnGetProcAddressT)(const char*);

struct SymLoadStruct final
{
    PRFuncPtr* const out_symPointer;
    const char* const symNames[5];

    void Load(pfnGetProcAddressT pfnLookup) const;
};

bool
LoadSymbols(pfnGetProcAddressT pfnLookup, const SymLoadStruct* structList,
            bool warnOnFailures);

} // namespace gl
} // namespace mozilla

#endif // LOAD_SYMBOLS_H_
