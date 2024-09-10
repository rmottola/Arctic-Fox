/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TILEDREGION_H_
#define MOZILLA_GFX_TILEDREGION_H_

#include "mozilla/ArrayView.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/Move.h"
#include "nsRegion.h"
#include "pixman.h"

namespace mozilla {
namespace gfx {

// See TiledRegion.cpp for documentation on TiledRegionImpl.
class TiledRegionImpl {
public:
  void Clear() { mRects.Clear(); }
  bool AddRect(const pixman_box32_t& aRect);
  bool Intersects(const pixman_box32_t& aRect) const;
  bool Contains(const pixman_box32_t& aRect) const;
  operator ArrayView<pixman_box32_t>() const { return ArrayView<pixman_box32_t>(mRects); }

private:
  nsTArray<pixman_box32_t> mRects;
};

/**
 * A auto-simplifying region type that supports one rectangle per tile.
 * The virtual tile grid is anchored at (0, 0) and has quadratic tiles whose
 * size is hard-coded as kTileSize in TiledRegion.cpp.
 * A TiledRegion starts out empty. You can add rectangles or (regular) regions
 * into it by calling Add(). Add() is a mutating union operation (similar to
 * OrWith on nsRegion) that's *not* exact, because it will enlarge the region as
 * necessary to satisfy the "one rectangle per tile" requirement.
 * Tiled regions convert implicitly to the underlying regular region type.
 * The only way to remove parts from a TiledRegion is by calling SetEmpty().
 */
template<typename RegionT>
class TiledRegion {
public:
  typedef typename RegionT::RectType RectT;

  TiledRegion()
    : mCoversBounds(false)
  {}

  TiledRegion(const TiledRegion& aOther)
    : mBounds(aOther.mBounds)
    , mImpl(aOther.mImpl)
    , mCoversBounds(false)
  {}

  TiledRegion(TiledRegion&& aOther)
    : mBounds(aOther.mBounds)
    , mImpl(Move(aOther.mImpl))
    , mCoversBounds(false)
  {}

  RegionT GetRegion() const
  {
    if (mBounds.IsEmpty()) {
      return RegionT();
    }
    if (mCoversBounds) {
      // Rect limit hit or allocation failed, treat as 1 rect.
      return RegionT(mBounds);
    }
    return RegionT(mImpl);
  }

  TiledRegion& operator=(const TiledRegion& aOther)
  {
    if (&aOther != this) {
      mBounds = aOther.mBounds;
      mImpl = aOther.mImpl;
      mCoversBounds = aOther.mCoversBounds;
    }
    return *this;
  }

  void Add(const RectT& aRect)
  {
    if (aRect.IsEmpty()) {
      return;
    }

    mBounds = mBounds.Union(aRect);

    if (mCoversBounds) {
      return;
    }
    if (ExceedsMaximumSize()) {
      FallBackToBounds();
      return;
    }

    if (!mImpl.AddRect(RectToBox(aRect))) {
      FallBackToBounds();
    }
  }

  void Add(const RegionT& aRegion)
  {
    mBounds = mBounds.Union(aRegion.GetBounds());

    if (mCoversBounds) {
      return;
    }
    if (ExceedsMaximumSize()) {
      FallBackToBounds();
      return;
    }

    for (auto iter = aRegion.RectIter(); !iter.Done(); iter.Next()) {
      RectT r = iter.Get();
      MOZ_ASSERT(!r.IsEmpty());
      if (!mImpl.AddRect(RectToBox(r))) {
        FallBackToBounds();
        return;
      }
    }
  }

  bool IsEmpty() const { return mBounds.IsEmpty(); }

  void SetEmpty()
  {
    mBounds.SetEmpty();
    mImpl.Clear();
    mCoversBounds = false;
  }

  RectT GetBounds() const { return mBounds; }

  bool Intersects(const RectT& aRect) const
  {
    if (!mBounds.Intersects(aRect)) {
      return false;
    }
    if (mCoversBounds) {
      return true;
    }

    return mImpl.Intersects(RectToBox(aRect));
  }

  bool Contains(const RectT& aRect) const
  {
    if (!mBounds.Contains(aRect)) {
      return false;
    }
    if (mCoversBounds) {
      return true;
    }
    return mImpl.Contains(RectToBox(aRect));
  }

private:

  bool ExceedsMaximumSize() const
  {
    // This stops us from allocating insane numbers of tiles.
    return mBounds.width >= 50 * 256 || mBounds.height >= 50 * 256;
  }

  void FallBackToBounds()
  {
    mCoversBounds = true;
    mImpl.Clear();
  }

  static pixman_box32_t RectToBox(const RectT& aRect)
  {
    return { aRect.x, aRect.y, aRect.XMost(), aRect.YMost() };
  }

  RectT mBounds;
  TiledRegionImpl mImpl;

  // mCoversBounds is true if we bailed out due to a large number of tiles.
  // mCoversBounds being true means that this TiledRegion is just a simple
  // rectangle (our mBounds).
  // Once set to true, the TiledRegion will stay in this state until SetEmpty
  // is called.
  bool mCoversBounds;
};

typedef TiledRegion<IntRegion> TiledIntRegion;

} // namespace gfx
} // namespace mozilla

#endif /* MOZILLA_GFX_TILEDREGION_H_ */
