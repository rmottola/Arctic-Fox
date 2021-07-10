#! /bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

MOZ_APP_BASENAME=ArcticFox
MOZ_APP_VENDOR=org.wicknix
MOZ_UPDATER=1
MOZ_PHOENIX=1

if test "$OS_TARGET" = "WINNT"; then
  MOZ_BUNDLED_FONTS=1
elif test "$OS_ARCH" = "Linux"; then
  MOZ_VERIFY_MAR_SIGNATURE=1
elif test "$OS_ARCH" = "Darwin"; then
  MOZ_VERIFY_MAR_SIGNATURE=1
fi

# Enable building ./signmar and running libmar signature tests
MOZ_ENABLE_SIGNMAR=1

MOZ_CHROME_FILE_FORMAT=omni
MOZ_SERVICES_COMMON=1
MOZ_MEDIA_NAVIGATOR=1
MOZ_SERVICES_CRYPTO=1
MOZ_SERVICES_SYNC=1
MOZ_APP_VERSION=`cat ${_topsrcdir}/$MOZ_BUILD_APP/config/version.txt`
MOZ_EXTENSIONS_DEFAULT=" gio"

MOZ_SERVICES_FXACCOUNTS=1
MOZ_DISABLE_EXPORT_JS=1
MOZ_WEBGL_CONFORMANT=1
MOZ_ACTIVITIES=1
MOZ_JSDOWNLOADS=1
MOZ_WEBM_ENCODER=1

MOZ_PHOENIX_EXTENSIONS=1
MOZ_BROWSER_STATUSBAR=1

#Enable devtools by default. Can be disabled with --disable-devtools in mozconfig.
MOZ_DEVTOOLS=1

# MOZ_APP_DISPLAYNAME will be set by branding/configure.sh
# Changing MOZ_*BRANDING_DIRECTORY requires a clobber to ensure correct results,
# because branding dependencies are broken.
# MOZ_BRANDING_DIRECTORY is the default branding directory used when none is
# specified. It should never point to the "official" branding directory.
# For mozilla-beta, mozilla-release, or mozilla-central repositories, use
# "nightly" branding (until bug 659568 is fixed).
# For the mozilla-aurora repository, use "aurora".
MOZ_BRANDING_DIRECTORY=browser/branding/arcticfox
MOZ_OFFICIAL_BRANDING_DIRECTORY=browser/branding/arcticfox
# New Pale Moon App GUID
# Firefox MOZ_APP_ID={ec8030f7-c20a-464f-9b0e-13a3a9e97384}
MOZ_APP_ID={8de7fcbb-c55c-4fbe-bfc5-fc555c87dbc4}
# This should usually be the same as the value MAR_CHANNEL_ID.
# If more than one ID is needed, then you should use a comma separated list
# of values.
ACCEPTED_MAR_CHANNEL_IDS=palemoon-release
# The MAR_CHANNEL_ID must not contain the following 3 characters: ",\t "
MAR_CHANNEL_ID=palemoon-release
MOZ_PROFILE_MIGRATOR=1
MOZ_EXTENSION_MANAGER=1
MOZ_APP_STATIC_INI=1
if test "$OS_TARGET" = "WINNT" -o "$OS_TARGET" = "Darwin"; then
  MOZ_FOLD_LIBS=1
fi
