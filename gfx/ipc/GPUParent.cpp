/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "GPUParent.h"
#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "gfxPrefs.h"
#include "GPUProcessHost.h"
#include "mozilla/Assertions.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "VRManager.h"
#include "VRManagerParent.h"
#include "VsyncBridgeParent.h"
#if defined(XP_WIN)
# include "mozilla/gfx/DeviceManagerD3D11.h"
#endif

namespace mozilla {
namespace gfx {

using namespace ipc;
using namespace layers;

GPUParent::GPUParent()
{
}

GPUParent::~GPUParent()
{
}

bool
GPUParent::Init(base::ProcessId aParentPid,
                MessageLoop* aIOLoop,
                IPC::Channel* aChannel)
{
  if (NS_WARN_IF(!Open(aChannel, aParentPid, aIOLoop))) {
    return false;
  }

  // Ensure gfxPrefs are initialized.
  gfxPrefs::GetSingleton();
  gfxConfig::Init();
  gfxVars::Initialize();
#if defined(XP_WIN)
  DeviceManagerD3D11::Init();
#endif
  CompositorThreadHolder::Start();
  VRManager::ManagerInit();
  gfxPlatform::InitNullMetadata();
  return true;
}

bool
GPUParent::RecvInit(nsTArray<GfxPrefSetting>&& prefs,
                    nsTArray<GfxVarUpdate>&& vars,
                    const DevicePrefs& devicePrefs)
{
  const nsTArray<gfxPrefs::Pref*>& globalPrefs = gfxPrefs::all();
  for (auto& setting : prefs) {
    gfxPrefs::Pref* pref = globalPrefs[setting.index()];
    pref->SetCachedValue(setting.value());
  }
  for (const auto& var : vars) {
    gfxVars::ApplyUpdate(var);
  }

  // Inherit device preferences.
  gfxConfig::Inherit(Feature::HW_COMPOSITING, devicePrefs.hwCompositing());
  gfxConfig::Inherit(Feature::D3D11_COMPOSITING, devicePrefs.d3d11Compositing());
  gfxConfig::Inherit(Feature::D3D9_COMPOSITING, devicePrefs.d3d9Compositing());
  gfxConfig::Inherit(Feature::OPENGL_COMPOSITING, devicePrefs.oglCompositing());
  gfxConfig::Inherit(Feature::DIRECT2D, devicePrefs.useD2D1());

#if defined(XP_WIN)
  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    DeviceManagerD3D11::Get()->CreateCompositorDevices();
  }
#endif

  return true;
}

bool
GPUParent::RecvInitVsyncBridge(Endpoint<PVsyncBridgeParent>&& aVsyncEndpoint)
{
  VsyncBridgeParent::Start(Move(aVsyncEndpoint));
  return true;
}

bool
GPUParent::RecvInitImageBridge(Endpoint<PImageBridgeParent>&& aEndpoint)
{
  ImageBridgeParent::CreateForGPUProcess(Move(aEndpoint));
  return true;
}

bool
GPUParent::RecvInitVRManager(Endpoint<PVRManagerParent>&& aEndpoint)
{
  VRManagerParent::CreateForGPUProcess(Move(aEndpoint));
  return true;
}

bool
GPUParent::RecvUpdatePref(const GfxPrefSetting& setting)
{
  gfxPrefs::Pref* pref = gfxPrefs::all()[setting.index()];
  pref->SetCachedValue(setting.value());
  return true;
}

bool
GPUParent::RecvUpdateVar(const GfxVarUpdate& aUpdate)
{
  gfxVars::ApplyUpdate(aUpdate);
  return true;
}

static void
OpenParent(RefPtr<CompositorBridgeParent> aParent,
           Endpoint<PCompositorBridgeParent>&& aEndpoint)
{
  if (!aParent->Bind(Move(aEndpoint))) {
    MOZ_CRASH("Failed to bind compositor");
  }
}

bool
GPUParent::RecvNewWidgetCompositor(Endpoint<layers::PCompositorBridgeParent>&& aEndpoint,
                                   const CSSToLayoutDeviceScale& aScale,
                                   const TimeDuration& aVsyncRate,
                                   const bool& aUseExternalSurfaceSize,
                                   const IntSize& aSurfaceSize)
{
  RefPtr<CompositorBridgeParent> cbp =
    new CompositorBridgeParent(aScale, aVsyncRate, aUseExternalSurfaceSize, aSurfaceSize);

  MessageLoop* loop = CompositorThreadHolder::Loop();
  loop->PostTask(NewRunnableFunction(OpenParent, cbp, Move(aEndpoint)));
  return true;
}

bool
GPUParent::RecvNewContentCompositorBridge(Endpoint<PCompositorBridgeParent>&& aEndpoint)
{
  return CompositorBridgeParent::CreateForContent(Move(aEndpoint));
}

bool
GPUParent::RecvNewContentImageBridge(Endpoint<PImageBridgeParent>&& aEndpoint)
{
  return ImageBridgeParent::CreateForContent(Move(aEndpoint));
}

bool
GPUParent::RecvNewContentVRManager(Endpoint<PVRManagerParent>&& aEndpoint)
{
  return VRManagerParent::CreateForContent(Move(aEndpoint));
}

bool
GPUParent::RecvDeallocateLayerTreeId(const uint64_t& aLayersId)
{
  CompositorBridgeParent::DeallocateLayerTreeId(aLayersId);
  return true;
}

void
GPUParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Shutting down GPU process early due to a crash!");
    ProcessChild::QuickExit();
  }

#ifndef NS_FREE_PERMANENT_DATA
  // No point in going through XPCOM shutdown because we don't keep persistent
  // state. Currently we quick-exit in RecvBeginShutdown so this should be
  // unreachable.
  ProcessChild::QuickExit();
#endif

  if (mVsyncBridge) {
    mVsyncBridge->Shutdown();
  }
  CompositorThreadHolder::Shutdown();
#if defined(XP_WIN)
  DeviceManagerD3D11::Shutdown();
#endif
  gfxVars::Shutdown();
  gfxConfig::Shutdown();
  gfxPrefs::DestroySingleton();
  XRE_ShutdownChildProcess();
}

} // namespace gfx
} // namespace mozilla
