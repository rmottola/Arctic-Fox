/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWrapperCache.h"

#include "mozilla/dom/Element.h"
#include "mozilla/dom/VRDisplayBinding.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/VRDisplay.h"
#include "Navigator.h"
#include "gfxVR.h"
#include "VRDisplayProxy.h"
#include "VRManagerChild.h"
#include "nsIFrame.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace dom {

/*static*/ bool
VRDisplay::RefreshVRDisplays(dom::Navigator* aNavigator)
{
  gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
  return vm && vm->RefreshVRDisplaysWithCallback(aNavigator);
}

/*static*/ void
VRDisplay::UpdateVRDisplays(nsTArray<RefPtr<VRDisplay>>& aDevices, nsISupports* aParent)
{
  nsTArray<RefPtr<VRDisplay>> devices;

  gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
  nsTArray<RefPtr<gfx::VRDisplayProxy>> proxyDevices;
  if (vm && vm->GetVRDisplays(proxyDevices)) {
    for (size_t i = 0; i < proxyDevices.Length(); i++) {
      RefPtr<gfx::VRDisplayProxy> proxyDevice = proxyDevices[i];
      bool isNewDevice = true;
      for (size_t j = 0; j < aDevices.Length(); j++) {
        if (aDevices[j]->GetHMD()->GetDeviceInfo() == proxyDevice->GetDeviceInfo()) {
          devices.AppendElement(aDevices[j]);
          isNewDevice = false;
        }
      }

      if (isNewDevice) {
        gfx::VRStateValidFlags sensorBits = proxyDevice->GetDeviceInfo().GetSupportedSensorStateBits();
        devices.AppendElement(new HMDInfoVRDisplay(aParent, proxyDevice));
        if (sensorBits & (gfx::VRStateValidFlags::State_Position |
            gfx::VRStateValidFlags::State_Orientation))
        {
          devices.AppendElement(new HMDPositionVRDisplay(aParent, proxyDevice));
        }
      }
    }
  }

  aDevices = devices;
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(VRFieldOfViewReadOnly, mParent)
NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(VRFieldOfViewReadOnly, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(VRFieldOfViewReadOnly, Release)

JSObject*
VRFieldOfViewReadOnly::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return VRFieldOfViewReadOnlyBinding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<VRFieldOfView>
VRFieldOfView::Constructor(const GlobalObject& aGlobal, const VRFieldOfViewInit& aParams,
                           ErrorResult& aRV)
{
  RefPtr<VRFieldOfView> fov =
    new VRFieldOfView(aGlobal.GetAsSupports(),
                      aParams.mUpDegrees, aParams.mRightDegrees,
                      aParams.mDownDegrees, aParams.mLeftDegrees);
  return fov.forget();
}

already_AddRefed<VRFieldOfView>
VRFieldOfView::Constructor(const GlobalObject& aGlobal,
                           double aUpDegrees, double aRightDegrees,
                           double aDownDegrees, double aLeftDegrees,
                           ErrorResult& aRV)
{
  RefPtr<VRFieldOfView> fov =
    new VRFieldOfView(aGlobal.GetAsSupports(),
                      aUpDegrees, aRightDegrees, aDownDegrees,
                      aLeftDegrees);
  return fov.forget();
}

JSObject*
VRFieldOfView::WrapObject(JSContext* aCx,
                          JS::Handle<JSObject*> aGivenProto)
{
  return VRFieldOfViewBinding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(VREyeParameters, mParent, mMinFOV, mMaxFOV, mRecFOV, mCurFOV, mEyeTranslation, mRenderRect)
NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(VREyeParameters, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(VREyeParameters, Release)

VREyeParameters::VREyeParameters(nsISupports* aParent,
                                 const gfx::VRFieldOfView& aMinFOV,
                                 const gfx::VRFieldOfView& aMaxFOV,
                                 const gfx::VRFieldOfView& aRecFOV,
                                 const gfx::Point3D& aEyeTranslation,
                                 const gfx::VRFieldOfView& aCurFOV,
                                 const gfx::IntRect& aRenderRect)
  : mParent(aParent)
{
  mMinFOV = new VRFieldOfView(aParent, aMinFOV);
  mMaxFOV = new VRFieldOfView(aParent, aMaxFOV);
  mRecFOV = new VRFieldOfView(aParent, aRecFOV);
  mCurFOV = new VRFieldOfView(aParent, aCurFOV);

  mEyeTranslation = new DOMPoint(aParent, aEyeTranslation.x, aEyeTranslation.y, aEyeTranslation.z, 0.0);
  mRenderRect = new DOMRect(aParent, aRenderRect.x, aRenderRect.y, aRenderRect.width, aRenderRect.height);
}

VRFieldOfView*
VREyeParameters::MinimumFieldOfView()
{
  return mMinFOV;
}

VRFieldOfView*
VREyeParameters::MaximumFieldOfView()
{
  return mMaxFOV;
}

VRFieldOfView*
VREyeParameters::RecommendedFieldOfView()
{
  return mRecFOV;
}

VRFieldOfView*
VREyeParameters::CurrentFieldOfView()
{
  return mCurFOV;
}

DOMPoint*
VREyeParameters::EyeTranslation()
{
  return mEyeTranslation;
}

DOMRect*
VREyeParameters::RenderRect()
{
  return mRenderRect;
}

JSObject*
VREyeParameters::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return VREyeParametersBinding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(VRPositionState, mParent)
NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(VRPositionState, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(VRPositionState, Release)

VRPositionState::VRPositionState(nsISupports* aParent, const gfx::VRHMDSensorState& aState)
  : mParent(aParent)
  , mVRState(aState)
{
  mTimeStamp = aState.timestamp;

  if (aState.flags & gfx::VRStateValidFlags::State_Position) {
    mPosition = new DOMPoint(mParent, aState.position[0], aState.position[1], aState.position[2], 0.0);
  }

  if (aState.flags & gfx::VRStateValidFlags::State_Orientation) {
    mOrientation = new DOMPoint(mParent, aState.orientation[0], aState.orientation[1], aState.orientation[2], aState.orientation[3]);
  }
}

DOMPoint*
VRPositionState::GetLinearVelocity()
{
  if (!mLinearVelocity) {
    mLinearVelocity = new DOMPoint(mParent, mVRState.linearVelocity[0], mVRState.linearVelocity[1], mVRState.linearVelocity[2], 0.0);
  }
  return mLinearVelocity;
}

DOMPoint*
VRPositionState::GetLinearAcceleration()
{
  if (!mLinearAcceleration) {
    mLinearAcceleration = new DOMPoint(mParent, mVRState.linearAcceleration[0], mVRState.linearAcceleration[1], mVRState.linearAcceleration[2], 0.0);
  }
  return mLinearAcceleration;
}

DOMPoint*
VRPositionState::GetAngularVelocity()
{
  if (!mAngularVelocity) {
    mAngularVelocity = new DOMPoint(mParent, mVRState.angularVelocity[0], mVRState.angularVelocity[1], mVRState.angularVelocity[2], 0.0);
  }
  return mAngularVelocity;
}

DOMPoint*
VRPositionState::GetAngularAcceleration()
{
  if (!mAngularAcceleration) {
    mAngularAcceleration = new DOMPoint(mParent, mVRState.angularAcceleration[0], mVRState.angularAcceleration[1], mVRState.angularAcceleration[2], 0.0);
  }
  return mAngularAcceleration;
}

JSObject*
VRPositionState::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return VRPositionStateBinding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(VRDisplay)
NS_IMPL_CYCLE_COLLECTING_RELEASE(VRDisplay)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(VRDisplay)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(VRDisplay, mParent)

/* virtual */ JSObject*
HMDVRDisplay::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return HMDVRDisplayBinding::Wrap(aCx, this, aGivenProto);
}

/* virtual */ JSObject*
PositionSensorVRDisplay::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return PositionSensorVRDisplayBinding::Wrap(aCx, this, aGivenProto);
}

HMDInfoVRDisplay::HMDInfoVRDisplay(nsISupports* aParent, gfx::VRDisplayProxy* aHMD)
  : HMDVRDisplay(aParent, aHMD)
{
  MOZ_COUNT_CTOR_INHERITED(HMDInfoVRDisplay, HMDVRDisplay);
  uint64_t hmdid = aHMD->GetDeviceInfo().GetDeviceID() << 8;
  uint64_t devid = hmdid | 0x00; // we generate a devid with low byte 0 for the HMD, 1 for the position sensor

  mHWID.Truncate();
  mHWID.AppendPrintf("0x%llx", hmdid);

  mDeviceId.Truncate();
  mDeviceId.AppendPrintf("0x%llx", devid);

  mDeviceName.Truncate();
  mDeviceName.Append(NS_ConvertASCIItoUTF16(aHMD->GetDeviceInfo().GetDeviceName()));
  mDeviceName.AppendLiteral(" (HMD)");

  mValid = true;
}

HMDInfoVRDisplay::~HMDInfoVRDisplay()
{
  MOZ_COUNT_DTOR_INHERITED(HMDInfoVRDisplay, HMDVRDisplay);
}

/* If a field of view that is set to all 0's is passed in,
 * the recommended field of view for that eye is used.
 */
void
HMDInfoVRDisplay::SetFieldOfView(const VRFieldOfViewInit& aLeftFOV,
                                const VRFieldOfViewInit& aRightFOV,
                                double zNear, double zFar)
{
  gfx::VRFieldOfView left = gfx::VRFieldOfView(aLeftFOV.mUpDegrees, aLeftFOV.mRightDegrees,
                                               aLeftFOV.mDownDegrees, aLeftFOV.mLeftDegrees);
  gfx::VRFieldOfView right = gfx::VRFieldOfView(aRightFOV.mUpDegrees, aRightFOV.mRightDegrees,
                                                aRightFOV.mDownDegrees, aRightFOV.mLeftDegrees);

  if (left.IsZero()) {
    left = mHMD->GetDeviceInfo().GetRecommendedEyeFOV(VRDisplayInfo::Eye_Left);
  }

  if (right.IsZero()) {
    right = mHMD->GetDeviceInfo().GetRecommendedEyeFOV(VRDisplayInfo::Eye_Right);
  }

  mHMD->SetFOV(left, right, zNear, zFar);
}

already_AddRefed<VREyeParameters> HMDInfoVRDisplay::GetEyeParameters(VREye aEye)
{
  gfx::IntSize sz(mHMD->GetDeviceInfo().SuggestedEyeResolution());
  gfx::VRDisplayInfo::Eye eye = aEye == VREye::Left ? gfx::VRDisplayInfo::Eye_Left : gfx::VRDisplayInfo::Eye_Right;
  RefPtr<VREyeParameters> params =
    new VREyeParameters(mParent,
                        gfx::VRFieldOfView(15, 15, 15, 15), // XXX min?
                        mHMD->GetDeviceInfo().GetMaximumEyeFOV(eye),
                        mHMD->GetDeviceInfo().GetRecommendedEyeFOV(eye),
                        mHMD->GetDeviceInfo().GetEyeTranslation(eye),
                        mHMD->GetDeviceInfo().GetEyeFOV(eye),
                        gfx::IntRect((aEye == VREye::Left) ? 0 : sz.width, 0, sz.width, sz.height));
  return params.forget();
}

HMDPositionVRDisplay::HMDPositionVRDisplay(nsISupports* aParent, gfx::VRDisplayProxy* aHMD)
  : PositionSensorVRDisplay(aParent, aHMD)
{
  MOZ_COUNT_CTOR_INHERITED(HMDPositionVRDisplay, PositionSensorVRDisplay);

  uint64_t hmdid = aHMD->GetDeviceInfo().GetDeviceID() << 8;
  uint64_t devid = hmdid | 0x01; // we generate a devid with low byte 0 for the HMD, 1 for the position sensor

  mHWID.Truncate();
  mHWID.AppendPrintf("0x%llx", hmdid);

  mDeviceId.Truncate();
  mDeviceId.AppendPrintf("0x%llx", devid);

  mDeviceName.Truncate();
  mDeviceName.Append(NS_ConvertASCIItoUTF16(aHMD->GetDeviceInfo().GetDeviceName()));
  mDeviceName.AppendLiteral(" (Sensor)");

  mValid = true;
}

HMDPositionVRDisplay::~HMDPositionVRDisplay()
{
  MOZ_COUNT_DTOR_INHERITED(HMDPositionVRDisplay, PositionSensorVRDisplay);
}

already_AddRefed<VRPositionState>
HMDPositionVRDisplay::GetState()
{
  gfx::VRHMDSensorState state = mHMD->GetSensorState();
  RefPtr<VRPositionState> obj = new VRPositionState(mParent, state);

  return obj.forget();
}

already_AddRefed<VRPositionState>
HMDPositionVRDisplay::GetImmediateState()
{
  gfx::VRHMDSensorState state = mHMD->GetImmediateSensorState();
  RefPtr<VRPositionState> obj = new VRPositionState(mParent, state);

  return obj.forget();
}

void
HMDPositionVRDisplay::ResetSensor()
{
  mHMD->ZeroSensor();
}

} // namespace dom
} // namespace mozilla
