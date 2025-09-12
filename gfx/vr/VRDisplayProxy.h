/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_VR_PROXY_H
#define GFX_VR_PROXY_H

#include "nsIScreen.h"
#include "nsCOMPtr.h"
#include "mozilla/RefPtr.h"

#include "gfxVR.h"

namespace mozilla {
namespace gfx {

class VRManagerChild;

class VRDisplayProxy
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VRDisplayProxy)

  explicit VRDisplayProxy(const VRDisplayUpdate& aDeviceUpdate);

  void UpdateDeviceInfo(const VRDisplayUpdate& aDeviceUpdate);
  void UpdateSensorState(const VRHMDSensorState& aSensorState);

  const VRDisplayInfo& GetDeviceInfo() const { return mDeviceInfo; }
  virtual VRHMDSensorState GetSensorState();
  virtual VRHMDSensorState GetImmediateSensorState();

  bool SetFOV(const VRFieldOfView& aFOVLeft, const VRFieldOfView& aFOVRight,
              double zNear, double zFar);

  virtual void ZeroSensor();


  // The nsIScreen that represents this device
  nsIScreen* GetScreen() { return mScreen; }

protected:
  virtual ~VRDisplayProxy();

  VRDisplayInfo mDeviceInfo;
  VRHMDSensorState mSensorState;

  nsCOMPtr<nsIScreen> mScreen;

  static already_AddRefed<nsIScreen> MakeFakeScreen(const IntRect& aScreenRect);

};

} // namespace gfx
} // namespace mozilla

#endif /* GFX_VR_PROXY_H */
