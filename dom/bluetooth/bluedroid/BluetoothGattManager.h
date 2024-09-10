/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluedroid_BluetoothGattManager_h
#define mozilla_dom_bluetooth_bluedroid_BluetoothGattManager_h

#include "BluetoothCommon.h"
#include "BluetoothInterface.h"
#include "BluetoothProfileManagerBase.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothGattClient;
class BluetoothReplyRunnable;

class BluetoothGattManager final : public nsIObserver
                                 , public BluetoothGattNotificationHandler
{
public:
  static const int MAX_NUM_CLIENTS;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static BluetoothGattManager* Get();
  static void InitGattInterface(BluetoothProfileResultHandler* aRes);
  static void DeinitGattInterface(BluetoothProfileResultHandler* aRes);

  void StartLeScan(const nsTArray<BluetoothUuid>& aServiceUuids,
                   BluetoothReplyRunnable* aRunnable);

  void StopLeScan(const BluetoothUuid& aScanUuid,
                  BluetoothReplyRunnable* aRunnable);

  void StartAdvertising(const BluetoothUuid& aAppUuid,
                        const BluetoothGattAdvertisingData& aData,
                        BluetoothReplyRunnable* aRunnable);

  void StopAdvertising(const BluetoothUuid& aAppUuid,
                       BluetoothReplyRunnable* aRunnable);

  void Connect(const BluetoothUuid& aAppUuid,
               const BluetoothAddress& aDeviceAddr,
               BluetoothReplyRunnable* aRunnable);

  void Disconnect(const BluetoothUuid& aAppUuid,
                  const BluetoothAddress& aDeviceAddr,
                  BluetoothReplyRunnable* aRunnable);

  void Discover(const BluetoothUuid& aAppUuid,
                BluetoothReplyRunnable* aRunnable);

  void UnregisterClient(int aClientIf,
                        BluetoothReplyRunnable* aRunnable);

  void ReadRemoteRssi(int aClientIf,
                      const BluetoothAddress& aDeviceAddr,
                      BluetoothReplyRunnable* aRunnable);

  void RegisterNotifications(const BluetoothUuid& aAppUuid,
                             const BluetoothGattServiceId& aServId,
                             const BluetoothGattId& aCharId,
                             BluetoothReplyRunnable* aRunnable);

  void DeregisterNotifications(const BluetoothUuid& aAppUuid,
                               const BluetoothGattServiceId& aServId,
                               const BluetoothGattId& aCharId,
                               BluetoothReplyRunnable* aRunnable);

  void ReadCharacteristicValue(
    const BluetoothUuid& aAppUuid,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharacteristicId,
    BluetoothReplyRunnable* aRunnable);

  void WriteCharacteristicValue(
    const BluetoothUuid& aAppUuid,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharacteristicId,
    const BluetoothGattWriteType& aWriteType,
    const nsTArray<uint8_t>& aValue,
    BluetoothReplyRunnable* aRunnable);

  void ReadDescriptorValue(
    const BluetoothUuid& aAppUuid,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharacteristicId,
    const BluetoothGattId& aDescriptorId,
    BluetoothReplyRunnable* aRunnable);

  void WriteDescriptorValue(
    const BluetoothUuid& aAppUuid,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharacteristicId,
    const BluetoothGattId& aDescriptorId,
    const nsTArray<uint8_t>& aValue,
    BluetoothReplyRunnable* aRunnable);

  void RegisterServer(const BluetoothUuid& aAppUuid,
                      BluetoothReplyRunnable* aRunnable);

  void ConnectPeripheral(
    const BluetoothUuid& aAppUuid,
    const BluetoothAddress& aAddress,
    BluetoothReplyRunnable* aRunnable);

  void DisconnectPeripheral(
    const BluetoothUuid& aAppUuid,
    const BluetoothAddress& aAddress,
    BluetoothReplyRunnable* aRunnable);

  void UnregisterServer(int aServerIf,
                        BluetoothReplyRunnable* aRunnable);

  void ServerAddService(
    const BluetoothUuid& aAppUuid,
    const BluetoothGattServiceId& aServiceId,
    uint16_t aHandleCount,
    BluetoothReplyRunnable* aRunnable);

  void ServerAddIncludedService(
    const BluetoothUuid& aAppUuid,
    const BluetoothAttributeHandle& aServiceHandle,
    const BluetoothAttributeHandle& aIncludedServiceHandle,
    BluetoothReplyRunnable* aRunnable);

  void ServerAddCharacteristic(
    const BluetoothUuid& aAppUuid,
    const BluetoothAttributeHandle& aServiceHandle,
    const BluetoothUuid& aCharacteristicUuid,
    BluetoothGattAttrPerm aPermissions,
    BluetoothGattCharProp aProperties,
    BluetoothReplyRunnable* aRunnable);

  void ServerAddDescriptor(
    const BluetoothUuid& aAppUuid,
    const BluetoothAttributeHandle& aServiceHandle,
    const BluetoothAttributeHandle& aCharacteristicHandle,
    const BluetoothUuid& aDescriptorUuid,
    BluetoothGattAttrPerm aPermissions,
    BluetoothReplyRunnable* aRunnable);

  void ServerRemoveService(
    const BluetoothUuid& aAppUuid,
    const BluetoothAttributeHandle& aServiceHandle,
    BluetoothReplyRunnable* aRunnable);

  void ServerStartService(
    const BluetoothUuid& aAppUuid,
    const BluetoothAttributeHandle& aServiceHandle,
    BluetoothReplyRunnable* aRunnable);

  void ServerStopService(
    const BluetoothUuid& aAppUuid,
    const BluetoothAttributeHandle& aServiceHandle,
    BluetoothReplyRunnable* aRunnable);

  void ServerSendResponse(
    const BluetoothUuid& aAppUuid,
    const BluetoothAddress& aAddress,
    uint16_t aStatus,
    int32_t aRequestId,
    const BluetoothGattResponse& aRsp,
    BluetoothReplyRunnable* aRunnable);

  void ServerSendIndication(
    const BluetoothUuid& aAppUuid,
    const BluetoothAddress& aAddress,
    const BluetoothAttributeHandle& aCharacteristicHandle,
    bool aConfirm,
    const nsTArray<uint8_t>& aValue,
    BluetoothReplyRunnable* aRunnable);

private:
  ~BluetoothGattManager();

  class DeinitProfileResultHandlerRunnable;
  class InitProfileResultHandlerRunnable;
  class RegisterModuleResultHandler;
  class UnregisterModuleResultHandler;

  class RegisterClientResultHandler;
  class UnregisterClientResultHandler;
  class StartLeScanResultHandler;
  class StopLeScanResultHandler;
  class StartAdvertisingResultHandler;
  class SetAdvDataResultHandler;
  class StopAdvertisingResultHandler;
  class ConnectResultHandler;
  class DisconnectResultHandler;
  class DiscoverResultHandler;
  class ReadRemoteRssiResultHandler;
  class RegisterNotificationsResultHandler;
  class DeregisterNotificationsResultHandler;
  class ReadCharacteristicValueResultHandler;
  class WriteCharacteristicValueResultHandler;
  class ReadDescriptorValueResultHandler;
  class WriteDescriptorValueResultHandler;
  class ScanDeviceTypeResultHandler;

  class RegisterServerResultHandler;
  class ConnectPeripheralResultHandler;
  class DisconnectPeripheralResultHandler;
  class UnregisterServerResultHandler;
  class ServerAddServiceResultHandler;
  class ServerAddIncludedServiceResultHandler;
  class ServerAddCharacteristicResultHandler;
  class ServerAddDescriptorResultHandler;
  class ServerRemoveDescriptorResultHandler;
  class ServerStartServiceResultHandler;
  class ServerStopServiceResultHandler;
  class ServerSendResponseResultHandler;
  class ServerSendIndicationResultHandler;

  BluetoothGattManager();

  nsresult Init();
  void Uninit();
  void HandleShutdown();

  void RegisterClientNotification(BluetoothGattStatus aStatus,
                                  int aClientIf,
                                  const BluetoothUuid& aAppUuid) override;

  void ScanResultNotification(
    const BluetoothAddress& aBdAddr, int aRssi,
    const BluetoothGattAdvData& aAdvData) override;

  void ConnectNotification(int aConnId,
                           BluetoothGattStatus aStatus,
                           int aClientIf,
                           const BluetoothAddress& aBdAddr) override;

  void DisconnectNotification(int aConnId,
                              BluetoothGattStatus aStatus,
                              int aClientIf,
                              const BluetoothAddress& aBdAddr) override;

  void SearchCompleteNotification(int aConnId,
                                  BluetoothGattStatus aStatus) override;

  void SearchResultNotification(int aConnId,
                                const BluetoothGattServiceId& aServiceId)
                                override;

  void GetCharacteristicNotification(
    int aConnId, BluetoothGattStatus aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharId,
    const BluetoothGattCharProp& aCharProperty) override;

  void GetDescriptorNotification(
    int aConnId, BluetoothGattStatus aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharId,
    const BluetoothGattId& aDescriptorId) override;

  void GetIncludedServiceNotification(
    int aConnId, BluetoothGattStatus aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattServiceId& aIncludedServId) override;

  void RegisterNotificationNotification(
    int aConnId, int aIsRegister, BluetoothGattStatus aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharId) override;

  void NotifyNotification(int aConnId,
                          const BluetoothGattNotifyParam& aNotifyParam)
                          override;

  void ReadCharacteristicNotification(int aConnId,
                                      BluetoothGattStatus aStatus,
                                      const BluetoothGattReadParam& aReadParam)
                                      override;

  void WriteCharacteristicNotification(
    int aConnId, BluetoothGattStatus aStatus,
    const BluetoothGattWriteParam& aWriteParam) override;

  void ReadDescriptorNotification(int aConnId,
                                  BluetoothGattStatus aStatus,
                                  const BluetoothGattReadParam& aReadParam)
                                  override;

  void WriteDescriptorNotification(int aConnId,
                                   BluetoothGattStatus aStatus,
                                   const BluetoothGattWriteParam& aWriteParam)
                                   override;

  void ExecuteWriteNotification(int aConnId,
                                BluetoothGattStatus aStatus) override;

  void ReadRemoteRssiNotification(int aClientIf,
                                  const BluetoothAddress& aBdAddr,
                                  int aRssi,
                                  BluetoothGattStatus aStatus) override;

  void ListenNotification(BluetoothGattStatus aStatus,
                          int aServerIf) override;

  void ProceedDiscoverProcess(BluetoothGattClient* aClient,
                              const BluetoothGattServiceId& aServiceId);

  void RegisterServerNotification(BluetoothGattStatus aStatus,
                                  int aServerIf,
                                  const BluetoothUuid& aAppUuid) override;

  void ConnectionNotification(int aConnId,
                              int aServerIf,
                              bool aConnected,
                              const BluetoothAddress& aBdAddr) override;

  void
  ServiceAddedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothAttributeHandle& aServiceHandle) override;

  void
  IncludedServiceAddedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothAttributeHandle& aServiceHandle,
    const BluetoothAttributeHandle& aIncludedServiceHandle) override;

  void
  CharacteristicAddedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothUuid& aCharId,
    const BluetoothAttributeHandle& aServiceHandle,
    const BluetoothAttributeHandle& aCharacteristicHandle) override;

  void
  DescriptorAddedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothUuid& aCharId,
    const BluetoothAttributeHandle& aServiceHandle,
    const BluetoothAttributeHandle& aDescriptorHandle) override;

  void
  ServiceStartedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothAttributeHandle& aServiceHandle) override;

  void
  ServiceStoppedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothAttributeHandle& aServiceHandle) override;

  void
  ServiceDeletedNotification(
    BluetoothGattStatus aStatus,
    int aServerIf,
    const BluetoothAttributeHandle& aServiceHandle) override;

  void
  RequestReadNotification(int aConnId,
                          int aTransId,
                          const BluetoothAddress& aBdAddr,
                          const BluetoothAttributeHandle& aAttributeHandle,
                          int aOffset,
                          bool aIsLong) override;

  void
  RequestWriteNotification(int aConnId,
                           int aTransId,
                           const BluetoothAddress& aBdAddr,
                           const BluetoothAttributeHandle& aAttributeHandle,
                           int aOffset,
                           int aLength,
                           const uint8_t* aValue,
                           bool aNeedResponse,
                           bool aIsPrepareWrite) override;

  static bool mInShutdown;
};

END_BLUETOOTH_NAMESPACE

#endif // mozilla_dom_bluetooth_bluedroid_BluetoothGattManager_h
