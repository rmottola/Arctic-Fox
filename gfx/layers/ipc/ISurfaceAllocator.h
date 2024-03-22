/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERS_ISURFACEDEALLOCATOR
#define GFX_LAYERS_ISURFACEDEALLOCATOR

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t
#include "gfxTypes.h"
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/RefPtr.h"
#include "nsIMemoryReporter.h"          // for nsIMemoryReporter
#include "mozilla/Atomics.h"            // for Atomic
#include "mozilla/layers/LayersMessages.h" // for ShmemSection
#include "LayersTypes.h"
#include "gfxPrefs.h"
#include "mozilla/layers/AtomicRefCountedWithFinalize.h"

/*
 * FIXME [bjacob] *** PURE CRAZYNESS WARNING ***
 * (I think that this doesn't apply anymore.)
 *
 * This #define is actually needed here, because subclasses of ISurfaceAllocator,
 * namely ShadowLayerForwarder, will or will not override AllocGrallocBuffer
 * depending on whether MOZ_HAVE_SURFACEDESCRIPTORGRALLOC is defined.
 */
#ifdef MOZ_WIDGET_GONK
#define MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
#endif

namespace mozilla {
namespace ipc {
class Shmem;
} // namespace ipc
namespace gfx {
class DataSourceSurface;
} // namespace gfx

namespace layers {

class CompositableForwarder;
class ShadowLayerForwarder;

class ShmemAllocator;
class ShmemSectionAllocator;
class LegacySurfaceDescriptorAllocator;
class ClientIPCAllocator;

enum BufferCapabilities {
  DEFAULT_BUFFER_CAPS = 0,
  /**
   * The allocated buffer must be efficiently mappable as a DataSourceSurface.
   */
  MAP_AS_IMAGE_SURFACE = 1 << 0,
  /**
   * The allocated buffer will be used for GL rendering only
   */
  USING_GL_RENDERING_ONLY = 1 << 1
};

class SurfaceDescriptor;


mozilla::ipc::SharedMemory::SharedMemoryType OptimalShmemType();

/**
 * An interface used to create and destroy surfaces that are shared with the
 * Compositor process (using shmem, or gralloc, or other platform specific memory)
 *
 * Most of the methods here correspond to methods that are implemented by IPDL
 * actors without a common polymorphic interface.
 * These methods should be only called in the ipdl implementor's thread, unless
 * specified otherwise in the implementing class.
 */
class ISurfaceAllocator : public AtomicRefCountedWithFinalize<ISurfaceAllocator>
{
public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(ISurfaceAllocator)

  // down-casting

  virtual ShmemAllocator* AsShmemAllocator() { return nullptr; }

  virtual ShmemSectionAllocator* AsShmemSectionAllocator() { return nullptr; }

  virtual CompositableForwarder* AsCompositableForwarder() { return nullptr; }

  virtual ShadowLayerForwarder* AsLayerForwarder() { return nullptr; }

  virtual ClientIPCAllocator* AsClientAllocator() { return nullptr; }

  virtual LegacySurfaceDescriptorAllocator*
  AsLegacySurfaceDescriptorAllocator() { return nullptr; }

  // ipc info

  virtual bool IPCOpen() const { return true; }

  virtual bool IsSameProcess() const = 0;

  virtual bool UsesImageBridge() const { return false; }

protected:
  void Finalize() {}

  virtual ~ISurfaceAllocator() {}

  friend class AtomicRefCountedWithFinalize<ISurfaceAllocator>;
};

/// Methods that are specific to the client/child side.
class ClientIPCAllocator : public ISurfaceAllocator
{
public:
  virtual ClientIPCAllocator* AsClientAllocator() override { return this; }

  virtual MessageLoop * GetMessageLoop() const = 0;

  virtual int32_t GetMaxTextureSize() const { return gfxPrefs::MaxTextureSize(); }
};

/// An allocator can provide shared memory.
///
/// The allocated shmems can be deallocated on either process, as long as they
/// belong to the same channel.
class ShmemAllocator
{
public:
  virtual bool AllocShmem(size_t aSize,
                          mozilla::ipc::SharedMemory::SharedMemoryType aShmType,
                          mozilla::ipc::Shmem* aShmem) = 0;
  virtual bool AllocUnsafeShmem(size_t aSize,
                                mozilla::ipc::SharedMemory::SharedMemoryType aShmType,
                                mozilla::ipc::Shmem* aShmem) = 0;
  virtual void DeallocShmem(mozilla::ipc::Shmem& aShmem) = 0;
};

/// An allocator that can group allocations in bigger chunks of shared memory.
///
/// The allocated shmem sections can only be deallocated by the same allocator
/// instance (and only in the child process).
class ShmemSectionAllocator
{
public:
  virtual bool AllocShmemSection(uint32_t aSize, ShmemSection* aShmemSection) = 0;

  virtual void DeallocShmemSection(ShmemSection& aShmemSection) = 0;

  virtual void MemoryPressure() {}
};

/// Some old stuff that's still around and used for screenshots.
///
/// New code should not need this (see TextureClient).
class LegacySurfaceDescriptorAllocator
{
public:
  virtual bool AllocSurfaceDescriptor(const gfx::IntSize& aSize,
                                      gfxContentType aContent,
                                      SurfaceDescriptor* aBuffer) = 0;

  virtual bool AllocSurfaceDescriptorWithCaps(const gfx::IntSize& aSize,
                                              gfxContentType aContent,
                                              uint32_t aCaps,
                                              SurfaceDescriptor* aBuffer) = 0;

  virtual void DestroySurfaceDescriptor(SurfaceDescriptor* aSurface) = 0;
};

already_AddRefed<gfx::DrawTarget>
GetDrawTargetForDescriptor(const SurfaceDescriptor& aDescriptor, gfx::BackendType aBackend);

already_AddRefed<gfx::DataSourceSurface>
GetSurfaceForDescriptor(const SurfaceDescriptor& aDescriptor);

uint8_t*
GetAddressFromDescriptor(const SurfaceDescriptor& aDescriptor);

class GfxMemoryImageReporter final : public nsIMemoryReporter
{
  ~GfxMemoryImageReporter() {}

public:
  NS_DECL_ISUPPORTS

  GfxMemoryImageReporter()
  {
#ifdef DEBUG
    // There must be only one instance of this class, due to |sAmount|
    // being static.
    static bool hasRun = false;
    MOZ_ASSERT(!hasRun);
    hasRun = true;
#endif
  }

  MOZ_DEFINE_MALLOC_SIZE_OF_ON_ALLOC(MallocSizeOfOnAlloc)
  MOZ_DEFINE_MALLOC_SIZE_OF_ON_FREE(MallocSizeOfOnFree)

  static void DidAlloc(void* aPointer)
  {
    sAmount += MallocSizeOfOnAlloc(aPointer);
  }

  static void WillFree(void* aPointer)
  {
    sAmount -= MallocSizeOfOnFree(aPointer);
  }

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override
  {
    return MOZ_COLLECT_REPORT(
      "explicit/gfx/heap-textures", KIND_HEAP, UNITS_BYTES, sAmount,
      "Heap memory shared between threads by texture clients and hosts.");
  }

private:
  // Typically we use |size_t| in memory reporters, but in the past this
  // variable has sometimes gone negative due to missing DidAlloc() calls.
  // Therefore, we use a signed type so that any such negative values show up
  // as negative in about:memory, rather than as enormous positive numbers.
  static mozilla::Atomic<ptrdiff_t> sAmount;
};

} // namespace layers
} // namespace mozilla

#endif
