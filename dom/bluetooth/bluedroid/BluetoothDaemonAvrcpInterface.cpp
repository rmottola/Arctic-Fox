/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothDaemonAvrcpInterface.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/unused.h"

BEGIN_BLUETOOTH_NAMESPACE

using namespace mozilla::ipc;

//
// AVRCP module
//

BluetoothAvrcpNotificationHandler*
  BluetoothDaemonAvrcpModule::sNotificationHandler;

void
BluetoothDaemonAvrcpModule::SetNotificationHandler(
  BluetoothAvrcpNotificationHandler* aNotificationHandler)
{
  sNotificationHandler = aNotificationHandler;
}

void
BluetoothDaemonAvrcpModule::HandleSvc(const DaemonSocketPDUHeader& aHeader,
                                      DaemonSocketPDU& aPDU,
                                      DaemonSocketResultHandler* aRes)
{
  static void (BluetoothDaemonAvrcpModule::* const HandleOp[])(
    const DaemonSocketPDUHeader&, DaemonSocketPDU&,
    DaemonSocketResultHandler*) = {
    [0] = &BluetoothDaemonAvrcpModule::HandleRsp,
    [1] = &BluetoothDaemonAvrcpModule::HandleNtf
  };

  MOZ_ASSERT(!NS_IsMainThread());

  unsigned int isNtf = !!(aHeader.mOpcode & 0x80);

  (this->*(HandleOp[isNtf]))(aHeader, aPDU, aRes);
}

// Commands
//

nsresult
BluetoothDaemonAvrcpModule::GetPlayStatusRspCmd(
  ControlPlayStatus aPlayStatus, uint32_t aSongLen, uint32_t aSongPos,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_GET_PLAY_STATUS_RSP,
                                1 + // Play status
                                4 + // Duration
                                4); // Position

  nsresult rv = PackPDU(aPlayStatus, aSongLen, aSongPos, *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::ListPlayerAppAttrRspCmd(
  int aNumAttr, const BluetoothAvrcpPlayerAttribute* aPAttrs,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_LIST_PLAYER_APP_ATTR_RSP,
                                1 + // # Attributes
                                aNumAttr); // Player attributes

  nsresult rv = PackPDU(
    PackConversion<int, uint8_t>(aNumAttr),
    PackArray<BluetoothAvrcpPlayerAttribute>(aPAttrs, aNumAttr), *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::ListPlayerAppValueRspCmd(
  int aNumVal, uint8_t* aPVals, BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_LIST_PLAYER_APP_VALUE_RSP,
                                1 + // # Values
                                aNumVal); // Player values

  nsresult rv = PackPDU(PackConversion<int, uint8_t>(aNumVal),
                        PackArray<uint8_t>(aPVals, aNumVal), *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::GetPlayerAppValueRspCmd(
  uint8_t aNumAttrs, const uint8_t* aIds, const uint8_t* aValues,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_GET_PLAYER_APP_VALUE_RSP,
                                1 + // # Pairs
                                2 * aNumAttrs); // Attribute-value pairs

  nsresult rv = PackPDU(
    aNumAttrs,
    BluetoothAvrcpAttributeValuePairs(aIds, aValues, aNumAttrs), *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::GetPlayerAppAttrTextRspCmd(
  int aNumAttr, const uint8_t* aIds, const char** aTexts,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_GET_PLAYER_APP_ATTR_TEXT_RSP,
                                0); // Dynamically allocated

  nsresult rv = PackPDU(
    PackConversion<int, uint8_t>(aNumAttr),
    BluetoothAvrcpAttributeTextPairs(aIds, aTexts, aNumAttr), *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::GetPlayerAppValueTextRspCmd(
  int aNumVal, const uint8_t* aIds, const char** aTexts,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_GET_PLAYER_APP_VALUE_TEXT_RSP,
                                0); // Dynamically allocated

  nsresult rv = PackPDU(
    PackConversion<int, uint8_t>(aNumVal),
    BluetoothAvrcpAttributeTextPairs(aIds, aTexts, aNumVal), *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::GetElementAttrRspCmd(
  uint8_t aNumAttr, const BluetoothAvrcpElementAttribute* aAttr,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_GET_ELEMENT_ATTR_RSP,
                                0); // Dynamically allocated

  nsresult rv = PackPDU(
    aNumAttr,
    PackArray<BluetoothAvrcpElementAttribute>(aAttr, aNumAttr), *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::SetPlayerAppValueRspCmd(
  BluetoothAvrcpStatus aRspStatus, BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_SET_PLAYER_APP_VALUE_RSP,
                                1); // Status code

  nsresult rv = PackPDU(aRspStatus, *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::RegisterNotificationRspCmd(
  BluetoothAvrcpEvent aEvent, BluetoothAvrcpNotification aType,
  const BluetoothAvrcpNotificationParam& aParam,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_REGISTER_NOTIFICATION_RSP,
                                1 + // Event
                                1 + // Type
                                1 + // Data length
                                256); // Maximum data length

  BluetoothAvrcpEventParamPair data(aEvent, aParam);
  nsresult rv = PackPDU(aEvent, aType, static_cast<uint8_t>(data.GetLength()),
                        data, *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

nsresult
BluetoothDaemonAvrcpModule::SetVolumeCmd(uint8_t aVolume,
                                         BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(NS_IsMainThread());

  UniquePtr<DaemonSocketPDU> pdu =
    MakeUnique<DaemonSocketPDU>(SERVICE_ID, OPCODE_SET_VOLUME,
                                1); // Volume

  nsresult rv = PackPDU(aVolume, *pdu);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = Send(pdu.get(), aRes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Unused << pdu.release();
  return NS_OK;
}

// Responses
//

void
BluetoothDaemonAvrcpModule::ErrorRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ErrorRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::OnError, UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::GetPlayStatusRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::GetPlayStatusRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::ListPlayerAppAttrRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::ListPlayerAppAttrRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::ListPlayerAppValueRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::ListPlayerAppValueRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::GetPlayerAppValueRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::GetPlayerAppValueRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::GetPlayerAppAttrTextRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::GetPlayerAppAttrTextRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::GetPlayerAppValueTextRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::GetPlayerAppValueTextRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::GetElementAttrRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::GetElementAttrRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::SetPlayerAppValueRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::SetPlayerAppValueRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::RegisterNotificationRspRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::RegisterNotificationRsp,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::SetVolumeRsp(
  const DaemonSocketPDUHeader& aHeader,
  DaemonSocketPDU& aPDU, BluetoothAvrcpResultHandler* aRes)
{
  ResultRunnable::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::SetVolume,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::HandleRsp(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU,
  DaemonSocketResultHandler* aRes)
{
  static void (BluetoothDaemonAvrcpModule::* const HandleRsp[])(
    const DaemonSocketPDUHeader&,
    DaemonSocketPDU&,
    BluetoothAvrcpResultHandler*) = {
    [OPCODE_ERROR] =
      &BluetoothDaemonAvrcpModule::ErrorRsp,
    [OPCODE_GET_PLAY_STATUS_RSP] =
      &BluetoothDaemonAvrcpModule::GetPlayStatusRspRsp,
    [OPCODE_LIST_PLAYER_APP_ATTR_RSP] =
      &BluetoothDaemonAvrcpModule::ListPlayerAppAttrRspRsp,
    [OPCODE_LIST_PLAYER_APP_VALUE_RSP] =
      &BluetoothDaemonAvrcpModule::ListPlayerAppValueRspRsp,
    [OPCODE_GET_PLAYER_APP_VALUE_RSP] =
      &BluetoothDaemonAvrcpModule::GetPlayerAppValueRspRsp,
    [OPCODE_GET_PLAYER_APP_ATTR_TEXT_RSP] =
      &BluetoothDaemonAvrcpModule::GetPlayerAppAttrTextRspRsp,
    [OPCODE_GET_PLAYER_APP_VALUE_TEXT_RSP] =
      &BluetoothDaemonAvrcpModule::GetPlayerAppValueTextRspRsp,
    [OPCODE_GET_ELEMENT_ATTR_RSP]=
      &BluetoothDaemonAvrcpModule::GetElementAttrRspRsp,
    [OPCODE_SET_PLAYER_APP_VALUE_RSP] =
      &BluetoothDaemonAvrcpModule::SetPlayerAppValueRspRsp,
    [OPCODE_REGISTER_NOTIFICATION_RSP] =
      &BluetoothDaemonAvrcpModule::RegisterNotificationRspRsp,
    [OPCODE_SET_VOLUME] =
      &BluetoothDaemonAvrcpModule::SetVolumeRsp
  };

  MOZ_ASSERT(!NS_IsMainThread()); // I/O thread

  if (NS_WARN_IF(!(aHeader.mOpcode < MOZ_ARRAY_LENGTH(HandleRsp))) ||
      NS_WARN_IF(!HandleRsp[aHeader.mOpcode])) {
    return;
  }

  RefPtr<BluetoothAvrcpResultHandler> res =
    static_cast<BluetoothAvrcpResultHandler*>(aRes);

  if (!res) {
    return; // Return early if no result handler has been set for response
  }

  (this->*(HandleRsp[aHeader.mOpcode]))(aHeader, aPDU, res);
}

// Notifications
//

// Returns the current notification handler to a notification runnable
class BluetoothDaemonAvrcpModule::NotificationHandlerWrapper final
{
public:
  typedef BluetoothAvrcpNotificationHandler ObjectType;

  static ObjectType* GetInstance()
  {
    MOZ_ASSERT(NS_IsMainThread());

    return sNotificationHandler;
  }
};

// Init operator class for RemoteFeatureNotification
class BluetoothDaemonAvrcpModule::RemoteFeatureInitOp final
  : private PDUInitOp
{
public:
  RemoteFeatureInitOp(DaemonSocketPDU& aPDU)
    : PDUInitOp(aPDU)
  { }

  nsresult
  operator () (BluetoothAddress& aArg1, unsigned long& aArg2) const
  {
    DaemonSocketPDU& pdu = GetPDU();

    /* Read address */
    nsresult rv = UnpackPDU(pdu, aArg1);
    if (NS_FAILED(rv)) {
      return rv;
    }

    /* Read feature */
    rv = UnpackPDU(
      pdu,
      UnpackConversion<BluetoothAvrcpRemoteFeatureBits, unsigned long>(aArg2));
    if (NS_FAILED(rv)) {
      return rv;
    }
    WarnAboutTrailingData();
    return NS_OK;
  }
};

void
BluetoothDaemonAvrcpModule::RemoteFeatureNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  RemoteFeatureNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::RemoteFeatureNotification,
    RemoteFeatureInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::GetPlayStatusNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  GetPlayStatusNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::GetPlayStatusNotification,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::ListPlayerAppAttrNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  ListPlayerAppAttrNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::ListPlayerAppAttrNotification,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::ListPlayerAppValuesNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  ListPlayerAppValuesNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::ListPlayerAppValuesNotification,
    UnpackPDUInitOp(aPDU));
}

// Init operator class for GetPlayerAppValueNotification
class BluetoothDaemonAvrcpModule::GetPlayerAppValueInitOp final
  : private PDUInitOp
{
public:
  GetPlayerAppValueInitOp(DaemonSocketPDU& aPDU)
    : PDUInitOp(aPDU)
  { }

  nsresult
  operator () (uint8_t& aArg1,
               UniquePtr<BluetoothAvrcpPlayerAttribute[]>& aArg2) const
  {
    DaemonSocketPDU& pdu = GetPDU();

    /* Read number of attributes */
    nsresult rv = UnpackPDU(pdu, aArg1);
    if (NS_FAILED(rv)) {
      return rv;
    }

    /* Read attributes */
    rv = UnpackPDU(
      pdu, UnpackArray<BluetoothAvrcpPlayerAttribute>(aArg2, aArg1));
    if (NS_FAILED(rv)) {
      return rv;
    }
    WarnAboutTrailingData();
    return NS_OK;
  }
};

void
BluetoothDaemonAvrcpModule::GetPlayerAppValueNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  GetPlayerAppValueNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::GetPlayerAppValueNotification,
    GetPlayerAppValueInitOp(aPDU));
}

// Init operator class for GetPlayerAppAttrsTextNotification
class BluetoothDaemonAvrcpModule::GetPlayerAppAttrsTextInitOp final
  : private PDUInitOp
{
public:
  GetPlayerAppAttrsTextInitOp(DaemonSocketPDU& aPDU)
    : PDUInitOp(aPDU)
  { }

  nsresult
  operator () (uint8_t& aArg1,
               UniquePtr<BluetoothAvrcpPlayerAttribute[]>& aArg2) const
  {
    DaemonSocketPDU& pdu = GetPDU();

    /* Read number of attributes */
    nsresult rv = UnpackPDU(pdu, aArg1);
    if (NS_FAILED(rv)) {
      return rv;
    }

    /* Read attributes */
    rv = UnpackPDU(
      pdu, UnpackArray<BluetoothAvrcpPlayerAttribute>(aArg2, aArg1));
    if (NS_FAILED(rv)) {
      return rv;
    }
    WarnAboutTrailingData();
    return NS_OK;
  }
};

void
BluetoothDaemonAvrcpModule::GetPlayerAppAttrsTextNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  GetPlayerAppAttrsTextNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::GetPlayerAppAttrsTextNotification,
    GetPlayerAppAttrsTextInitOp(aPDU));
}

// Init operator class for GetPlayerAppValuesTextNotification
class BluetoothDaemonAvrcpModule::GetPlayerAppValuesTextInitOp final
  : private PDUInitOp
{
public:
  GetPlayerAppValuesTextInitOp(DaemonSocketPDU& aPDU)
    : PDUInitOp(aPDU)
  { }

  nsresult
  operator () (uint8_t& aArg1, uint8_t& aArg2,
               UniquePtr<uint8_t[]>& aArg3) const
  {
    DaemonSocketPDU& pdu = GetPDU();

    /* Read attribute */
    nsresult rv = UnpackPDU(pdu, aArg1);
    if (NS_FAILED(rv)) {
      return rv;
    }

    /* Read number of values */
    rv = UnpackPDU(pdu, aArg2);
    if (NS_FAILED(rv)) {
      return rv;
    }

    /* Read values */
    rv = UnpackPDU(pdu, UnpackArray<uint8_t>(aArg3, aArg2));
    if (NS_FAILED(rv)) {
      return rv;
    }
    WarnAboutTrailingData();
    return NS_OK;
  }
};

void
BluetoothDaemonAvrcpModule::GetPlayerAppValuesTextNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  GetPlayerAppValuesTextNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::GetPlayerAppValuesTextNotification,
    GetPlayerAppValuesTextInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::SetPlayerAppValueNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  SetPlayerAppValueNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::SetPlayerAppValueNotification,
    UnpackPDUInitOp(aPDU));
}

// Init operator class for GetElementAttrNotification
class BluetoothDaemonAvrcpModule::GetElementAttrInitOp final
  : private PDUInitOp
{
public:
  GetElementAttrInitOp(DaemonSocketPDU& aPDU)
    : PDUInitOp(aPDU)
  { }

  nsresult
  operator () (uint8_t& aArg1,
               UniquePtr<BluetoothAvrcpMediaAttribute[]>& aArg2) const
  {
    DaemonSocketPDU& pdu = GetPDU();

    /* Read number of attributes */
    nsresult rv = UnpackPDU(pdu, aArg1);
    if (NS_FAILED(rv)) {
      return rv;
    }

    /* Read attributes */
    rv = UnpackPDU(
      pdu, UnpackArray<BluetoothAvrcpMediaAttribute>(aArg2, aArg1));
    if (NS_FAILED(rv)) {
      return rv;
    }
    WarnAboutTrailingData();
    return NS_OK;
  }
};

void
BluetoothDaemonAvrcpModule::GetElementAttrNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  GetElementAttrNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::GetElementAttrNotification,
    GetElementAttrInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::RegisterNotificationNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  RegisterNotificationNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::RegisterNotificationNotification,
    UnpackPDUInitOp(aPDU));
}

#if ANDROID_VERSION >= 19
void
BluetoothDaemonAvrcpModule::VolumeChangeNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  VolumeChangeNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::VolumeChangeNotification,
    UnpackPDUInitOp(aPDU));
}

void
BluetoothDaemonAvrcpModule::PassthroughCmdNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU)
{
  PassthroughCmdNotification::Dispatch(
    &BluetoothAvrcpNotificationHandler::PassthroughCmdNotification,
    UnpackPDUInitOp(aPDU));
}
#endif

void
BluetoothDaemonAvrcpModule::HandleNtf(
  const DaemonSocketPDUHeader& aHeader, DaemonSocketPDU& aPDU,
  DaemonSocketResultHandler* aRes)
{
  static void (BluetoothDaemonAvrcpModule::* const HandleNtf[])(
    const DaemonSocketPDUHeader&, DaemonSocketPDU&) = {
#if ANDROID_VERSION >= 19
    [0] = &BluetoothDaemonAvrcpModule::RemoteFeatureNtf,
    [1] = &BluetoothDaemonAvrcpModule::GetPlayStatusNtf,
    [2] = &BluetoothDaemonAvrcpModule::ListPlayerAppAttrNtf,
    [3] = &BluetoothDaemonAvrcpModule::ListPlayerAppValuesNtf,
    [4] = &BluetoothDaemonAvrcpModule::GetPlayerAppValueNtf,
    [5] = &BluetoothDaemonAvrcpModule::GetPlayerAppAttrsTextNtf,
    [6] = &BluetoothDaemonAvrcpModule::GetPlayerAppValuesTextNtf,
    [7] = &BluetoothDaemonAvrcpModule::SetPlayerAppValueNtf,
    [8] = &BluetoothDaemonAvrcpModule::GetElementAttrNtf,
    [9] = &BluetoothDaemonAvrcpModule::RegisterNotificationNtf,
    [10] = &BluetoothDaemonAvrcpModule::VolumeChangeNtf,
    [11] = &BluetoothDaemonAvrcpModule::PassthroughCmdNtf
#else
    [0] = &BluetoothDaemonAvrcpModule::GetPlayStatusNtf,
    [1] = &BluetoothDaemonAvrcpModule::ListPlayerAppAttrNtf,
    [2] = &BluetoothDaemonAvrcpModule::ListPlayerAppValuesNtf,
    [3] = &BluetoothDaemonAvrcpModule::GetPlayerAppValueNtf,
    [4] = &BluetoothDaemonAvrcpModule::GetPlayerAppAttrsTextNtf,
    [5] = &BluetoothDaemonAvrcpModule::GetPlayerAppValuesTextNtf,
    [6] = &BluetoothDaemonAvrcpModule::SetPlayerAppValueNtf,
    [7] = &BluetoothDaemonAvrcpModule::GetElementAttrNtf,
    [8] = &BluetoothDaemonAvrcpModule::RegisterNotificationNtf
#endif
  };

  MOZ_ASSERT(!NS_IsMainThread());

  uint8_t index = aHeader.mOpcode - 0x81;

  if (NS_WARN_IF(!(index < MOZ_ARRAY_LENGTH(HandleNtf))) ||
      NS_WARN_IF(!HandleNtf[index])) {
    return;
  }

  (this->*(HandleNtf[index]))(aHeader, aPDU);
}

//
// AVRCP interface
//

BluetoothDaemonAvrcpInterface::BluetoothDaemonAvrcpInterface(
  BluetoothDaemonAvrcpModule* aModule)
  : mModule(aModule)
{ }

BluetoothDaemonAvrcpInterface::~BluetoothDaemonAvrcpInterface()
{ }

void
BluetoothDaemonAvrcpInterface::SetNotificationHandler(
  BluetoothAvrcpNotificationHandler* aNotificationHandler)
{
  MOZ_ASSERT(mModule);

  mModule->SetNotificationHandler(aNotificationHandler);
}

void
BluetoothDaemonAvrcpInterface::GetPlayStatusRsp(
  ControlPlayStatus aPlayStatus, uint32_t aSongLen, uint32_t aSongPos,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->GetPlayStatusRspCmd(aPlayStatus, aSongLen,
                                             aSongPos, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::ListPlayerAppAttrRsp(
  int aNumAttr, const BluetoothAvrcpPlayerAttribute* aPAttrs,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->ListPlayerAppAttrRspCmd(aNumAttr, aPAttrs, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::ListPlayerAppValueRsp(
  int aNumVal, uint8_t* aPVals, BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->ListPlayerAppValueRspCmd(aNumVal, aPVals, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::GetPlayerAppValueRsp(
  uint8_t aNumAttrs, const uint8_t* aIds, const uint8_t* aValues,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->GetPlayerAppValueRspCmd(aNumAttrs, aIds,
                                                 aValues, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::GetPlayerAppAttrTextRsp(
  int aNumAttr, const uint8_t* aIds, const char** aTexts,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->GetPlayerAppAttrTextRspCmd(aNumAttr, aIds,
                                                    aTexts, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::GetPlayerAppValueTextRsp(
  int aNumVal, const uint8_t* aIds, const char** aTexts,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->GetPlayerAppValueTextRspCmd(aNumVal, aIds,
                                                     aTexts, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::GetElementAttrRsp(
  uint8_t aNumAttr, const BluetoothAvrcpElementAttribute* aAttr,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->GetElementAttrRspCmd(aNumAttr, aAttr, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::SetPlayerAppValueRsp(
  BluetoothAvrcpStatus aRspStatus, BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->SetPlayerAppValueRspCmd(aRspStatus, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::RegisterNotificationRsp(
  BluetoothAvrcpEvent aEvent,
  BluetoothAvrcpNotification aType,
  const BluetoothAvrcpNotificationParam& aParam,
  BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->RegisterNotificationRspCmd(aEvent, aType,
                                                    aParam, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::SetVolume(
  uint8_t aVolume, BluetoothAvrcpResultHandler* aRes)
{
  MOZ_ASSERT(mModule);

  nsresult rv = mModule->SetVolumeCmd(aVolume, aRes);
  if (NS_FAILED(rv)) {
    DispatchError(aRes, rv);
  }
}

void
BluetoothDaemonAvrcpInterface::DispatchError(
  BluetoothAvrcpResultHandler* aRes, BluetoothStatus aStatus)
{
  DaemonResultRunnable1<BluetoothAvrcpResultHandler, void,
                        BluetoothStatus, BluetoothStatus>::Dispatch(
    aRes, &BluetoothAvrcpResultHandler::OnError,
    ConstantInitOp1<BluetoothStatus>(aStatus));
}

void
BluetoothDaemonAvrcpInterface::DispatchError(
  BluetoothAvrcpResultHandler* aRes, nsresult aRv)
{
  BluetoothStatus status;

  if (NS_WARN_IF(NS_FAILED(Convert(aRv, status)))) {
    status = STATUS_FAIL;
  }
  DispatchError(aRes, status);
}

END_BLUETOOTH_NAMESPACE
