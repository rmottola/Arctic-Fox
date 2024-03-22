/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothDaemonHelpers.h"
#include <limits>

#define MAX_UUID_SIZE 16

BEGIN_BLUETOOTH_NAMESPACE

using mozilla::ipc::DaemonSocketPDUHelpers::Convert;
using mozilla::ipc::DaemonSocketPDUHelpers::PackPDU;

//
// Conversion
//

nsresult
Convert(bool aIn, BluetoothScanMode& aOut)
{
  static const BluetoothScanMode sScanMode[] = {
    [false] = SCAN_MODE_CONNECTABLE,
    [true] = SCAN_MODE_CONNECTABLE_DISCOVERABLE
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sScanMode), bool, BluetoothScanMode)) {
    aOut = SCAN_MODE_NONE; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sScanMode[aIn];
  return NS_OK;
}

nsresult
Convert(int32_t aIn, BluetoothTypeOfDevice& aOut)
{
  static const BluetoothTypeOfDevice sTypeOfDevice[] = {
    [0x00] = static_cast<BluetoothTypeOfDevice>(0), // invalid, required by gcc
    [0x01] = TYPE_OF_DEVICE_BREDR,
    [0x02] = TYPE_OF_DEVICE_BLE,
    [0x03] = TYPE_OF_DEVICE_DUAL
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        !aIn, int32_t, BluetoothTypeOfDevice) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        static_cast<size_t>(aIn) >= MOZ_ARRAY_LENGTH(sTypeOfDevice), int32_t,
        BluetoothTypeOfDevice)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sTypeOfDevice[aIn];
  return NS_OK;
}

nsresult
Convert(int32_t aIn, BluetoothScanMode& aOut)
{
  static const BluetoothScanMode sScanMode[] = {
    [0x00] = SCAN_MODE_NONE,
    [0x01] = SCAN_MODE_CONNECTABLE,
    [0x02] = SCAN_MODE_CONNECTABLE_DISCOVERABLE
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn < 0, int32_t, BluetoothScanMode) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        static_cast<size_t>(aIn) >= MOZ_ARRAY_LENGTH(sScanMode), int32_t,
        BluetoothScanMode)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sScanMode[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothA2dpAudioState& aOut)
{
  static const BluetoothA2dpAudioState sAudioState[] = {
    [0x00] = A2DP_AUDIO_STATE_REMOTE_SUSPEND,
    [0x01] = A2DP_AUDIO_STATE_STOPPED,
    [0x02] = A2DP_AUDIO_STATE_STARTED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAudioState), uint8_t,
        BluetoothA2dpAudioState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAudioState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothA2dpConnectionState& aOut)
{
  static const BluetoothA2dpConnectionState sConnectionState[] = {
    [0x00] = A2DP_CONNECTION_STATE_DISCONNECTED,
    [0x01] = A2DP_CONNECTION_STATE_CONNECTING,
    [0x02] = A2DP_CONNECTION_STATE_CONNECTED,
    [0x03] = A2DP_CONNECTION_STATE_DISCONNECTING
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sConnectionState), uint8_t,
        BluetoothA2dpConnectionState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sConnectionState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothAclState& aOut)
{
  static const BluetoothAclState sAclState[] = {
    [0x00] = ACL_STATE_CONNECTED,
    [0x01] = ACL_STATE_DISCONNECTED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAclState), uint8_t, BluetoothAclState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAclState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothAvrcpEvent& aOut)
{
  static const BluetoothAvrcpEvent sAvrcpEvent[] = {
    [0x00] = static_cast<BluetoothAvrcpEvent>(0),
    [0x01] = AVRCP_EVENT_PLAY_STATUS_CHANGED,
    [0x02] = AVRCP_EVENT_TRACK_CHANGE,
    [0x03] = AVRCP_EVENT_TRACK_REACHED_END,
    [0x04] = AVRCP_EVENT_TRACK_REACHED_START,
    [0x05] = AVRCP_EVENT_PLAY_POS_CHANGED,
    [0x06] = static_cast<BluetoothAvrcpEvent>(0),
    [0x07] = static_cast<BluetoothAvrcpEvent>(0),
    [0x08] = AVRCP_EVENT_APP_SETTINGS_CHANGED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        !aIn, uint8_t, BluetoothAvrcpEvent) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn == 0x06, uint8_t, BluetoothAvrcpEvent) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn == 0x07, uint8_t, BluetoothAvrcpEvent) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAvrcpEvent), uint8_t, BluetoothAvrcpEvent)) {
    aOut = static_cast<BluetoothAvrcpEvent>(0); // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAvrcpEvent[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothAvrcpMediaAttribute& aOut)
{
  static const BluetoothAvrcpMediaAttribute sAvrcpMediaAttribute[] = {
    [0x00] = static_cast<BluetoothAvrcpMediaAttribute>(0),
    [0x01] = AVRCP_MEDIA_ATTRIBUTE_TITLE,
    [0x02] = AVRCP_MEDIA_ATTRIBUTE_ARTIST,
    [0x03] = AVRCP_MEDIA_ATTRIBUTE_ALBUM,
    [0x04] = AVRCP_MEDIA_ATTRIBUTE_TRACK_NUM,
    [0x05] = AVRCP_MEDIA_ATTRIBUTE_NUM_TRACKS,
    [0x06] = AVRCP_MEDIA_ATTRIBUTE_GENRE,
    [0x07] = AVRCP_MEDIA_ATTRIBUTE_PLAYING_TIME
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        !aIn, uint8_t, BluetoothAvrcpMediaAttrbiute) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAvrcpMediaAttribute), uint8_t,
        BluetoothAvrcpMediaAttribute)) {
    // silences compiler warning
    aOut = static_cast<BluetoothAvrcpMediaAttribute>(0);
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAvrcpMediaAttribute[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothAvrcpPlayerAttribute& aOut)
{
  static const BluetoothAvrcpPlayerAttribute sAvrcpPlayerAttribute[] = {
    [0x00] = static_cast<BluetoothAvrcpPlayerAttribute>(0),
    [0x01] = AVRCP_PLAYER_ATTRIBUTE_EQUALIZER,
    [0x02] = AVRCP_PLAYER_ATTRIBUTE_REPEAT,
    [0x03] = AVRCP_PLAYER_ATTRIBUTE_SHUFFLE,
    [0x04] = AVRCP_PLAYER_ATTRIBUTE_SCAN
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        !aIn, uint8_t, BluetoothAvrcpPlayerAttrbiute) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAvrcpPlayerAttribute), uint8_t,
        BluetoothAvrcpPlayerAttribute)) {
    // silences compiler warning
    aOut = static_cast<BluetoothAvrcpPlayerAttribute>(0);
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAvrcpPlayerAttribute[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothAvrcpRemoteFeatureBits& aOut)
{
  static const uint8_t sAvrcpRemoteFeatureBits[] = {
    [0] = AVRCP_REMOTE_FEATURE_METADATA,
    [1] = AVRCP_REMOTE_FEATURE_ABSOLUTE_VOLUME,
    [2] = AVRCP_REMOTE_FEATURE_BROWSE
  };
  uint8_t bits = 0;
  size_t i;
  for (i = 0; i < MOZ_ARRAY_LENGTH(sAvrcpRemoteFeatureBits); ++i, aIn >>= 1) {
    if (aIn & 0x01) {
      bits |= sAvrcpRemoteFeatureBits[i];
    }
  }
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn << i, 'uint8_t', BluetoothAvrcpRemoteFeatureBits)) {
    aOut = AVRCP_REMOTE_FEATURE_NONE; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = static_cast<BluetoothAvrcpRemoteFeatureBits>(bits);
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothBondState& aOut)
{
  static const BluetoothBondState sBondState[] = {
    [0x00] = BOND_STATE_NONE,
    [0x01] = BOND_STATE_BONDING,
    [0x02] = BOND_STATE_BONDED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sBondState), uint8_t, BluetoothBondState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sBondState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeAudioState& aOut)
{
  static const BluetoothHandsfreeAudioState sAudioState[] = {
    [0x00] = HFP_AUDIO_STATE_DISCONNECTED,
    [0x01] = HFP_AUDIO_STATE_CONNECTING,
    [0x02] = HFP_AUDIO_STATE_CONNECTED,
    [0x03] = HFP_AUDIO_STATE_DISCONNECTING
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAudioState), uint8_t,
        BluetoothHandsfreeAudioState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAudioState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeCallHoldType& aOut)
{
  static const BluetoothHandsfreeCallHoldType sCallHoldType[] = {
    [0x00] = HFP_CALL_HOLD_RELEASEHELD,
    [0x01] = HFP_CALL_HOLD_RELEASEACTIVE_ACCEPTHELD,
    [0x02] = HFP_CALL_HOLD_HOLDACTIVE_ACCEPTHELD,
    [0x03] = HFP_CALL_HOLD_ADDHELDTOCONF
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sCallHoldType), uint8_t,
        BluetoothHandsfreeCallHoldType)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sCallHoldType[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeConnectionState& aOut)
{
  static const BluetoothHandsfreeConnectionState sConnectionState[] = {
    [0x00] = HFP_CONNECTION_STATE_DISCONNECTED,
    [0x01] = HFP_CONNECTION_STATE_CONNECTING,
    [0x02] = HFP_CONNECTION_STATE_CONNECTED,
    [0x03] = HFP_CONNECTION_STATE_SLC_CONNECTED,
    [0x04] = HFP_CONNECTION_STATE_DISCONNECTING
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sConnectionState), uint8_t,
        BluetoothHandsfreeConnectionState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sConnectionState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeNRECState& aOut)
{
  static const BluetoothHandsfreeNRECState sNRECState[] = {
    [0x00] = HFP_NREC_STOPPED,
    [0x01] = HFP_NREC_STARTED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sNRECState), uint8_t,
        BluetoothHandsfreeNRECState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sNRECState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeVoiceRecognitionState& aOut)
{
  static const BluetoothHandsfreeVoiceRecognitionState sState[] = {
    [0x00] = HFP_VOICE_RECOGNITION_STOPPED,
    [0x01] = HFP_VOICE_RECOGNITION_STARTED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sState), uint8_t,
        BluetoothHandsfreeVoiceRecognitionState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeVolumeType& aOut)
{
  static const BluetoothHandsfreeVolumeType sVolumeType[] = {
    [0x00] = HFP_VOLUME_TYPE_SPEAKER,
    [0x01] = HFP_VOLUME_TYPE_MICROPHONE
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sVolumeType), uint8_t,
        BluetoothHandsfreeVolumeType)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sVolumeType[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHandsfreeWbsConfig& aOut)
{
  static const BluetoothHandsfreeWbsConfig sWbsConfig[] = {
    [0x00] = HFP_WBS_NONE,
    [0x01] = HFP_WBS_NO,
    [0x02] = HFP_WBS_YES
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sWbsConfig), uint8_t,
        BluetoothHandsfreeWbsConfig)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sWbsConfig[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothTypeOfDevice& aOut)
{
  return Convert((int32_t)aIn, aOut);
}

nsresult
Convert(uint8_t aIn, BluetoothPropertyType& aOut)
{
  static const BluetoothPropertyType sPropertyType[] = {
    [0x00] = static_cast<BluetoothPropertyType>(0), // invalid, required by gcc
    [0x01] = PROPERTY_BDNAME,
    [0x02] = PROPERTY_BDADDR,
    [0x03] = PROPERTY_UUIDS,
    [0x04] = PROPERTY_CLASS_OF_DEVICE,
    [0x05] = PROPERTY_TYPE_OF_DEVICE,
    [0x06] = PROPERTY_SERVICE_RECORD,
    [0x07] = PROPERTY_ADAPTER_SCAN_MODE,
    [0x08] = PROPERTY_ADAPTER_BONDED_DEVICES,
    [0x09] = PROPERTY_ADAPTER_DISCOVERY_TIMEOUT,
    [0x0a] = PROPERTY_REMOTE_FRIENDLY_NAME,
    [0x0b] = PROPERTY_REMOTE_RSSI,
    [0x0c] = PROPERTY_REMOTE_VERSION_INFO
  };
  if (aIn == 0xff) {
    /* This case is handled separately to not populate
     * |sPropertyType| with empty entries. */
    aOut = PROPERTY_REMOTE_DEVICE_TIMESTAMP;
    return NS_OK;
  }
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        !aIn, uint8_t, BluetoothPropertyType) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sPropertyType), uint8_t,
        BluetoothPropertyType)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sPropertyType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothSocketType aIn, uint8_t& aOut)
{
  static const uint8_t sSocketType[] = {
    [0] = 0, // silences compiler warning
    [BluetoothSocketType::RFCOMM] = 0x01,
    [BluetoothSocketType::SCO] = 0x02,
    [BluetoothSocketType::L2CAP] = 0x03
    // EL2CAP not supported
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn == BluetoothSocketType::EL2CAP, BluetoothSocketType, uint8_t) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sSocketType), BluetoothSocketType, uint8_t) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        !sSocketType[aIn], BluetoothSocketType, uint8_t)) {
    aOut = 0; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sSocketType[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothSspVariant& aOut)
{
  static const BluetoothSspVariant sSspVariant[] = {
    [0x00] = SSP_VARIANT_PASSKEY_CONFIRMATION,
    [0x01] = SSP_VARIANT_PASSKEY_ENTRY,
    [0x02] = SSP_VARIANT_CONSENT,
    [0x03] = SSP_VARIANT_PASSKEY_NOTIFICATION
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sSspVariant), uint8_t, BluetoothSspVariant)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sSspVariant[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothStatus& aOut)
{
  static const BluetoothStatus sStatus[] = {
    [0x00] = STATUS_SUCCESS,
    [0x01] = STATUS_FAIL,
    [0x02] = STATUS_NOT_READY,
    [0x03] = STATUS_NOMEM,
    [0x04] = STATUS_BUSY,
    [0x05] = STATUS_DONE,
    [0x06] = STATUS_UNSUPPORTED,
    [0x07] = STATUS_PARM_INVALID,
    [0x08] = STATUS_UNHANDLED,
    [0x09] = STATUS_AUTH_FAILURE,
    [0x0a] = STATUS_RMT_DEV_DOWN,
    [0x0b] = STATUS_AUTH_REJECTED
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sStatus), uint8_t, BluetoothStatus)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sStatus[aIn];
  return NS_OK;
}

nsresult
Convert(int32_t aIn, BluetoothAttributeHandle& aOut)
{
  if (NS_WARN_IF(aIn < 0x0000) || NS_WARN_IF(aIn > 0xFFFF)) {
    aOut.mHandle = 0x0000; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut.mHandle = static_cast<uint16_t>(aIn);
  return NS_OK;
}

nsresult
Convert(int32_t aIn, BluetoothGattStatus& aOut)
{
  /* Reference: $B2G/external/bluetooth/bluedroid/stack/include/gatt_api.h */
  static const BluetoothGattStatus sGattStatus[] = {
    [0x0000] = GATT_STATUS_SUCCESS,
    [0x0001] = GATT_STATUS_INVALID_HANDLE,
    [0x0002] = GATT_STATUS_READ_NOT_PERMITTED,
    [0x0003] = GATT_STATUS_WRITE_NOT_PERMITTED,
    [0x0004] = GATT_STATUS_INVALID_PDU,
    [0x0005] = GATT_STATUS_INSUFFICIENT_AUTHENTICATION,
    [0x0006] = GATT_STATUS_REQUEST_NOT_SUPPORTED,
    [0x0007] = GATT_STATUS_INVALID_OFFSET,
    [0x0008] = GATT_STATUS_INSUFFICIENT_AUTHORIZATION,
    [0x0009] = GATT_STATUS_PREPARE_QUEUE_FULL,
    [0x000a] = GATT_STATUS_ATTRIBUTE_NOT_FOUND,
    [0x000b] = GATT_STATUS_ATTRIBUTE_NOT_LONG,
    [0x000c] = GATT_STATUS_INSUFFICIENT_ENCRYPTION_KEY_SIZE,
    [0x000d] = GATT_STATUS_INVALID_ATTRIBUTE_LENGTH,
    [0x000e] = GATT_STATUS_UNLIKELY_ERROR,
    [0x000f] = GATT_STATUS_INSUFFICIENT_ENCRYPTION,
    [0x0010] = GATT_STATUS_UNSUPPORTED_GROUP_TYPE,
    [0x0011] = GATT_STATUS_INSUFFICIENT_RESOURCES
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn < 0, int32_t, BluetoothGattStatus) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= static_cast<ssize_t>(MOZ_ARRAY_LENGTH(sGattStatus)), int32_t,
        BluetoothGattStatus)) {
    aOut = GATT_STATUS_UNKNOWN_ERROR;
  } else {
    aOut = sGattStatus[aIn];
  }
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHidProtocolMode& aOut)
{
  static const BluetoothHidProtocolMode sMode[] = {
    [0x00] = HID_PROTOCOL_MODE_REPORT,
    [0x01] = HID_PROTOCOL_MODE_BOOT
  };
  if (aIn == 0xff) {
    /* This case is handled separately to not populate
     * |sMode| with empty entries. */
    aOut = HID_PROTOCOL_MODE_UNSUPPORTED;
    return NS_OK;
  }
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sMode), uint8_t, BluetoothHidProtocolMode)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sMode[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHidConnectionState& aOut)
{
  static const BluetoothHidConnectionState sConnectionState[] = {
    [0x00] = HID_CONNECTION_STATE_CONNECTED,
    [0x01] = HID_CONNECTION_STATE_CONNECTING,
    [0x02] = HID_CONNECTION_STATE_DISCONNECTED,
    [0x03] = HID_CONNECTION_STATE_DISCONNECTING,
    [0x04] = HID_CONNECTION_STATE_FAILED_MOUSE_FROM_HOST,
    [0x05] = HID_CONNECTION_STATE_FAILED_KEYBOARD_FROM_HOST,
    [0x06] = HID_CONNECTION_STATE_FAILED_TOO_MANY_DEVICES,
    [0x07] = HID_CONNECTION_STATE_FAILED_NO_HID_DRIVER,
    [0x08] = HID_CONNECTION_STATE_FAILED_GENERIC,
    [0x09] = HID_CONNECTION_STATE_UNKNOWN
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sConnectionState),
        uint8_t, BluetoothHidConnectionState)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sConnectionState[aIn];
  return NS_OK;
}

nsresult
Convert(uint8_t aIn, BluetoothHidStatus& aOut)
{
  static const BluetoothHidStatus sStatus[] = {
    [0x00] = HID_STATUS_OK,
    [0x01] = HID_STATUS_HANDSHAKE_DEVICE_NOT_READY,
    [0x02] = HID_STATUS_HANDSHAKE_INVALID_REPORT_ID,
    [0x03] = HID_STATUS_HANDSHAKE_TRANSACTION_NOT_SPT,
    [0x04] = HID_STATUS_HANDSHAKE_INVALID_PARAMETER,
    [0x05] = HID_STATUS_HANDSHAKE_GENERIC_ERROR,
    [0x06] = HID_STATUS_GENERAL_ERROR,
    [0x07] = HID_STATUS_SDP_ERROR,
    [0x08] = HID_STATUS_SET_PROTOCOL_ERROR,
    [0x09] = HID_STATUS_DEVICE_DATABASE_FULL,
    [0x0a] = HID_STATUS_DEVICE_TYPE_NOT_SUPPORTED,
    [0x0b] = HID_STATUS_NO_RESOURCES,
    [0x0c] = HID_STATUS_AUTHENTICATION_FAILED,
    [0x0d] = HID_STATUS_HDL
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sStatus), uint8_t, BluetoothHidStatus)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sStatus[aIn];
  return NS_OK;
}

nsresult
Convert(nsresult aIn, BluetoothStatus& aOut)
{
  if (NS_SUCCEEDED(aIn)) {
    aOut = STATUS_SUCCESS;
  } else if (aIn == NS_ERROR_OUT_OF_MEMORY) {
    aOut = STATUS_NOMEM;
  } else {
    aOut = STATUS_FAIL;
  }
  return NS_OK;
}

nsresult
Convert(const BluetoothAttributeHandle& aIn, int32_t& aOut)
{
  aOut = static_cast<int32_t>(aIn.mHandle);
  return NS_OK;
}

nsresult
Convert(const BluetoothAttributeHandle& aIn, uint16_t& aOut)
{
  aOut = aIn.mHandle;
  return NS_OK;
}


nsresult
Convert(BluetoothAvrcpEvent aIn, uint8_t& aOut)
{
  static const uint8_t sValue[] = {
    [AVRCP_EVENT_PLAY_STATUS_CHANGED] = 0x01,
    [AVRCP_EVENT_TRACK_CHANGE] = 0x02,
    [AVRCP_EVENT_TRACK_REACHED_END] = 0x03,
    [AVRCP_EVENT_TRACK_REACHED_START] = 0x04,
    [AVRCP_EVENT_PLAY_POS_CHANGED] = 0x05,
    [AVRCP_EVENT_APP_SETTINGS_CHANGED] = 0x08
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sValue), BluetoothAvrcpEvent, uint8_t)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sValue[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothAvrcpNotification aIn, uint8_t& aOut)
{
  static const bool sValue[] = {
    [AVRCP_NTF_INTERIM] = 0x00,
    [AVRCP_NTF_CHANGED] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sValue), BluetoothAvrcpNotification,
        uint8_t)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sValue[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothAvrcpPlayerAttribute aIn, uint8_t& aOut)
{
  static const uint8_t sValue[] = {
    [AVRCP_PLAYER_ATTRIBUTE_EQUALIZER] = 0x01,
    [AVRCP_PLAYER_ATTRIBUTE_REPEAT] = 0x02,
    [AVRCP_PLAYER_ATTRIBUTE_SHUFFLE] = 0x03,
    [AVRCP_PLAYER_ATTRIBUTE_SCAN] = 0x04
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sValue), BluetoothAvrcpPlayerAttribute, uint8_t)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sValue[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothAvrcpRemoteFeatureBits aIn, unsigned long& aOut)
{
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn < std::numeric_limits<unsigned long>::min(),
        BluetoothAvrcpRemoteFeature, unsigned long) ||
      MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn > std::numeric_limits<unsigned long>::max(),
        BluetoothAvrcpRemoteFeature, unsigned long)) {
    aOut = 0; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = static_cast<unsigned long>(aIn);
  return NS_OK;
}

nsresult
Convert(BluetoothAvrcpStatus aIn, uint8_t& aOut)
{
  static const uint8_t sValue[] = {
    [AVRCP_STATUS_BAD_COMMAND] = 0x00,
    [AVRCP_STATUS_BAD_PARAMETER] = 0x01,
    [AVRCP_STATUS_NOT_FOUND] = 0x02,
    [AVRCP_STATUS_INTERNAL_ERROR] = 0x03,
    [AVRCP_STATUS_SUCCESS] = 0x04
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sValue), BluetoothAvrcpStatus, uint8_t)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sValue[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeAtResponse aIn, uint8_t& aOut)
{
  static const uint8_t sAtResponse[] = {
    [HFP_AT_RESPONSE_ERROR] = 0x00,
    [HFP_AT_RESPONSE_OK] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sAtResponse), BluetoothHandsfreeAtResponse,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sAtResponse[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeCallAddressType aIn, uint8_t& aOut)
{
  static const uint8_t sCallAddressType[] = {
    [HFP_CALL_ADDRESS_TYPE_UNKNOWN] = 0x81,
    [HFP_CALL_ADDRESS_TYPE_INTERNATIONAL] = 0x91
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sCallAddressType),
        BluetoothHandsfreeCallAddressType, uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sCallAddressType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeCallDirection aIn, uint8_t& aOut)
{
  static const uint8_t sCallDirection[] = {
    [HFP_CALL_DIRECTION_OUTGOING] = 0x00,
    [HFP_CALL_DIRECTION_INCOMING] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sCallDirection),
        BluetoothHandsfreeCallDirection, uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sCallDirection[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeCallState aIn, uint8_t& aOut)
{
  static const uint8_t sCallState[] = {
    [HFP_CALL_STATE_ACTIVE] = 0x00,
    [HFP_CALL_STATE_HELD] = 0x01,
    [HFP_CALL_STATE_DIALING] = 0x02,
    [HFP_CALL_STATE_ALERTING] = 0x03,
    [HFP_CALL_STATE_INCOMING] = 0x04,
    [HFP_CALL_STATE_WAITING] = 0x05,
    [HFP_CALL_STATE_IDLE] = 0x06
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sCallState), BluetoothHandsfreeCallState,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sCallState[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeCallMode aIn, uint8_t& aOut)
{
  static const uint8_t sCallMode[] = {
    [HFP_CALL_MODE_VOICE] = 0x00,
    [HFP_CALL_MODE_DATA] = 0x01,
    [HFP_CALL_MODE_FAX] = 0x02
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sCallMode), BluetoothHandsfreeCallMode, uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sCallMode[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeCallMptyType aIn, uint8_t& aOut)
{
  static const uint8_t sCallMptyType[] = {
    [HFP_CALL_MPTY_TYPE_SINGLE] = 0x00,
    [HFP_CALL_MPTY_TYPE_MULTI] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sCallMptyType),
        BluetoothHandsfreeCallMptyType, uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sCallMptyType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeNetworkState aIn, uint8_t& aOut)
{
  static const uint8_t sNetworkState[] = {
    [HFP_NETWORK_STATE_NOT_AVAILABLE] = 0x00,
    [HFP_NETWORK_STATE_AVAILABLE] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sNetworkState), BluetoothHandsfreeNetworkState,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sNetworkState[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeServiceType aIn, uint8_t& aOut)
{
  static const uint8_t sServiceType[] = {
    [HFP_SERVICE_TYPE_HOME] = 0x00,
    [HFP_SERVICE_TYPE_ROAMING] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sServiceType), BluetoothHandsfreeServiceType,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sServiceType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeVolumeType aIn, uint8_t& aOut)
{
  static const uint8_t sVolumeType[] = {
    [HFP_VOLUME_TYPE_SPEAKER] = 0x00,
    [HFP_VOLUME_TYPE_MICROPHONE] = 0x01
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sVolumeType), BluetoothHandsfreeVolumeType,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sVolumeType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHandsfreeWbsConfig aIn, uint8_t& aOut)
{
  static const uint8_t sWbsConfig[] = {
    [HFP_WBS_NONE] = 0x00,
    [HFP_WBS_NO] = 0x01,
    [HFP_WBS_YES] = 0x02
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sWbsConfig), BluetoothHandsfreeWbsConfig,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sWbsConfig[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothPropertyType aIn, uint8_t& aOut)
{
  static const uint8_t sPropertyType[] = {
    [PROPERTY_UNKNOWN] = 0x00,
    [PROPERTY_BDNAME] = 0x01,
    [PROPERTY_BDADDR] = 0x02,
    [PROPERTY_UUIDS] = 0x03,
    [PROPERTY_CLASS_OF_DEVICE] = 0x04,
    [PROPERTY_TYPE_OF_DEVICE] = 0x05,
    [PROPERTY_SERVICE_RECORD] = 0x06,
    [PROPERTY_ADAPTER_SCAN_MODE] = 0x07,
    [PROPERTY_ADAPTER_BONDED_DEVICES] = 0x08,
    [PROPERTY_ADAPTER_DISCOVERY_TIMEOUT] = 0x09,
    [PROPERTY_REMOTE_FRIENDLY_NAME] = 0x0a,
    [PROPERTY_REMOTE_RSSI] = 0x0b,
    [PROPERTY_REMOTE_VERSION_INFO] = 0x0c,
    [PROPERTY_REMOTE_DEVICE_TIMESTAMP] = 0xff
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sPropertyType), BluetoothPropertyType,
        uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sPropertyType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothScanMode aIn, int32_t& aOut)
{
  static const int32_t sScanMode[] = {
    [SCAN_MODE_NONE] = 0x00,
    [SCAN_MODE_CONNECTABLE] = 0x01,
    [SCAN_MODE_CONNECTABLE_DISCOVERABLE] = 0x02
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sScanMode), BluetoothScanMode, int32_t)) {
    aOut = 0; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sScanMode[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothSetupServiceId aIn, uint8_t& aOut)
{
  static const uint8_t sServiceId[] = {
    [SETUP_SERVICE_ID_SETUP] = 0x00,
    [SETUP_SERVICE_ID_CORE] = 0x01,
    [SETUP_SERVICE_ID_SOCKET] = 0x02,
    [SETUP_SERVICE_ID_HID] = 0x03,
    [SETUP_SERVICE_ID_PAN] = 0x04,
    [SETUP_SERVICE_ID_HANDSFREE] = 0x05,
    [SETUP_SERVICE_ID_A2DP] = 0x06,
    [SETUP_SERVICE_ID_HEALTH] = 0x07,
    [SETUP_SERVICE_ID_AVRCP] = 0x08,
    [SETUP_SERVICE_ID_GATT] = 0x09,
    [SETUP_SERVICE_ID_HANDSFREE_CLIENT] = 0x0a,
    [SETUP_SERVICE_ID_MAP_CLIENT] = 0x0b,
    [SETUP_SERVICE_ID_AVRCP_CONTROLLER] = 0x0c,
    [SETUP_SERVICE_ID_A2DP_SINK] = 0x0d
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sServiceId),
        BluetoothServiceSetupId, uint8_t)) {
    aOut = 0; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sServiceId[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothSspVariant aIn, uint8_t& aOut)
{
  static const uint8_t sValue[] = {
    [SSP_VARIANT_PASSKEY_CONFIRMATION] = 0x00,
    [SSP_VARIANT_PASSKEY_ENTRY] = 0x01,
    [SSP_VARIANT_CONSENT] = 0x02,
    [SSP_VARIANT_PASSKEY_NOTIFICATION] = 0x03
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sValue), BluetoothSspVariant, uint8_t)) {
    aOut = 0; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sValue[aIn];
  return NS_OK;
}

nsresult
Convert(ControlPlayStatus aIn, uint8_t& aOut)
{
  static const uint8_t sValue[] = {
    [PLAYSTATUS_STOPPED] = 0x00,
    [PLAYSTATUS_PLAYING] = 0x01,
    [PLAYSTATUS_PAUSED] = 0x02,
    [PLAYSTATUS_FWD_SEEK] = 0x03,
    [PLAYSTATUS_REV_SEEK] = 0x04
  };
  if (aIn == PLAYSTATUS_ERROR) {
    /* This case is handled separately to not populate
     * |sValue| with empty entries. */
    aOut = 0xff;
    return NS_OK;
  }
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sValue), ControlPlayStatus, uint8_t)) {
    aOut = 0; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sValue[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothGattAuthReq aIn, int32_t& aOut)
{
  static const int32_t sGattAuthReq[] = {
    [GATT_AUTH_REQ_NONE] = 0x00,
    [GATT_AUTH_REQ_NO_MITM] = 0x01,
    [GATT_AUTH_REQ_MITM] = 0x02,
    [GATT_AUTH_REQ_SIGNED_NO_MITM] = 0x03,
    [GATT_AUTH_REQ_SIGNED_MITM] = 0x04
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sGattAuthReq), BluetoothGattAuthReq,
        int32_t)) {
    aOut = GATT_AUTH_REQ_NONE; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sGattAuthReq[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothGattAuthReq aIn, uint8_t& aOut)
{
  static const uint8_t sGattAuthReq[] = {
    [GATT_AUTH_REQ_NONE] = 0x00,
    [GATT_AUTH_REQ_NO_MITM] = 0x01,
    [GATT_AUTH_REQ_MITM] = 0x02,
    [GATT_AUTH_REQ_SIGNED_NO_MITM] = 0x03,
    [GATT_AUTH_REQ_SIGNED_MITM] = 0x04
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sGattAuthReq), BluetoothGattAuthReq,
        uint8_t)) {
    aOut = GATT_AUTH_REQ_NONE; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sGattAuthReq[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothGattWriteType aIn, int32_t& aOut)
{
  static const int32_t sGattWriteType[] = {
    [GATT_WRITE_TYPE_NO_RESPONSE] = 0x01,
    [GATT_WRITE_TYPE_NORMAL] = 0x02,
    [GATT_WRITE_TYPE_PREPARE] = 0x03,
    [GATT_WRITE_TYPE_SIGNED] = 0x04
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sGattWriteType), BluetoothGattWriteType,
        int32_t)) {
    aOut = GATT_WRITE_TYPE_NORMAL; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sGattWriteType[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHidProtocolMode aIn, uint8_t& aOut)
{
  static const uint8_t sMode[] = {
    [HID_PROTOCOL_MODE_REPORT] = 0x00,
    [HID_PROTOCOL_MODE_BOOT] = 0x01
  };
  if (aIn == HID_PROTOCOL_MODE_UNSUPPORTED) {
    /* This case is handled separately to not populate
     * |sValue| with empty entries. */
    aOut = 0xff;
    return NS_OK;
  }
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sMode), BluetoothHidProtocolMode, uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sMode[aIn];
  return NS_OK;
}

nsresult
Convert(BluetoothHidReportType aIn, uint8_t& aOut)
{
  static const uint8_t sType[] = {
    [0x00] = static_cast<BluetoothHidReportType>(0),
    [HID_REPORT_TYPE_INPUT] = 0x01,
    [HID_REPORT_TYPE_OUTPUT] = 0x02,
    [HID_REPORT_TYPE_FEATURE] = 0x03
  };
  if (MOZ_HAL_IPC_CONVERT_WARN_IF(
        aIn >= MOZ_ARRAY_LENGTH(sType),
        BluetoothHidReportType, uint8_t)) {
    aOut = 0x00; // silences compiler warning
    return NS_ERROR_ILLEGAL_VALUE;
  }
  aOut = sType[aIn];
  return NS_OK;
}

/* |ConvertArray| is a helper for converting arrays. Pass an
 * instance of this structure as the first argument to |Convert|
 * to convert an array. The output type has to support the array
 * subscript operator.
 */
template <typename T>
struct ConvertArray
{
  ConvertArray(const T* aData, unsigned long aLength)
  : mData(aData)
  , mLength(aLength)
  { }

  const T* mData;
  unsigned long mLength;
};

/* This implementation of |Convert| converts the elements of an
 * array one-by-one. The result data structures must have enough
 * memory allocated.
 */
template<typename Tin, typename Tout>
inline nsresult
Convert(const ConvertArray<Tin>& aIn, Tout& aOut)
{
  for (unsigned long i = 0; i < aIn.mLength; ++i) {
    nsresult rv = Convert(aIn.mData[i], aOut[i]);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  return NS_OK;
}

//
// Packing
//

nsresult
PackPDU(const BluetoothAddress& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackArray<uint8_t>(aIn.mAddr, sizeof(aIn.mAddr)), aPDU);
}

nsresult
PackPDU(const BluetoothAttributeHandle& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothAttributeHandle, int32_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothAvrcpAttributeTextPairs& aIn,
        DaemonSocketPDU& aPDU)
{
  size_t i;

  for (i = 0; i < aIn.mLength; ++i) {
    nsresult rv = PackPDU(aIn.mAttr[i], aPDU);
    if (NS_FAILED(rv)) {
      return rv;
    }

    uint8_t len;
    const uint8_t* str;

    if (aIn.mText[i]) {
      str = reinterpret_cast<const uint8_t*>(aIn.mText[i]);
      len = strlen(aIn.mText[i]) + 1;
    } else {
      /* write \0 character for NULL strings */
      str = reinterpret_cast<const uint8_t*>("\0");
      len = 1;
    }

    rv = PackPDU(len, aPDU);
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = PackPDU(PackArray<uint8_t>(str, len), aPDU);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  return NS_OK;
}

nsresult
PackPDU(const BluetoothAvrcpAttributeValuePairs& aIn,
        DaemonSocketPDU& aPDU)
{
  size_t i;

  for (i = 0; i < aIn.mLength; ++i) {
    nsresult rv = PackPDU(aIn.mAttr[i], aPDU);
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = PackPDU(aIn.mValue[i], aPDU);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  return NS_OK;
}

nsresult
PackPDU(const BluetoothAvrcpElementAttribute& aIn, DaemonSocketPDU& aPDU)
{
  nsresult rv = PackPDU(PackConversion<uint32_t, uint8_t>(aIn.mId), aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }

  const NS_ConvertUTF16toUTF8 cstr(aIn.mValue);

  if (MOZ_HAL_IPC_PACK_WARN_IF(
        cstr.Length() == PR_UINT32_MAX, BluetoothAvrcpElementAttribute)) {
    return NS_ERROR_ILLEGAL_VALUE; /* integer overflow detected */
  }

  uint32_t clen = cstr.Length() + 1; /* include \0 character */

  rv = PackPDU(PackConversion<uint32_t, uint8_t>(clen), aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return PackPDU(
    PackArray<uint8_t>(reinterpret_cast<const uint8_t*>(cstr.get()), clen),
    aPDU);
}

nsresult
PackPDU(BluetoothAvrcpEvent aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothAvrcpEvent, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothAvrcpEventParamPair& aIn, DaemonSocketPDU& aPDU)
{
  nsresult rv;

  switch (aIn.mEvent) {
    case AVRCP_EVENT_PLAY_STATUS_CHANGED:
      rv = PackPDU(aIn.mParam.mPlayStatus, aPDU);
      break;
    case AVRCP_EVENT_TRACK_CHANGE:
      rv = PackPDU(PackArray<uint8_t>(aIn.mParam.mTrack,
                                      MOZ_ARRAY_LENGTH(aIn.mParam.mTrack)),
                   aPDU);
      break;
    case AVRCP_EVENT_TRACK_REACHED_END:
      /* fall through */
    case AVRCP_EVENT_TRACK_REACHED_START:
      /* no data to pack */
      rv = NS_OK;
      break;
    case AVRCP_EVENT_PLAY_POS_CHANGED:
      rv = PackPDU(aIn.mParam.mSongPos, aPDU);
      break;
    case AVRCP_EVENT_APP_SETTINGS_CHANGED:
      /* pack number of attribute-value pairs */
      rv = PackPDU(aIn.mParam.mNumAttr, aPDU);
      if (NS_FAILED(rv)) {
        return rv;
      }
      /* pack attribute-value pairs */
      rv = PackPDU(BluetoothAvrcpAttributeValuePairs(aIn.mParam.mIds,
                                                     aIn.mParam.mValues,
                                                     aIn.mParam.mNumAttr),
                   aPDU);
      break;
    default:
      rv = NS_ERROR_ILLEGAL_VALUE;
      break;
  }
  return rv;
}

nsresult
PackPDU(BluetoothAvrcpNotification aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothAvrcpNotification, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothAvrcpPlayerAttribute aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothAvrcpPlayerAttribute, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothAvrcpStatus aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothAvrcpStatus, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothConfigurationParameter& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(aIn.mType, aIn.mLength,
                 PackArray<uint8_t>(aIn.mValue.get(), aIn.mLength), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeAtResponse& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeAtResponse, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeCallAddressType& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeCallAddressType, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeCallDirection& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeCallDirection, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeCallMode& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeCallMode, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeCallMptyType& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeCallMptyType, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeCallState& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeCallState, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeNetworkState& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeNetworkState, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeServiceType& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeServiceType, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeVolumeType& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeVolumeType, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHandsfreeWbsConfig& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHandsfreeWbsConfig, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothProperty& aIn, DaemonSocketPDU& aPDU)
{
  nsresult rv = PackPDU(aIn.mType, aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }

  switch (aIn.mType) {
    case PROPERTY_BDNAME:
      rv = PackPDU(PackConversion<uint8_t, uint16_t>(aIn.mRemoteName.mLength),
                   PackArray<uint8_t>(aIn.mRemoteName.mName,
                                      aIn.mRemoteName.mLength),
                   aPDU);
      break;
    case PROPERTY_REMOTE_FRIENDLY_NAME: {
        NS_ConvertUTF16toUTF8 stringUTF8(aIn.mString);

        rv = PackPDU(PackConversion<size_t, uint16_t>(stringUTF8.Length()),
                     PackArray<uint8_t>(
                       reinterpret_cast<const uint8_t*>(stringUTF8.get()),
                       stringUTF8.Length()),
                     aPDU);
      }
      break;
    case PROPERTY_CLASS_OF_DEVICE:
      /* fall through */
    case PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
      rv = PackPDU(PackConversion<size_t, uint16_t>(sizeof(aIn.mUint32)),
                   aIn.mUint32,
                   aPDU);
      break;
    case PROPERTY_ADAPTER_SCAN_MODE:
      /* |mScanMode| is sent as signed int of 4 bytes */
      rv = PackPDU(PackConversion<size_t, uint16_t>(sizeof(int32_t)),
                   aIn.mScanMode,
                   aPDU);
      break;
    default:
      NS_NOTREACHED("Invalid property for packing");
      return NS_ERROR_ILLEGAL_VALUE;
  }
  return rv;
}

nsresult
PackPDU(const BluetoothPinCode& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(aIn.mLength,
                 PackArray<uint8_t>(aIn.mPinCode, sizeof(aIn.mPinCode)),
                 aPDU);
}

nsresult
PackPDU(BluetoothPropertyType aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothPropertyType, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothSspVariant aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothSspVariant, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothScanMode aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothScanMode, int32_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothSetupServiceId aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothSetupServiceId, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothServiceName& aIn, DaemonSocketPDU& aPDU)
{
  static const uint8_t sTerminator = '\0';

  nsresult rv =
    PackPDU(PackArray<uint8_t>(aIn.mName, sizeof(aIn.mName)), aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* The PDU requries one additional byte for \0 termination */
  return aPDU.Write(sTerminator);
}

nsresult
PackPDU(BluetoothSocketType aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothSocketType, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(ControlPlayStatus aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<ControlPlayStatus, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothTransport aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothTransport, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothUuid& aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackArray<uint8_t>(aIn.mUuid, sizeof(aIn.mUuid)), aPDU);
}

nsresult
PackPDU(const BluetoothGattId& aIn, DaemonSocketPDU& aPDU)
{
  nsresult rv = PackPDU(PackReversed<BluetoothUuid>(aIn.mUuid), aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return PackPDU(aIn.mInstanceId, aPDU);
}

nsresult
PackPDU(const BluetoothGattServiceId& aIn, DaemonSocketPDU& aPDU)
{
  nsresult rv = PackPDU(aIn.mId, aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return PackPDU(aIn.mIsPrimary, aPDU);
}

nsresult
PackPDU(BluetoothGattAuthReq aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothGattAuthReq, int32_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothGattWriteType aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(PackConversion<BluetoothGattWriteType, int32_t>(aIn), aPDU);
}

nsresult
PackPDU(const BluetoothHidInfoParam& aIn, DaemonSocketPDU& aPDU)
{
  if (MOZ_HAL_IPC_PACK_WARN_IF(
        aIn.mDescriptorLength > BLUETOOTH_HID_MAX_DESC_LEN,
        BluetoothHidInfoParam)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  nsresult rv =  PackPDU(aIn.mAttributeMask,
                         aIn.mSubclass,
                         aIn.mApplicationId,
                         aIn.mVendorId,
                         aIn.mProductId,
                         aIn.mVersion,
                         aIn.mCountryCode,
                         aIn.mDescriptorLength,
                         aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return PackPDU(
    PackArray<uint8_t>(aIn.mDescriptorValue, aIn.mDescriptorLength), aPDU);
}

nsresult
PackPDU(const BluetoothHidReport& aIn, DaemonSocketPDU& aPDU)
{
  uint8_t* reportData =
    const_cast<uint8_t*>(aIn.mReportData.Elements());

  nsresult rv = PackPDU(
    PackConversion<uint32_t, uint16_t>(aIn.mReportData.Length()), aPDU);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return PackPDU(
    PackArray<uint8_t>(reportData, aIn.mReportData.Length()),
    aPDU);
}

nsresult
PackPDU(BluetoothHidProtocolMode aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHidProtocolMode, uint8_t>(aIn), aPDU);
}

nsresult
PackPDU(BluetoothHidReportType aIn, DaemonSocketPDU& aPDU)
{
  return PackPDU(
    PackConversion<BluetoothHidReportType, uint8_t>(aIn), aPDU);
}

//
// Unpacking
//

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothA2dpAudioState& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothA2dpAudioState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothA2dpConnectionState& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothA2dpConnectionState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAclState& aOut)
{
  return UnpackPDU(aPDU, UnpackConversion<uint8_t, BluetoothAclState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAttributeHandle& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<int32_t, BluetoothAttributeHandle>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAvrcpEvent& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothAvrcpEvent>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAvrcpMediaAttribute& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothAvrcpMediaAttribute>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAvrcpPlayerAttribute& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothAvrcpPlayerAttribute>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAvrcpPlayerSettings& aOut)
{
  /* Read number of attribute-value pairs */
  nsresult rv = UnpackPDU(aPDU, aOut.mNumAttr);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* Read attribute-value pairs */
  for (uint8_t i = 0; i < aOut.mNumAttr; ++i) {
    nsresult rv = UnpackPDU(aPDU, aOut.mIds[i]);
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = UnpackPDU(aPDU, aOut.mValues[i]);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  return NS_OK;
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothAvrcpRemoteFeatureBits& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothAvrcpRemoteFeatureBits>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothBondState& aOut)
{
  return UnpackPDU(aPDU, UnpackConversion<uint8_t, BluetoothBondState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothTypeOfDevice& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<int32_t, BluetoothTypeOfDevice>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHandsfreeAudioState& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHandsfreeAudioState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHandsfreeCallHoldType& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHandsfreeCallHoldType>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHandsfreeConnectionState& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHandsfreeConnectionState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHandsfreeNRECState& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHandsfreeNRECState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHandsfreeWbsConfig& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHandsfreeWbsConfig>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU,
          BluetoothHandsfreeVoiceRecognitionState& aOut)
{
  return UnpackPDU(
    aPDU,
    UnpackConversion<uint8_t, BluetoothHandsfreeVoiceRecognitionState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHandsfreeVolumeType& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHandsfreeVolumeType>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothProperty& aOut)
{
  nsresult rv = UnpackPDU(aPDU, aOut.mType);
  if (NS_FAILED(rv)) {
    return rv;
  }
  uint16_t len;
  rv = UnpackPDU(aPDU, len);
  if (NS_FAILED(rv)) {
    return rv;
  }

  switch (aOut.mType) {
    case PROPERTY_BDNAME: {
        const uint8_t* data = aPDU.Consume(len);
        if (MOZ_HAL_IPC_UNPACK_WARN_IF(!data, BluetoothProperty)) {
          return NS_ERROR_ILLEGAL_VALUE;
        }
        // We construct an nsCString here because the string
        // returned from the PDU is not 0-terminated.
        aOut.mRemoteName.Assign(data, len);
      }
      break;
    case PROPERTY_REMOTE_FRIENDLY_NAME: {
        const uint8_t* data = aPDU.Consume(len);
        if (MOZ_HAL_IPC_UNPACK_WARN_IF(!data, BluetoothProperty)) {
          return NS_ERROR_ILLEGAL_VALUE;
        }
        // We construct an nsCString here because the string
        // returned from the PDU is not 0-terminated.
        aOut.mString = NS_ConvertUTF8toUTF16(
          nsCString(reinterpret_cast<const char*>(data), len));
      }
      break;
    case PROPERTY_BDADDR:
      rv = UnpackPDU(aPDU, aOut.mBdAddress);
      break;
    case PROPERTY_UUIDS: {
        size_t numUuids = len / MAX_UUID_SIZE;
        aOut.mUuidArray.SetLength(numUuids);
        rv = UnpackPDU(aPDU, aOut.mUuidArray);
      }
      break;
    case PROPERTY_CLASS_OF_DEVICE:
      /* fall through */
    case PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
      rv = UnpackPDU(aPDU, aOut.mUint32);
      break;
    case PROPERTY_TYPE_OF_DEVICE:
      rv = UnpackPDU(aPDU, aOut.mTypeOfDevice);
      break;
    case PROPERTY_SERVICE_RECORD:
      rv = UnpackPDU(aPDU, aOut.mServiceRecord);
      break;
    case PROPERTY_ADAPTER_SCAN_MODE:
      rv = UnpackPDU(aPDU, aOut.mScanMode);
      break;
    case PROPERTY_ADAPTER_BONDED_DEVICES: {
        /* unpack addresses */
        size_t numAddresses = len / BLUETOOTH_ADDRESS_BYTES;
        aOut.mBdAddressArray.SetLength(numAddresses);
        rv = UnpackPDU(aPDU, aOut.mBdAddressArray);
      }
      break;
    case PROPERTY_REMOTE_RSSI: {
        int8_t rssi;
        rv = UnpackPDU(aPDU, rssi);
        aOut.mInt32 = rssi;
      }
      break;
    case PROPERTY_REMOTE_VERSION_INFO:
      rv = UnpackPDU(aPDU, aOut.mRemoteInfo);
      break;
    case PROPERTY_REMOTE_DEVICE_TIMESTAMP:
      /* nothing to do */
      break;
    default:
      break;
  }
  return rv;
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothPropertyType& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothPropertyType>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothRemoteInfo& aOut)
{
  nsresult rv = UnpackPDU(aPDU,
                          UnpackConversion<uint32_t, int>(aOut.mVerMajor));
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = UnpackPDU(aPDU, UnpackConversion<uint32_t, int>(aOut.mVerMinor));
  if (NS_FAILED(rv)) {
    return rv;
  }
  return UnpackPDU(aPDU, UnpackConversion<uint32_t, int>(aOut.mManufacturer));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothScanMode& aOut)
{
  return UnpackPDU(aPDU, UnpackConversion<int32_t, BluetoothScanMode>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothServiceRecord& aOut)
{
  /* unpack UUID */
  nsresult rv = UnpackPDU(aPDU, aOut.mUuid);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack channel */
  rv = UnpackPDU(aPDU, aOut.mChannel);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack name */
  return aPDU.Read(aOut.mName, sizeof(aOut.mName));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothSspVariant& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothSspVariant>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothStatus& aOut)
{
  return UnpackPDU(aPDU, UnpackConversion<uint8_t, BluetoothStatus>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothGattStatus& aOut)
{
  return UnpackPDU(aPDU, UnpackConversion<int32_t, BluetoothGattStatus>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothGattId& aOut)
{
  /* unpack UUID */
  nsresult rv = UnpackPDU(aPDU, UnpackReversed<BluetoothUuid>(aOut.mUuid));
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack instance id */
  return UnpackPDU(aPDU, aOut.mInstanceId);
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothGattServiceId& aOut)
{
  /* unpack id */
  nsresult rv = UnpackPDU(aPDU, aOut.mId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack isPrimary */
  return UnpackPDU(aPDU, aOut.mIsPrimary);
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothGattReadParam& aOut)
{
  /* unpack service id */
  nsresult rv = UnpackPDU(aPDU, aOut.mServiceId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack characteristic id */
  rv = UnpackPDU(aPDU, aOut.mCharId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack descriptor id */
  rv = UnpackPDU(aPDU, aOut.mDescriptorId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack status */
  rv = UnpackPDU(aPDU, aOut.mStatus);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack value type */
  rv = UnpackPDU(aPDU, aOut.mValueType);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack length */
  rv = UnpackPDU(aPDU, aOut.mValueLength);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack value */
  return aPDU.Read(aOut.mValue, aOut.mValueLength);
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothGattWriteParam& aOut)
{
  /* unpack service id */
  nsresult rv = UnpackPDU(aPDU, aOut.mServiceId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack characteristic id */
  rv = UnpackPDU(aPDU, aOut.mCharId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack descriptor id */
  rv = UnpackPDU(aPDU, aOut.mDescriptorId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack status */
  return UnpackPDU(aPDU, aOut.mStatus);
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothGattNotifyParam& aOut)
{
  /* unpack address */
  nsresult rv = UnpackPDU(aPDU, aOut.mBdAddr);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack service id */
  rv = UnpackPDU(aPDU, aOut.mServiceId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack characteristic id */
  rv = UnpackPDU(aPDU, aOut.mCharId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack isNotify */
  rv = UnpackPDU(aPDU, aOut.mIsNotify);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack length */
  rv = UnpackPDU(aPDU, aOut.mLength);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack value */
  return aPDU.Read(aOut.mValue, aOut.mLength);
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHidInfoParam& aOut)
{
  /* unpack attribute mask */
  nsresult rv = UnpackPDU(aPDU, aOut.mAttributeMask);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack subclass */
  rv = UnpackPDU(aPDU, aOut.mSubclass);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack application id */
  rv = UnpackPDU(aPDU, aOut.mApplicationId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack vendor id */
  rv = UnpackPDU(aPDU, aOut.mVendorId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack product id */
  rv = UnpackPDU(aPDU, aOut.mProductId);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack version */
  rv = UnpackPDU(aPDU, aOut.mVersion);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack country code */
  rv = UnpackPDU(aPDU, aOut.mCountryCode);
  if (NS_FAILED(rv)) {
    return rv;
  }
  /* unpack descriptor length */
  rv = UnpackPDU(aPDU, aOut.mDescriptorLength);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (MOZ_HAL_IPC_PACK_WARN_IF(
        aOut.mDescriptorLength > BLUETOOTH_HID_MAX_DESC_LEN,
        BluetoothHidInfoParam)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  /* unpack descriptor value */
  return UnpackPDU(
    aPDU,
    UnpackArray<uint8_t>(aOut.mDescriptorValue, aOut.mDescriptorLength));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHidReport& aOut)
{
  return UnpackPDU(aPDU, aOut.mReportData);
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHidProtocolMode& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHidProtocolMode>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHidConnectionState& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHidConnectionState>(aOut));
}

nsresult
UnpackPDU(DaemonSocketPDU& aPDU, BluetoothHidStatus& aOut)
{
  return UnpackPDU(
    aPDU, UnpackConversion<uint8_t, BluetoothHidStatus>(aOut));
}

END_BLUETOOTH_NAMESPACE
