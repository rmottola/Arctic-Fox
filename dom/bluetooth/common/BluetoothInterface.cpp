/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothInterface.h"
#if ANDROID_VERSION >= 17
#include <cutils/properties.h>
#endif
#ifdef MOZ_B2G_BT_DAEMON
#include "BluetoothDaemonInterface.h"
#endif

BEGIN_BLUETOOTH_NAMESPACE

//
// Setup Interface
//

// Result handling
//

void
BluetoothSetupResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothSetupResultHandler::RegisterModule()
{ }

void
BluetoothSetupResultHandler::UnregisterModule()
{ }

void
BluetoothSetupResultHandler::Configuration()
{ }

// Interface
//

BluetoothSetupInterface::~BluetoothSetupInterface()
{ }

//
// Bluetooth Core Interface
//

// Notification handling
//

BluetoothCoreNotificationHandler::BluetoothCoreNotificationHandler()
{ }

BluetoothCoreNotificationHandler::~BluetoothCoreNotificationHandler()
{ }

void
BluetoothCoreNotificationHandler::AdapterStateChangedNotification(bool aState)
{ }

void
BluetoothCoreNotificationHandler::AdapterPropertiesNotification(
  BluetoothStatus aStatus,int aNumProperties,
  const BluetoothProperty* aProperties)
{ }

void
BluetoothCoreNotificationHandler::RemoteDevicePropertiesNotification(
  BluetoothStatus aStatus, const BluetoothAddress& aBdAddr,
  int aNumProperties, const BluetoothProperty* aProperties)
{ }

void
BluetoothCoreNotificationHandler::DeviceFoundNotification(
  int aNumProperties, const BluetoothProperty* aProperties)
{ }

void
BluetoothCoreNotificationHandler::DiscoveryStateChangedNotification(
  bool aState)
{ }

void
BluetoothCoreNotificationHandler::PinRequestNotification(
  const BluetoothAddress& aRemoteBdAddr, const BluetoothRemoteName& aBdName,
  uint32_t aCod)
{ }

void
BluetoothCoreNotificationHandler::SspRequestNotification(
  const BluetoothAddress& aRemoteBdAddr, const BluetoothRemoteName& aBdName,
  uint32_t aCod, BluetoothSspVariant aPairingVariant, uint32_t aPassKey)
{ }

void
BluetoothCoreNotificationHandler::BondStateChangedNotification(
  BluetoothStatus aStatus, const BluetoothAddress& aRemoteBdAddr,
  BluetoothBondState aState)
{ }

void
BluetoothCoreNotificationHandler::AclStateChangedNotification(
  BluetoothStatus aStatus, const BluetoothAddress& aRemoteBdAddr,
  BluetoothAclState aState)
{ }

void
BluetoothCoreNotificationHandler::DutModeRecvNotification(
  uint16_t aOpcode,
  const uint8_t* aBuf,
  uint8_t aLen)
{ }

void
BluetoothCoreNotificationHandler::LeTestModeNotification(
  BluetoothStatus aStatus,
  uint16_t aNumPackets)
{ }

void
BluetoothCoreNotificationHandler::EnergyInfoNotification(
  const BluetoothActivityEnergyInfo& aInfo)
{ }

// Result handling
//

void
BluetoothCoreResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_LOGR("Received error code %d", aStatus);
}

void
BluetoothCoreResultHandler::Enable()
{ }

void
BluetoothCoreResultHandler::Disable()
{ }

void
BluetoothCoreResultHandler::GetAdapterProperties()
{ }

void
BluetoothCoreResultHandler::GetAdapterProperty()
{ }

void
BluetoothCoreResultHandler::SetAdapterProperty()
{ }

void
BluetoothCoreResultHandler::GetRemoteDeviceProperties()
{ }

void
BluetoothCoreResultHandler::GetRemoteDeviceProperty()
{ }

void
BluetoothCoreResultHandler::SetRemoteDeviceProperty()
{ }

void
BluetoothCoreResultHandler::GetRemoteServiceRecord()
{ }

void
BluetoothCoreResultHandler::GetRemoteServices()
{ }

void
BluetoothCoreResultHandler::StartDiscovery()
{ }

void
BluetoothCoreResultHandler::CancelDiscovery()
{ }

void
BluetoothCoreResultHandler::CreateBond()
{ }

void
BluetoothCoreResultHandler::RemoveBond()
{ }

void
BluetoothCoreResultHandler::CancelBond()
{ }

void
BluetoothCoreResultHandler::GetConnectionState()
{ }

void
BluetoothCoreResultHandler::PinReply()
{ }

void
BluetoothCoreResultHandler::SspReply()
{ }

void
BluetoothCoreResultHandler::DutModeConfigure()
{ }

void
BluetoothCoreResultHandler::DutModeSend()
{ }

void
BluetoothCoreResultHandler::LeTestMode()
{ }

void
BluetoothCoreResultHandler::ReadEnergyInfo()
{ }

// Interface
//

BluetoothCoreInterface::BluetoothCoreInterface()
{ }

BluetoothCoreInterface::~BluetoothCoreInterface()
{ }

//
// Socket Interface
//

// Result handling
//

void
BluetoothSocketResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothSocketResultHandler::Listen(int aSockFd)
{ }

void
BluetoothSocketResultHandler::Connect(int aSockFd,
                                      const BluetoothAddress& aBdAddress,
                                      int aConnectionState)
{ }

void
BluetoothSocketResultHandler::Accept(int aSockFd,
                                     const BluetoothAddress& aBdAddress,
                                     int aConnectionState)
{ }

// Interface
//

BluetoothSocketInterface::~BluetoothSocketInterface()
{ }

//
//Hid Interface
//

//Notification handling
//

BluetoothHidNotificationHandler::BluetoothHidNotificationHandler()
{ }

BluetoothHidNotificationHandler::~BluetoothHidNotificationHandler()
{ }

void
BluetoothHidNotificationHandler::ConnectionStateNotification(
  const BluetoothAddress& aBdAddr, BluetoothHidConnectionState aState)
{ }

void
BluetoothHidNotificationHandler::HidInfoNotification(
  const BluetoothAddress& aBdAddr,
  const BluetoothHidInfoParam& aHidInfoParam)
{ }

void
BluetoothHidNotificationHandler::ProtocolModeNotification(
  const BluetoothAddress& aBdAddr, BluetoothHidStatus aStatus,
  BluetoothHidProtocolMode aProtocolMode)
{ }

void
BluetoothHidNotificationHandler::IdleTimeNotification(
  const BluetoothAddress& aBdAddr,
  BluetoothHidStatus aStatus, uint16_t aIdleTime)
{ }

void
BluetoothHidNotificationHandler::GetReportNotification(
  const BluetoothAddress& aBdAddr, BluetoothHidStatus aStatus,
  const BluetoothHidReport& aReport)
{ }

void
BluetoothHidNotificationHandler::VirtualUnplugNotification(
  const BluetoothAddress& aBdAddr, BluetoothHidStatus aStatus)
{ }

void
BluetoothHidNotificationHandler::HandshakeNotification(
  const BluetoothAddress& aBdAddr, BluetoothHidStatus aStatus)
{ }

// Result handling
//

void BluetoothHidResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothHidResultHandler::Connect()
{ }

void
BluetoothHidResultHandler::Disconnect()
{ }

void
BluetoothHidResultHandler::VirtualUnplug()
{ }

void
BluetoothHidResultHandler::SetInfo()
{ }

void
BluetoothHidResultHandler::GetProtocol()
{ }

void
BluetoothHidResultHandler::SetProtocol()
{ }

void
BluetoothHidResultHandler::GetReport()
{ }

void
BluetoothHidResultHandler::SetReport()
{ }

void
BluetoothHidResultHandler::SendData()
{ }

// Interface
//

BluetoothHidInterface::BluetoothHidInterface()
{ }

BluetoothHidInterface::~BluetoothHidInterface()
{ }

//
// Handsfree Interface
//

// Notification handling
//

BluetoothHandsfreeNotificationHandler::BluetoothHandsfreeNotificationHandler()
{ }

BluetoothHandsfreeNotificationHandler::~BluetoothHandsfreeNotificationHandler()
{ }

void
BluetoothHandsfreeNotificationHandler::ConnectionStateNotification(
  BluetoothHandsfreeConnectionState aState, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::AudioStateNotification(
  BluetoothHandsfreeAudioState aState, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::VoiceRecognitionNotification(
  BluetoothHandsfreeVoiceRecognitionState aState, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::AnswerCallNotification(
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::HangupCallNotification(
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::VolumeNotification(
  BluetoothHandsfreeVolumeType aType, int aVolume, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::DialCallNotification(
  const nsAString& aNumber, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::DtmfNotification(
  char aDtmf, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::NRECNotification(
  BluetoothHandsfreeNRECState aNrec, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::WbsNotification(
  BluetoothHandsfreeWbsConfig aWbs, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::CallHoldNotification(
  BluetoothHandsfreeCallHoldType aChld, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::CnumNotification(
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::CindNotification(
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::CopsNotification(
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::ClccNotification(
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::UnknownAtNotification(
  const nsACString& aAtString, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothHandsfreeNotificationHandler::KeyPressedNotification(
  const BluetoothAddress& aBdAddr)
{ }

// Result handling
//

void
BluetoothHandsfreeResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothHandsfreeResultHandler::Connect()
{ }

void
BluetoothHandsfreeResultHandler::Disconnect()
{ }

void
BluetoothHandsfreeResultHandler::ConnectAudio()
{ }

void
BluetoothHandsfreeResultHandler::DisconnectAudio()
{ }

void
BluetoothHandsfreeResultHandler::StartVoiceRecognition()
{ }

void
BluetoothHandsfreeResultHandler::StopVoiceRecognition()
{ }

void
BluetoothHandsfreeResultHandler::VolumeControl()
{ }

void
BluetoothHandsfreeResultHandler::DeviceStatusNotification()
{ }

void
BluetoothHandsfreeResultHandler::CopsResponse()
{ }

void
BluetoothHandsfreeResultHandler::CindResponse()
{ }

void
BluetoothHandsfreeResultHandler::FormattedAtResponse()
{ }

void
BluetoothHandsfreeResultHandler::AtResponse()
{ }

void
BluetoothHandsfreeResultHandler::ClccResponse()
{ }

void
BluetoothHandsfreeResultHandler::PhoneStateChange()
{ }

void
BluetoothHandsfreeResultHandler::ConfigureWbs()
{ }

// Interface
//

BluetoothHandsfreeInterface::BluetoothHandsfreeInterface()
{ }

BluetoothHandsfreeInterface::~BluetoothHandsfreeInterface()
{ }

//
// Bluetooth Advanced Audio Interface
//

// Notification handling
//

BluetoothA2dpNotificationHandler::BluetoothA2dpNotificationHandler()
{ }

BluetoothA2dpNotificationHandler::~BluetoothA2dpNotificationHandler()
{ }

void
BluetoothA2dpNotificationHandler::ConnectionStateNotification(
  BluetoothA2dpConnectionState aState, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothA2dpNotificationHandler::AudioStateNotification(
  BluetoothA2dpAudioState aState, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothA2dpNotificationHandler::AudioConfigNotification(
  const BluetoothAddress& aBdAddr, uint32_t aSampleRate, uint8_t aChannelCount)
{ }

// Result handling
//

void
BluetoothA2dpResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothA2dpResultHandler::Connect()
{ }

void
BluetoothA2dpResultHandler::Disconnect()
{ }

// Interface
//

BluetoothA2dpInterface::BluetoothA2dpInterface()
{ }

BluetoothA2dpInterface::~BluetoothA2dpInterface()
{ }

//
// Bluetooth AVRCP Interface
//

// Notification handling
//

BluetoothAvrcpNotificationHandler::BluetoothAvrcpNotificationHandler()
{ }

BluetoothAvrcpNotificationHandler::~BluetoothAvrcpNotificationHandler()
{ }

void
BluetoothAvrcpNotificationHandler::GetPlayStatusNotification()
{ }

void
BluetoothAvrcpNotificationHandler::ListPlayerAppAttrNotification()
{ }

void
BluetoothAvrcpNotificationHandler::ListPlayerAppValuesNotification(
  BluetoothAvrcpPlayerAttribute aAttrId)
{ }

void
BluetoothAvrcpNotificationHandler::GetPlayerAppValueNotification(
  uint8_t aNumAttrs, const BluetoothAvrcpPlayerAttribute* aAttrs)
{ }

void
BluetoothAvrcpNotificationHandler::GetPlayerAppAttrsTextNotification(
  uint8_t aNumAttrs, const BluetoothAvrcpPlayerAttribute* aAttrs)
{ }

void
BluetoothAvrcpNotificationHandler::GetPlayerAppValuesTextNotification(
  uint8_t aAttrId, uint8_t aNumVals, const uint8_t* aValues)
{ }

void
BluetoothAvrcpNotificationHandler::SetPlayerAppValueNotification(
  const BluetoothAvrcpPlayerSettings& aSettings)
{ }

void
BluetoothAvrcpNotificationHandler::GetElementAttrNotification(
  uint8_t aNumAttrs, const BluetoothAvrcpMediaAttribute* aAttrs)
{ }

void
BluetoothAvrcpNotificationHandler::RegisterNotificationNotification(
  BluetoothAvrcpEvent aEvent, uint32_t aParam)
{ }

void
BluetoothAvrcpNotificationHandler::RemoteFeatureNotification(
  const BluetoothAddress& aBdAddr, unsigned long aFeatures)
{ }

void
BluetoothAvrcpNotificationHandler::VolumeChangeNotification(
  uint8_t aVolume, uint8_t aCType)
{ }

void
BluetoothAvrcpNotificationHandler::PassthroughCmdNotification(
  uint8_t aId, uint8_t aKeyState)
{ }

// Result handling
//

void
BluetoothAvrcpResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothAvrcpResultHandler::GetPlayStatusRsp()
{ }

void
BluetoothAvrcpResultHandler::ListPlayerAppAttrRsp()
{ }

void
BluetoothAvrcpResultHandler::ListPlayerAppValueRsp()
{ }

void
BluetoothAvrcpResultHandler::GetPlayerAppValueRsp()
{ }

void
BluetoothAvrcpResultHandler::GetPlayerAppAttrTextRsp()
{ }

void
BluetoothAvrcpResultHandler::GetPlayerAppValueTextRsp()
{ }

void
BluetoothAvrcpResultHandler::GetElementAttrRsp()
{ }

void
BluetoothAvrcpResultHandler::SetPlayerAppValueRsp()
{ }

void
BluetoothAvrcpResultHandler::RegisterNotificationRsp()
{ }

void
BluetoothAvrcpResultHandler::SetVolume()
{ }

// Interface
//

BluetoothAvrcpInterface::BluetoothAvrcpInterface()
{ }

BluetoothAvrcpInterface::~BluetoothAvrcpInterface()
{ }

//
// Bluetooth GATT Interface
//

// Notification handling
//

BluetoothGattNotificationHandler::BluetoothGattNotificationHandler()
{ }

BluetoothGattNotificationHandler::~BluetoothGattNotificationHandler()
{ }

void
BluetoothGattNotificationHandler::RegisterClientNotification(
  BluetoothGattStatus aStatus, int aClientIf, const BluetoothUuid& aAppUuid)
{ }

void
BluetoothGattNotificationHandler::ScanResultNotification(
  const BluetoothAddress& aBdAddr, int aRssi, const BluetoothGattAdvData& aAdvData)
{ }

void
BluetoothGattNotificationHandler::ConnectNotification(
  int aConnId, BluetoothGattStatus aStatus, int aClientIf,
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothGattNotificationHandler::DisconnectNotification(
  int aConnId, BluetoothGattStatus aStatus, int aClientIf,
  const BluetoothAddress& aBdAddr)
{ }

void
BluetoothGattNotificationHandler::SearchCompleteNotification(
  int aConnId, BluetoothGattStatus aStatus)
{ }

void
BluetoothGattNotificationHandler::SearchResultNotification(
  int aConnId, const BluetoothGattServiceId& aServiceId)
{ }

void
BluetoothGattNotificationHandler::GetCharacteristicNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattId& aCharId,
  const BluetoothGattCharProp& aCharProperty)
{ }

void
BluetoothGattNotificationHandler::GetDescriptorNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattId& aCharId,
  const BluetoothGattId& aDescriptorId)
{ }

void
BluetoothGattNotificationHandler::GetIncludedServiceNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothGattServiceId& aIncludedServId)
{ }

void
BluetoothGattNotificationHandler::RegisterNotificationNotification(
  int aConnId, int aIsRegister, BluetoothGattStatus aStatus,
  const BluetoothGattServiceId& aServiceId, const BluetoothGattId& aCharId)
{ }

void
BluetoothGattNotificationHandler::NotifyNotification(
  int aConnId, const BluetoothGattNotifyParam& aNotifyParam)
{ }

void
BluetoothGattNotificationHandler::ReadCharacteristicNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattReadParam& aReadParam)
{ }

void
BluetoothGattNotificationHandler::WriteCharacteristicNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattWriteParam& aWriteParam)
{ }

void
BluetoothGattNotificationHandler::ReadDescriptorNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattReadParam& aReadParam)
{ }

void
BluetoothGattNotificationHandler::WriteDescriptorNotification(
  int aConnId, BluetoothGattStatus aStatus,
  const BluetoothGattWriteParam& aWriteParam)
{ }

void
BluetoothGattNotificationHandler::ExecuteWriteNotification(
  int aConnId, BluetoothGattStatus aStatus)
{ }

void
BluetoothGattNotificationHandler::ReadRemoteRssiNotification(
  int aClientIf, const BluetoothAddress& aBdAddr, int aRssi,
  BluetoothGattStatus aStatus)
{ }

void
BluetoothGattNotificationHandler::ListenNotification(
  BluetoothGattStatus aStatus, int aServerIf)
{ }

void
BluetoothGattNotificationHandler::RegisterServerNotification(
  BluetoothGattStatus aStatus, int aServerIf, const BluetoothUuid& aAppUuid)
{ }

void
BluetoothGattNotificationHandler::ConnectionNotification(
  int aConnId, int aServerIf, bool aConnected, const BluetoothAddress& aBdAddr)
{ }

void
BluetoothGattNotificationHandler::ServiceAddedNotification(
  BluetoothGattStatus aStatus, int aServerIf,
  const BluetoothGattServiceId& aServiceId,
  const BluetoothAttributeHandle& aServiceHandle)
{ }

void
BluetoothGattNotificationHandler::IncludedServiceAddedNotification(
  BluetoothGattStatus aStatus, int aServerIf,
  const BluetoothAttributeHandle& aServiceHandle,
  const BluetoothAttributeHandle& aIncludedServiceHandle)
{ }

void
BluetoothGattNotificationHandler::CharacteristicAddedNotification(
  BluetoothGattStatus aStatus, int aServerIf, const BluetoothUuid& aCharId,
  const BluetoothAttributeHandle& aServiceHandle,
  const BluetoothAttributeHandle& aCharacteristicHandle)
{ }

void
BluetoothGattNotificationHandler::DescriptorAddedNotification(
  BluetoothGattStatus aStatus, int aServerIf, const BluetoothUuid& aCharId,
  const BluetoothAttributeHandle& aServiceHandle,
  const BluetoothAttributeHandle& aDescriptorHandle)
{ }

void
BluetoothGattNotificationHandler::ServiceStartedNotification(
  BluetoothGattStatus aStatus, int aServerIf,
  const BluetoothAttributeHandle& aServiceHandle)
{ }

void
BluetoothGattNotificationHandler::ServiceStoppedNotification(
  BluetoothGattStatus aStatus, int aServerIf,
  const BluetoothAttributeHandle& aServiceHandle)
{ }

void
BluetoothGattNotificationHandler::ServiceDeletedNotification(
  BluetoothGattStatus aStatus, int aServerIf,
  const BluetoothAttributeHandle& aServiceHandle)
{ }

void
BluetoothGattNotificationHandler::RequestReadNotification(
  int aConnId, int aTransId, const BluetoothAddress& aBdAddr,
  const BluetoothAttributeHandle& aAttributeHandle, int aOffset, bool aIsLong)
{ }

void
BluetoothGattNotificationHandler::RequestWriteNotification(
  int aConnId, int aTransId, const BluetoothAddress& aBdAddr,
  const BluetoothAttributeHandle& aAttributeHandle, int aOffset, int aLength,
  const uint8_t* aValue, bool aNeedResponse, bool aIsPrepareWrite)
{ }

void
BluetoothGattNotificationHandler::RequestExecuteWriteNotification(
  int aConnId, int aTransId, const BluetoothAddress& aBdAddr, bool aExecute)
{ }

void
BluetoothGattNotificationHandler::ResponseConfirmationNotification(
  BluetoothGattStatus aStatus, int aHandle)
{ }

void
BluetoothGattNotificationHandler::IndicationSentNotification(
  int aConnId, BluetoothGattStatus aStatus)
{ }

void
BluetoothGattNotificationHandler::CongestionNotification(int aConnId,
                                                         bool aCongested)
{ }

void
BluetoothGattNotificationHandler::MtuChangedNotification(int aConnId,
                                                         int aMtu)
{ }

// Result handling
//

void
BluetoothGattResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_WARNING("Received error code %d", (int)aStatus);
}

void
BluetoothGattResultHandler::RegisterClient()
{ }

void
BluetoothGattResultHandler::UnregisterClient()
{ }

void
BluetoothGattResultHandler::Scan()
{ }

void
BluetoothGattResultHandler::Connect()
{ }

void
BluetoothGattResultHandler::Disconnect()
{ }

void
BluetoothGattResultHandler::Listen()
{ }

void
BluetoothGattResultHandler::Refresh()
{ }

void
BluetoothGattResultHandler::SearchService()
{ }

void
BluetoothGattResultHandler::GetIncludedService()
{ }

void
BluetoothGattResultHandler::GetCharacteristic()
{ }

void
BluetoothGattResultHandler::GetDescriptor()
{ }

void
BluetoothGattResultHandler::ReadCharacteristic()
{ }

void
BluetoothGattResultHandler::WriteCharacteristic()
{ }

void
BluetoothGattResultHandler::ReadDescriptor()
{ }

void
BluetoothGattResultHandler::WriteDescriptor()
{ }

void
BluetoothGattResultHandler::ExecuteWrite()
{ }

void
BluetoothGattResultHandler::RegisterNotification()
{ }

void
BluetoothGattResultHandler::DeregisterNotification()
{ }

void
BluetoothGattResultHandler::ReadRemoteRssi()
{ }

void
BluetoothGattResultHandler::GetDeviceType(BluetoothTypeOfDevice aType)
{ }

void
BluetoothGattResultHandler::SetAdvData()
{ }

void
BluetoothGattResultHandler::TestCommand()
{ }

void
BluetoothGattResultHandler::RegisterServer()
{ }

void
BluetoothGattResultHandler::UnregisterServer()
{ }

void
BluetoothGattResultHandler::ConnectPeripheral()
{ }

void
BluetoothGattResultHandler::DisconnectPeripheral()
{ }

void
BluetoothGattResultHandler::AddService()
{ }

void
BluetoothGattResultHandler::AddIncludedService()
{ }

void
BluetoothGattResultHandler::AddCharacteristic()
{ }

void
BluetoothGattResultHandler::AddDescriptor()
{ }

void
BluetoothGattResultHandler::StartService()
{ }

void
BluetoothGattResultHandler::StopService()
{ }

void
BluetoothGattResultHandler::DeleteService()
{ }

void
BluetoothGattResultHandler::SendIndication()
{ }

void
BluetoothGattResultHandler::SendResponse()
{ }

// Interface
//

BluetoothGattInterface::BluetoothGattInterface()
{ }

BluetoothGattInterface::~BluetoothGattInterface()
{ }

//
// Bluetooth Interface
//

// Notification handling
//

BluetoothNotificationHandler::BluetoothNotificationHandler()
{ }

BluetoothNotificationHandler::~BluetoothNotificationHandler()
{ }

void
BluetoothNotificationHandler::BackendErrorNotification(bool aCrashed)
{ }

// Result handling
//

void
BluetoothResultHandler::OnError(BluetoothStatus aStatus)
{
  BT_LOGR("Received error code %d", aStatus);
}

void
BluetoothResultHandler::Init()
{ }

void
BluetoothResultHandler::Cleanup()
{ }

// Interface
//

BluetoothInterface*
BluetoothInterface::GetInstance()
{
#if ANDROID_VERSION >= 17
  /* We pick a default backend from the available ones. The options are
   * ordered by preference. If a backend is supported but not available
   * on the current system, we pick the next one. The selected default
   * can be overriden manually by storing the respective string in the
   * system property 'ro.moz.bluetooth.backend'.
   */

  static const char* const sDefaultBackend[] = {
#ifdef MOZ_B2G_BT_DAEMON
    "bluetoothd",
#endif
    nullptr // no default backend; must be final element in array
  };

  const char* defaultBackend;

  for (size_t i = 0; i < MOZ_ARRAY_LENGTH(sDefaultBackend); ++i) {

    /* select current backend */
    defaultBackend = sDefaultBackend[i];

    if (defaultBackend) {
      if (!strcmp(defaultBackend, "bluetoothd") &&
          access("/init.bluetooth.rc", F_OK) == -1) {
        continue; /* bluetoothd not available */
      }
    }
    break;
  }

  char value[PROPERTY_VALUE_MAX];
  int len;

  len = property_get("ro.moz.bluetooth.backend", value, defaultBackend);
  if (len < 0) {
    BT_WARNING("No Bluetooth backend available.");
    return nullptr;
  }

  const nsDependentCString backend(value, len);

  /* Here's where we decide which implementation to use. Currently
   * there is only Bluedroid and the Bluetooth daemon, but others are
   * possible. Having multiple interfaces built-in and selecting the
   * correct one at runtime is also an option.
   */

#ifdef MOZ_B2G_BT_DAEMON
  if (backend.LowerCaseEqualsLiteral("bluetoothd")) {
    return BluetoothDaemonInterface::GetInstance();
  } else
#endif
  {
    BT_WARNING("Bluetooth backend '%s' is unknown or not available.",
               backend.get());
  }
  return nullptr;

#else
  /* Anything that's not Android 4.2 or later uses BlueZ instead. The
   * code should actually never reach this point.
   */
  BT_WARNING("No Bluetooth backend available for your system.");
  return nullptr;
#endif
}

BluetoothInterface::BluetoothInterface()
{ }

BluetoothInterface::~BluetoothInterface()
{ }

END_BLUETOOTH_NAMESPACE
