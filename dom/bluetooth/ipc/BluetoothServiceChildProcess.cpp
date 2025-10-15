/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothServiceChildProcess.h"

#include "mozilla/Assertions.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ipc/BlobChild.h"

#include "BluetoothChild.h"
#include "MainThreadUtils.h"

USING_BLUETOOTH_NAMESPACE

namespace {

BluetoothChild* sBluetoothChild;

inline
void
SendRequest(BluetoothReplyRunnable* aRunnable, const Request& aRequest)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRunnable);

  NS_WARNING_ASSERTION(
    sBluetoothChild,
    "Calling methods on BluetoothServiceChildProcess during shutdown!");

  if (sBluetoothChild) {
    BluetoothRequestChild* actor = new BluetoothRequestChild(aRunnable);
    sBluetoothChild->SendPBluetoothRequestConstructor(actor, aRequest);
  }
}

} // namespace

// static
BluetoothServiceChildProcess*
BluetoothServiceChildProcess::Create()
{
  MOZ_ASSERT(!sBluetoothChild);

  mozilla::dom::ContentChild* contentChild =
    mozilla::dom::ContentChild::GetSingleton();
  MOZ_ASSERT(contentChild);

  BluetoothServiceChildProcess* btService = new BluetoothServiceChildProcess();

  sBluetoothChild = new BluetoothChild(btService);
  contentChild->SendPBluetoothConstructor(sBluetoothChild);

  return btService;
}

BluetoothServiceChildProcess::BluetoothServiceChildProcess()
{
}

BluetoothServiceChildProcess::~BluetoothServiceChildProcess()
{
  sBluetoothChild = nullptr;
}

void
BluetoothServiceChildProcess::NoteDeadActor()
{
  MOZ_ASSERT(sBluetoothChild);
  sBluetoothChild = nullptr;
}

void
BluetoothServiceChildProcess::RegisterBluetoothSignalHandler(
                                              const nsAString& aNodeName,
                                              BluetoothSignalObserver* aHandler)
{
  if (sBluetoothChild && !IsSignalRegistered(aNodeName)) {
    sBluetoothChild->SendRegisterSignalHandler(nsString(aNodeName));
  }
  BluetoothService::RegisterBluetoothSignalHandler(aNodeName, aHandler);
}

void
BluetoothServiceChildProcess::UnregisterBluetoothSignalHandler(
                                              const nsAString& aNodeName,
                                              BluetoothSignalObserver* aHandler)
{
  BluetoothService::UnregisterBluetoothSignalHandler(aNodeName, aHandler);
  if (sBluetoothChild && !IsSignalRegistered(aNodeName)) {
    sBluetoothChild->SendUnregisterSignalHandler(nsString(aNodeName));
  }
}

nsresult
BluetoothServiceChildProcess::GetAdaptersInternal(
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, GetAdaptersRequest());
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::StartInternal(BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StartBluetoothRequest());
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::StopInternal(BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StopBluetoothRequest());
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::GetConnectedDevicePropertiesInternal(
                                              uint16_t aServiceUuid,
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, ConnectedDevicePropertiesRequest(aServiceUuid));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::GetPairedDevicePropertiesInternal(
  const nsTArray<BluetoothAddress>& aDeviceAddresses,
  BluetoothReplyRunnable* aRunnable)
{
  PairedDevicePropertiesRequest request;
  request.addresses().AppendElements(aDeviceAddresses);

  SendRequest(aRunnable, request);
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::FetchUuidsInternal(
  const BluetoothAddress& aDeviceAddress, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, FetchUuidsRequest(aDeviceAddress));
  return NS_OK;
}

void
BluetoothServiceChildProcess::StopDiscoveryInternal(
   BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StopDiscoveryRequest());
}

void
BluetoothServiceChildProcess::StartDiscoveryInternal(
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StartDiscoveryRequest());
}

void
BluetoothServiceChildProcess::StopLeScanInternal(
  const BluetoothUuid& aScanUuid,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StopLeScanRequest(aScanUuid));
}

void
BluetoothServiceChildProcess::StartLeScanInternal(
  const nsTArray<BluetoothUuid>& aServiceUuids,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StartLeScanRequest(aServiceUuids));
}

void
BluetoothServiceChildProcess::StartAdvertisingInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothGattAdvertisingData& aAdvData,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StartAdvertisingRequest(aAppUuid, aAdvData));
}

void
BluetoothServiceChildProcess::StopAdvertisingInternal(
  const BluetoothUuid& aAppUuid,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StopAdvertisingRequest(aAppUuid));
}

nsresult
BluetoothServiceChildProcess::SetProperty(BluetoothObjectType aType,
                                          const BluetoothNamedValue& aValue,
                                          BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SetPropertyRequest(aType, aValue));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::CreatePairedDeviceInternal(
  const BluetoothAddress& aDeviceAddress, int aTimeout,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, PairRequest(aDeviceAddress, aTimeout));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::RemoveDeviceInternal(
  const BluetoothAddress& aDeviceAddress, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, UnpairRequest(aDeviceAddress));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::GetServiceChannel(const BluetoothAddress& aDeviceAddress,
                                                const BluetoothUuid& aServiceUuid,
                                                BluetoothProfileManagerBase* aManager)
{
  MOZ_CRASH("This should never be called!");
}

bool
BluetoothServiceChildProcess::UpdateSdpRecords(const BluetoothAddress& aDeviceAddress,
                                               BluetoothProfileManagerBase* aManager)
{
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::PinReplyInternal(
  const BluetoothAddress& aDeviceAddress, bool aAccept,
  const BluetoothPinCode& aPinCode, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, PinReplyRequest(aDeviceAddress, aAccept, aPinCode));
}

void
BluetoothServiceChildProcess::SspReplyInternal(
  const BluetoothAddress& aDeviceAddress,
  BluetoothSspVariant aVariant, bool aAccept,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SspReplyRequest(aDeviceAddress, aVariant, aAccept));
}

void
BluetoothServiceChildProcess::SetPinCodeInternal(
  const BluetoothAddress& aDeviceAddress,
  const BluetoothPinCode& aPinCode,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SetPinCodeRequest(aDeviceAddress, aPinCode));
}

void
BluetoothServiceChildProcess::SetPasskeyInternal(
  const BluetoothAddress& aDeviceAddress,
  uint32_t aPasskey,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SetPasskeyRequest(aDeviceAddress, aPasskey));
}

void
BluetoothServiceChildProcess::SetPairingConfirmationInternal(
                                                const BluetoothAddress& aDeviceAddress,
                                                bool aConfirm,
                                                BluetoothReplyRunnable* aRunnable)
{
  if (aConfirm) {
    SendRequest(aRunnable, ConfirmPairingConfirmationRequest(aDeviceAddress));
  } else {
    SendRequest(aRunnable, DenyPairingConfirmationRequest(aDeviceAddress));
  }
}

void
BluetoothServiceChildProcess::Connect(
  const BluetoothAddress& aDeviceAddress,
  uint32_t aCod, uint16_t aServiceUuid,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, ConnectRequest(aDeviceAddress, aCod, aServiceUuid));
}

void
BluetoothServiceChildProcess::Disconnect(
  const BluetoothAddress& aDeviceAddress,
  uint16_t aServiceUuid,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, DisconnectRequest(aDeviceAddress, aServiceUuid));
}

void
BluetoothServiceChildProcess::SendFile(
  const BluetoothAddress& aDeviceAddress,
  BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SendFileRequest(aDeviceAddress, nullptr, aBlobChild));
}

void
BluetoothServiceChildProcess::SendFile(
  const BluetoothAddress& aDeviceAddress,
  Blob* aBlobChild,
  BluetoothReplyRunnable* aRunnable)
{
  // Parent-process-only method
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::StopSendingFile(
  const BluetoothAddress& aDeviceAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StopSendingFileRequest(aDeviceAddress));
}

void
BluetoothServiceChildProcess::ConfirmReceivingFile(
  const BluetoothAddress& aDeviceAddress, bool aConfirm,
  BluetoothReplyRunnable* aRunnable)
{
  if(aConfirm) {
    SendRequest(aRunnable, ConfirmReceivingFileRequest(aDeviceAddress));
    return;
  }

  SendRequest(aRunnable, DenyReceivingFileRequest(aDeviceAddress));
}

void
BluetoothServiceChildProcess::ConnectSco(BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, ConnectScoRequest());
}

void
BluetoothServiceChildProcess::DisconnectSco(BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, DisconnectScoRequest());
}

void
BluetoothServiceChildProcess::IsScoConnected(BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, IsScoConnectedRequest());
}

void
BluetoothServiceChildProcess::SetObexPassword(
  const nsAString& aPassword,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SetObexPasswordRequest(nsString(aPassword)));
}

void
BluetoothServiceChildProcess::RejectObexAuth(BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, RejectObexAuthRequest());
}

void
BluetoothServiceChildProcess::ReplyTovCardPulling(
  BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, ReplyTovCardPullingRequest(nullptr, aBlobChild));
}

void
BluetoothServiceChildProcess::ReplyTovCardPulling(
  Blob* aBlobChild,
  BluetoothReplyRunnable* aRunnable)
{
  // Parent-process-only method
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::ReplyToPhonebookPulling(
  BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  uint16_t aPhonebookSize,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    ReplyToPhonebookPullingRequest(nullptr, aBlobChild, aPhonebookSize));
}

void
BluetoothServiceChildProcess::ReplyToPhonebookPulling(
  Blob* aBlobChild,
  uint16_t aPhonebookSize,
  BluetoothReplyRunnable* aRunnable)
{
  // Parent-process-only method
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::ReplyTovCardListing(
  BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  uint16_t aPhonebookSize,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    ReplyTovCardListingRequest(nullptr, aBlobChild, aPhonebookSize));
}

void
BluetoothServiceChildProcess::ReplyTovCardListing(
  Blob* aBlobChild,
  uint16_t aPhonebookSize,
  BluetoothReplyRunnable* aRunnable)
{
  // Parent-process-only method
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::ReplyToMapFolderListing(long aMasId,
  const nsAString& aFolderList,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              ReplyToFolderListingRequest(aMasId, nsString(aFolderList)));
}

void
BluetoothServiceChildProcess::ReplyToMapMessagesListing(BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  long aMasId,
  bool aNewMessage,
  const nsAString& aTimestamp,
  int aSize,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              ReplyToMessagesListingRequest(aMasId, nullptr, aBlobChild,
                                            aNewMessage, nsString(aTimestamp), aSize));
}

void
BluetoothServiceChildProcess::ReplyToMapMessagesListing(long aMasId,
  Blob* aBlob,
  bool aNewMessage,
  const nsAString& aTimestamp,
  int aSize,
  BluetoothReplyRunnable* aRunnable)
{
  // Parent-process-only method
  MOZ_CRASH("This should never be called!");
}


void
BluetoothServiceChildProcess::ReplyToMapGetMessage(BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  long aMasId,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    ReplyToGetMessageRequest(aMasId, nullptr, aBlobChild));
}

void
BluetoothServiceChildProcess::ReplyToMapGetMessage(Blob* aBlob,
  long aMasId,
  BluetoothReplyRunnable* aRunnable)
{
  // Parent-process-only method
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::ReplyToMapSetMessageStatus(long aMasId,
  bool aStatus,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    ReplyToSetMessageStatusRequest(aMasId, aStatus));
}

void
BluetoothServiceChildProcess::ReplyToMapSendMessage(long aMasId,
  const nsAString& aHandleId,
  bool aStatus,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    ReplyToSendMessageRequest(aMasId, nsString(aHandleId), aStatus));
}

void
BluetoothServiceChildProcess::ReplyToMapMessageUpdate(long aMasId,
  bool aStatus,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    ReplyToMessageUpdateRequest(aMasId, aStatus));
}

#ifdef MOZ_B2G_RIL
void
BluetoothServiceChildProcess::AnswerWaitingCall(
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, AnswerWaitingCallRequest());
}

void
BluetoothServiceChildProcess::IgnoreWaitingCall(
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, IgnoreWaitingCallRequest());
}

void
BluetoothServiceChildProcess::ToggleCalls(
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, ToggleCallsRequest());
}
#endif // MOZ_B2G_RIL

void
BluetoothServiceChildProcess::SendMetaData(const nsAString& aTitle,
                                           const nsAString& aArtist,
                                           const nsAString& aAlbum,
                                           int64_t aMediaNumber,
                                           int64_t aTotalMediaCount,
                                           int64_t aDuration,
                                           BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              SendMetaDataRequest(nsString(aTitle), nsString(aArtist),
                                  nsString(aAlbum), aMediaNumber,
                                  aTotalMediaCount, aDuration));
}

void
BluetoothServiceChildProcess::SendPlayStatus(int64_t aDuration,
                                             int64_t aPosition,
                                             ControlPlayStatus aPlayStatus,
                                             BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              SendPlayStatusRequest(aDuration, aPosition, aPlayStatus));
}

void
BluetoothServiceChildProcess::ConnectGattClientInternal(
  const BluetoothUuid& aAppUuid, const BluetoothAddress& aDeviceAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, ConnectGattClientRequest(aAppUuid, aDeviceAddress));
}

void
BluetoothServiceChildProcess::DisconnectGattClientInternal(
  const BluetoothUuid& aAppUuid, const BluetoothAddress& aDeviceAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              DisconnectGattClientRequest(aAppUuid, aDeviceAddress));
}

void
BluetoothServiceChildProcess::DiscoverGattServicesInternal(
  const BluetoothUuid& aAppUuid, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, DiscoverGattServicesRequest(aAppUuid));
}

void
BluetoothServiceChildProcess::GattClientStartNotificationsInternal(
  const BluetoothUuid& aAppUuid, const BluetoothGattServiceId& aServId,
  const BluetoothGattId& aCharId, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattClientStartNotificationsRequest(aAppUuid, aServId, aCharId));
}

void
BluetoothServiceChildProcess::GattClientStopNotificationsInternal(
  const BluetoothUuid& aAppUuid, const BluetoothGattServiceId& aServId,
  const BluetoothGattId& aCharId, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattClientStopNotificationsRequest(aAppUuid, aServId, aCharId));
}

void
BluetoothServiceChildProcess::UnregisterGattClientInternal(
  int aClientIf, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, UnregisterGattClientRequest(aClientIf));
}

void
BluetoothServiceChildProcess::GattClientReadRemoteRssiInternal(
  int aClientIf, const BluetoothAddress& aDeviceAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              GattClientReadRemoteRssiRequest(aClientIf, aDeviceAddress));
}

void
BluetoothServiceChildProcess::GattClientReadCharacteristicValueInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattId& aCharacteristicId,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattClientReadCharacteristicValueRequest(aAppUuid,
                                             aServiceId,
                                             aCharacteristicId));
}

void
BluetoothServiceChildProcess::GattClientWriteCharacteristicValueInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattId& aCharacteristicId,
  const BluetoothGattWriteType& aWriteType,
  const nsTArray<uint8_t>& aValue,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattClientWriteCharacteristicValueRequest(aAppUuid,
                                              aServiceId,
                                              aCharacteristicId,
                                              aWriteType,
                                              aValue));
}

void
BluetoothServiceChildProcess::GattClientReadDescriptorValueInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattId& aCharacteristicId,
  const BluetoothGattId& aDescriptorId,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattClientReadDescriptorValueRequest(aAppUuid,
                                         aServiceId,
                                         aCharacteristicId,
                                         aDescriptorId));
}

void
BluetoothServiceChildProcess::GattClientWriteDescriptorValueInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattId& aCharacteristicId,
  const BluetoothGattId& aDescriptorId,
  const nsTArray<uint8_t>& aValue,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattClientWriteDescriptorValueRequest(aAppUuid,
                                          aServiceId,
                                          aCharacteristicId,
                                          aDescriptorId,
                                          aValue));
}

void
BluetoothServiceChildProcess::GattServerRegisterInternal(
  const BluetoothUuid& aAppUuid,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,GattServerRegisterRequest(aAppUuid));
}

void
BluetoothServiceChildProcess::GattServerConnectPeripheralInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAddress& aAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerConnectPeripheralRequest(aAppUuid, aAddress));
}

void
BluetoothServiceChildProcess::GattServerDisconnectPeripheralInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAddress& aAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerDisconnectPeripheralRequest(aAppUuid, aAddress));
}

void
BluetoothServiceChildProcess::UnregisterGattServerInternal(
  int aServerIf, BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, UnregisterGattServerRequest(aServerIf));
}

void
BluetoothServiceChildProcess::GattServerAddServiceInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothGattServiceId& aServiceId,
  uint16_t aHandleCount,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerAddServiceRequest(aAppUuid, aServiceId, aHandleCount));
}

void
BluetoothServiceChildProcess::GattServerAddIncludedServiceInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAttributeHandle& aServiceHandle,
  const BluetoothAttributeHandle& aIncludedServiceHandle,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerAddIncludedServiceRequest(aAppUuid,
                                        aServiceHandle,
                                        aIncludedServiceHandle));
}

void
BluetoothServiceChildProcess::GattServerAddCharacteristicInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAttributeHandle& aServiceHandle,
  const BluetoothUuid& aCharacteristicUuid,
  BluetoothGattAttrPerm aPermissions,
  BluetoothGattCharProp aProperties,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerAddCharacteristicRequest(aAppUuid,
                                       aServiceHandle,
                                       aCharacteristicUuid,
                                       aPermissions,
                                       aProperties));
}

void
BluetoothServiceChildProcess::GattServerAddDescriptorInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAttributeHandle& aServiceHandle,
  const BluetoothAttributeHandle& aCharacteristicHandle,
  const BluetoothUuid& aDescriptorUuid,
  BluetoothGattAttrPerm aPermissions,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerAddDescriptorRequest(aAppUuid,
                                   aServiceHandle,
                                   aCharacteristicHandle,
                                   aDescriptorUuid,
                                   aPermissions));
}

void
BluetoothServiceChildProcess::GattServerRemoveServiceInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAttributeHandle& aServiceHandle,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerRemoveServiceRequest(aAppUuid, aServiceHandle));
}

void
BluetoothServiceChildProcess::GattServerStartServiceInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAttributeHandle& aServiceHandle,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerStartServiceRequest(aAppUuid, aServiceHandle));
}

void
BluetoothServiceChildProcess::GattServerStopServiceInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAttributeHandle& aServiceHandle,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerStopServiceRequest(aAppUuid, aServiceHandle));
}

void
BluetoothServiceChildProcess::GattServerSendResponseInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAddress& aAddress,
  uint16_t aStatus,
  int32_t aRequestId,
  const BluetoothGattResponse& aRsp,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerSendResponseRequest(aAppUuid,
                                  aAddress,
                                  aStatus,
                                  aRequestId,
                                  aRsp));
}

void
BluetoothServiceChildProcess::GattServerSendIndicationInternal(
  const BluetoothUuid& aAppUuid,
  const BluetoothAddress& aAddress,
  const BluetoothAttributeHandle& aCharacteristicHandle,
  bool aConfirm,
  const nsTArray<uint8_t>& aValue,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
    GattServerSendIndicationRequest(aAppUuid,
                                    aAddress,
                                    aCharacteristicHandle,
                                    aConfirm,
                                    aValue));
}

nsresult
BluetoothServiceChildProcess::HandleStartup()
{
  // Don't need to do anything here for startup since our Create function takes
  // care of the actor machinery.
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::HandleShutdown()
{
  // If this process is shutting down then we need to disconnect ourselves from
  // the parent.
  if (sBluetoothChild) {
    sBluetoothChild->BeginShutdown();
  }
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::SendSinkMessage(const nsAString& aDeviceAddresses,
                                              const nsAString& aMessage)
{
  MOZ_CRASH("This should never be called!");
}

nsresult
BluetoothServiceChildProcess::SendInputMessage(const nsAString& aDeviceAddresses,
                                               const nsAString& aMessage)
{
  MOZ_CRASH("This should never be called!");
}

void
BluetoothServiceChildProcess::UpdatePlayStatus(uint32_t aDuration,
                                               uint32_t aPosition,
                                               ControlPlayStatus aPlayStatus)
{
  MOZ_CRASH("This should never be called!");
}

