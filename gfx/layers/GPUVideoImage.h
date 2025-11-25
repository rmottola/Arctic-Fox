/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_GPU_VIDEO_IMAGE_H
#define GFX_GPU_VIDEO_IMAGE_H

#include "mozilla/RefPtr.h"
#include "ImageContainer.h"
#include "mozilla/layers/GPUVideoTextureClient.h"
#include "mozilla/layers/CompositableClient.h"
#include "mozilla/layers/ImageBridgeChild.h"

namespace mozilla {
namespace dom {
class VideoDecoderManagerChild;
}
namespace layers {

// Image class that refers to a decoded video frame within
// the GPU process.
class GPUVideoImage final : public Image {
public:
  GPUVideoImage(dom::VideoDecoderManagerChild* aManager,
                const SurfaceDescriptorGPUVideo& aSD,
                const gfx::IntSize& aSize)
    : Image(nullptr, ImageFormat::GPU_VIDEO)
    , mSize(aSize)
  {
    // Create the TextureClient immediately since the GPUVideoTextureData
    // is responsible for deallocating the SurfaceDescriptor.
    mTextureClient =
      TextureClient::CreateWithData(new GPUVideoTextureData(aManager, aSD, aSize),
                                    TextureFlags::DEFAULT,
                                    ImageBridgeChild::GetSingleton().get());
  }

  ~GPUVideoImage() override {}

  gfx::IntSize GetSize() override { return mSize; }

  virtual already_AddRefed<gfx::SourceSurface> GetAsSourceSurface() override
  {
    if (!mTextureClient) {
      return nullptr;
    }
    GPUVideoTextureData* data = mTextureClient->GetInternalData()->AsGPUVideoTextureData();
    return data->GetAsSourceSurface();
  }

  virtual TextureClient* GetTextureClient(KnowsCompositor* aForwarder) override
  {
    MOZ_ASSERT(aForwarder == ImageBridgeChild::GetSingleton(), "Must only use GPUVideo on ImageBridge");
    return mTextureClient;
  }

private:
  gfx::IntSize mSize;
  RefPtr<TextureClient> mTextureClient;
};

} // namepace layers
} // namespace mozilla

#endif // GFX_GPU_VIDEO_IMAGE_H
