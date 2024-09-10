/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MacIOSurfaceTextureClientOGL.h"
#include "mozilla/gfx/MacIOSurface.h" 
#include "MacIOSurfaceHelpers.h"

namespace mozilla {
namespace layers {

MacIOSurfaceTextureData::MacIOSurfaceTextureData(MacIOSurface* aSurface)
: mSurface(aSurface)
{
  MOZ_ASSERT(mSurface);
}

MacIOSurfaceTextureData::~MacIOSurfaceTextureData()
{}

// static
MacIOSurfaceTextureData*
MacIOSurfaceTextureData::Create(MacIOSurface* aSurface)
{
  MOZ_ASSERT(aSurface);
  if (!aSurface) {
    return nullptr;
  }
  return new MacIOSurfaceTextureData(aSurface);
}

bool
MacIOSurfaceTextureData::Serialize(SurfaceDescriptor& aOutDescriptor)
{
  aOutDescriptor = SurfaceDescriptorMacIOSurface(mSurface->GetIOSurfaceID(),
                                                 mSurface->GetContentsScaleFactor(),
                                                 !mSurface->HasAlpha());
  return true;
}

void
MacIOSurfaceTextureData::FillInfo(TextureData::Info& aInfo) const
{
  aInfo.size = gfx::IntSize(mSurface->GetDevicePixelWidth(), mSurface->GetDevicePixelHeight());
  aInfo.format = mSurface->GetFormat();
  aInfo.hasIntermediateBuffer = false;
  aInfo.hasSynchronization = false;
  aInfo.supportsMoz2D = false;
  aInfo.canExposeMappedData = false;
}

already_AddRefed<gfx::DataSourceSurface>
MacIOSurfaceTextureData::GetAsSurface()
{
  RefPtr<gfx::SourceSurface> surf = CreateSourceSurfaceFromMacIOSurface(mSurface);
  return surf->GetDataSurface();
}

} // namespace layers
} // namespace mozilla
