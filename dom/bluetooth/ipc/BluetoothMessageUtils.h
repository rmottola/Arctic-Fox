/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_ipc_BluetoothMessageUtils_h
#define mozilla_dom_bluetooth_ipc_BluetoothMessageUtils_h

#include "mozilla/dom/bluetooth/BluetoothCommon.h"
#include "ipc/IPCMessageUtils.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothAddress>
{
  typedef mozilla::dom::bluetooth::BluetoothAddress paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    for (size_t i = 0; i < MOZ_ARRAY_LENGTH(aParam.mAddr); ++i) {
      WriteParam(aMsg, aParam.mAddr[i]);
    }
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    for (size_t i = 0; i < MOZ_ARRAY_LENGTH(aResult->mAddr); ++i) {
      if (!ReadParam(aMsg, aIter, aResult->mAddr + i)) {
        return false;
      }
    }
    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothObjectType>
  : public ContiguousEnumSerializer<
             mozilla::dom::bluetooth::BluetoothObjectType,
             mozilla::dom::bluetooth::TYPE_MANAGER,
             mozilla::dom::bluetooth::NUM_TYPE>
{ };

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothPinCode>
{
  typedef mozilla::dom::bluetooth::BluetoothPinCode paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    auto length = aParam.mLength;
    if (length > MOZ_ARRAY_LENGTH(aParam.mPinCode)) {
      length = MOZ_ARRAY_LENGTH(aParam.mPinCode);
    }

    WriteParam(aMsg, length);
    for (uint8_t i = 0; i < length; ++i) {
      WriteParam(aMsg, aParam.mPinCode[i]);
    }
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &aResult->mLength)) {
      return false;
    }

    auto maxLength = MOZ_ARRAY_LENGTH(aResult->mPinCode);

    if (aResult->mLength > maxLength) {
      return false;
    }
    for (uint8_t i = 0; i < aResult->mLength; ++i) {
      if (!ReadParam(aMsg, aIter, aResult->mPinCode + i)) {
        return false;
      }
    }
    for (uint8_t i = aResult->mLength; i < maxLength; ++i) {
      aResult->mPinCode[i] = 0;
    }
    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothRemoteName>
{
  typedef mozilla::dom::bluetooth::BluetoothRemoteName paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mLength);
    for (size_t i = 0; i < aParam.mLength; ++i) {
      WriteParam(aMsg, aParam.mName[i]);
    }
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &aResult->mLength)) {
      return false;
    }
    if (aResult->mLength > MOZ_ARRAY_LENGTH(aResult->mName)) {
      return false;
    }
    for (uint8_t i = 0; i < aResult->mLength; ++i) {
      if (!ReadParam(aMsg, aIter, aResult->mName + i)) {
        return false;
      }
    }
    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothSspVariant>
  : public ContiguousEnumSerializer<
             mozilla::dom::bluetooth::BluetoothSspVariant,
             mozilla::dom::bluetooth::SSP_VARIANT_PASSKEY_CONFIRMATION,
             mozilla::dom::bluetooth::NUM_SSP_VARIANT>
{ };

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothStatus>
  : public ContiguousEnumSerializer<
             mozilla::dom::bluetooth::BluetoothStatus,
             mozilla::dom::bluetooth::STATUS_SUCCESS,
             mozilla::dom::bluetooth::NUM_STATUS>
{ };

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattWriteType>
  : public ContiguousEnumSerializer<
             mozilla::dom::bluetooth::BluetoothGattWriteType,
             mozilla::dom::bluetooth::GATT_WRITE_TYPE_NO_RESPONSE,
             mozilla::dom::bluetooth::GATT_WRITE_TYPE_END_GUARD>
{ };

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattAuthReq>
  : public ContiguousEnumSerializer<
             mozilla::dom::bluetooth::BluetoothGattAuthReq,
             mozilla::dom::bluetooth::GATT_AUTH_REQ_NONE,
             mozilla::dom::bluetooth::GATT_AUTH_REQ_END_GUARD>
{ };

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothUuid>
{
  typedef mozilla::dom::bluetooth::BluetoothUuid paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    for (uint8_t i = 0; i < 16; i++) {
      WriteParam(aMsg, aParam.mUuid[i]);
    }
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    for (uint8_t i = 0; i < 16; i++) {
      if (!ReadParam(aMsg, aIter, &(aResult->mUuid[i]))) {
        return false;
      }
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattId>
{
  typedef mozilla::dom::bluetooth::BluetoothGattId paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mUuid);
    WriteParam(aMsg, aParam.mInstanceId);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &(aResult->mUuid)) ||
        !ReadParam(aMsg, aIter, &(aResult->mInstanceId))) {
      return false;
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattServiceId>
{
  typedef mozilla::dom::bluetooth::BluetoothGattServiceId paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mId);
    WriteParam(aMsg, aParam.mIsPrimary);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &(aResult->mId)) ||
        !ReadParam(aMsg, aIter, &(aResult->mIsPrimary))) {
      return false;
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattCharAttribute>
{
  typedef mozilla::dom::bluetooth::BluetoothGattCharAttribute paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mId);
    WriteParam(aMsg, aParam.mProperties);
    WriteParam(aMsg, aParam.mWriteType);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &(aResult->mId)) ||
        !ReadParam(aMsg, aIter, &(aResult->mProperties)) ||
        !ReadParam(aMsg, aIter, &(aResult->mWriteType))) {
      return false;
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothAttributeHandle>
{
  typedef mozilla::dom::bluetooth::BluetoothAttributeHandle paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mHandle);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &(aResult->mHandle))) {
      return false;
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattResponse>
{
  typedef mozilla::dom::bluetooth::BluetoothGattResponse paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    auto length = aParam.mLength;
    if (length > MOZ_ARRAY_LENGTH(aParam.mValue)) {
      length = MOZ_ARRAY_LENGTH(aParam.mValue);
    }

    WriteParam(aMsg, aParam.mHandle);
    WriteParam(aMsg, aParam.mOffset);
    WriteParam(aMsg, length);
    WriteParam(aMsg, aParam.mAuthReq);
    for (uint16_t i = 0; i < length; i++) {
      WriteParam(aMsg, aParam.mValue[i]);
    }
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &(aResult->mHandle)) ||
        !ReadParam(aMsg, aIter, &(aResult->mOffset)) ||
        !ReadParam(aMsg, aIter, &(aResult->mLength)) ||
        !ReadParam(aMsg, aIter, &(aResult->mAuthReq))) {
      return false;
    }

    if (aResult->mLength > MOZ_ARRAY_LENGTH(aResult->mValue)) {
      return false;
    }

    for (uint16_t i = 0; i < aResult->mLength; i++) {
      if (!ReadParam(aMsg, aIter, &(aResult->mValue[i]))) {
        return false;
      }
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::ControlPlayStatus>
{
  typedef mozilla::dom::bluetooth::ControlPlayStatus paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, static_cast<uint8_t>(aParam));
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    uint8_t value;
    if (!ReadParam(aMsg, aIter, &value)) {
      return false;
    }

    mozilla::dom::bluetooth::ControlPlayStatus result =
      static_cast<mozilla::dom::bluetooth::ControlPlayStatus>(value);

    switch (result) {
      case mozilla::dom::bluetooth::ControlPlayStatus::PLAYSTATUS_STOPPED:
      case mozilla::dom::bluetooth::ControlPlayStatus::PLAYSTATUS_PLAYING:
      case mozilla::dom::bluetooth::ControlPlayStatus::PLAYSTATUS_PAUSED:
      case mozilla::dom::bluetooth::ControlPlayStatus::PLAYSTATUS_FWD_SEEK:
      case mozilla::dom::bluetooth::ControlPlayStatus::PLAYSTATUS_REV_SEEK:
      case mozilla::dom::bluetooth::ControlPlayStatus::PLAYSTATUS_ERROR:
        *aResult = result;
        return true;
      default:
        return false;
    }
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattAdvertisingData>
{
  typedef mozilla::dom::bluetooth::BluetoothGattAdvertisingData paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mAppearance);
    WriteParam(aMsg, aParam.mIncludeDevName);
    WriteParam(aMsg, aParam.mIncludeTxPower);
    WriteParam(aMsg, aParam.mManufacturerData);
    WriteParam(aMsg, aParam.mServiceData);
    WriteParam(aMsg, aParam.mServiceUuids);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (!ReadParam(aMsg, aIter, &(aResult->mAppearance)) ||
        !ReadParam(aMsg, aIter, &(aResult->mIncludeDevName)) ||
        !ReadParam(aMsg, aIter, &(aResult->mIncludeTxPower)) ||
        !ReadParam(aMsg, aIter, &(aResult->mManufacturerData)) ||
        !ReadParam(aMsg, aIter, &(aResult->mServiceData)) ||
        !ReadParam(aMsg, aIter, &(aResult->mServiceUuids))) {
      return false;
    }

    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::bluetooth::BluetoothGattStatus>
  : public ContiguousEnumSerializer<
             mozilla::dom::bluetooth::BluetoothGattStatus,
             mozilla::dom::bluetooth::GATT_STATUS_SUCCESS,
             mozilla::dom::bluetooth::GATT_STATUS_END_OF_ERROR>
{ };

} // namespace IPC

#endif // mozilla_dom_bluetooth_ipc_BluetoothMessageUtils_h
