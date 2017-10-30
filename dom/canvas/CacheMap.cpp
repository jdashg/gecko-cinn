/* -*- Mode: C++; tab-width: 13; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=13 sts=4 et sw=4 tw=90: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheMap.h"

namespace mozilla {

void
CacheMapInvalidator::InvalidateCaches() const
{
    while (mCacheEntries.size()) {
        const auto& entry = *(mCacheEntries.begin());
        entry->Invalidate();
        MOZ_ASSERT(mCacheEntries.find(entry) == mCacheEntries.end());
    }
}

namespace detail {

CacheMapUntypedEntry::CacheMapUntypedEntry(std::vector<const CacheMapInvalidator*>&& invalidators)
    : mInvalidators(Move(invalidators))
{
    for (const auto& cur : mInvalidators) {
        const auto res = cur->mCacheEntries.insert(this);
        const auto& didInsert = res.second;
        MOZ_ALWAYS_TRUE( didInsert );
    }
}

CacheMapUntypedEntry::~CacheMapUntypedEntry()
{
    for (const auto& cur : mInvalidators) {
        const auto erased = cur->mCacheEntries.erase(this);
        MOZ_ALWAYS_TRUE( erased == 1 );
    }
}

} // namespace detail

} // namespace mozilla
