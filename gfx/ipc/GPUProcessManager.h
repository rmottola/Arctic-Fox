/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_mozilla_gfx_ipc_GPUProcessManager_h_
#define _include_mozilla_gfx_ipc_GPUProcessManager_h_

#include "base/basictypes.h"
#include "base/process.h"
#include "Units.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/ipc/Transport.h"

namespace mozilla {
namespace layers {
class APZCTreeManager;
class CompositorSession;
class ClientLayerManager;
class CompositorUpdateObserver;
class PCompositorBridgeParent;
} // namespace layers
namespace widget {
class CompositorWidgetProxy;
} // namespace widget
namespace dom {
class ContentParent;
class TabParent;
} // namespace dom
namespace ipc {
class GeckoChildProcessHost;
} // namespace ipc
namespace gfx {

// The GPUProcessManager is a singleton responsible for creating GPU-bound
// objects that may live in another process. Currently, it provides access
// to the compositor via CompositorBridgeParent.
class GPUProcessManager final
{
  typedef layers::APZCTreeManager APZCTreeManager;
  typedef layers::CompositorUpdateObserver CompositorUpdateObserver;

public:
  static void Initialize();
  static void Shutdown();
  static GPUProcessManager* Get();

  ~GPUProcessManager();

  already_AddRefed<layers::CompositorSession> CreateTopLevelCompositor(
    widget::CompositorWidgetProxy* aProxy,
    layers::ClientLayerManager* aLayerManager,
    CSSToLayoutDeviceScale aScale,
    bool aUseAPZ,
    bool aUseExternalSurfaceSize,
    int aSurfaceWidth,
    int aSurfaceHeight);

  layers::PCompositorBridgeParent* CreateTabCompositorBridge(
    ipc::Transport* aTransport,
    base::ProcessId aOtherProcess,
    ipc::GeckoChildProcessHost* aSubprocess);

  // This returns a reference to the APZCTreeManager to which
  // pan/zoom-related events can be sent.
  already_AddRefed<APZCTreeManager> GetAPZCTreeManagerForLayers(uint64_t aLayersId);

  // Allocate an ID that can be used to refer to a layer tree and
  // associated resources that live only on the compositor thread.
  //
  // Must run on the content main thread.
  uint64_t AllocateLayerTreeId();

  // Release compositor-thread resources referred to by |aID|.
  //
  // Must run on the content main thread.
  void DeallocateLayerTreeId(uint64_t aLayersId);

  void RequestNotifyLayerTreeReady(uint64_t aLayersId, CompositorUpdateObserver* aObserver);
  void RequestNotifyLayerTreeCleared(uint64_t aLayersId, CompositorUpdateObserver* aObserver);
  void SwapLayerTreeObservers(uint64_t aLayer, uint64_t aOtherLayer);

  // Creates a new RemoteContentController for aTabId. Should only be called on
  // the main thread.
  //
  // aLayersId      The layers id for the browser corresponding to aTabId.
  // aContentParent The ContentParent for the process that the TabChild for
  //                aTabId lives in.
  // aBrowserParent The toplevel TabParent for aTabId.
  bool UpdateRemoteContentController(uint64_t aLayersId,
                                     dom::ContentParent* aContentParent,
                                     const dom::TabId& aTabId,
                                     dom::TabParent* aBrowserParent);

private:
  GPUProcessManager();

  DISALLOW_COPY_AND_ASSIGN(GPUProcessManager);
};

} // namespace gfx
} // namespace mozilla

#endif // _include_mozilla_gfx_ipc_GPUProcessManager_h_
