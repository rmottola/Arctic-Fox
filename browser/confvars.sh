#! /bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

MOZ_APP_BASENAME=ArcticFox
MOZ_APP_VENDOR=org.multix
MOZ_UPDATER=1
MOZ_PHOENIX=1

if test "$OS_ARCH" = "WINNT"; then
  MOZ_MAINTENANCE_SERVICE=1
  if ! test "$HAVE_64BIT_BUILD"; then
    if test "$MOZ_UPDATE_CHANNEL" = "nightly" -o \
            "$MOZ_UPDATE_CHANNEL" = "beta" -o \
            "$MOZ_UPDATE_CHANNEL" = "beta-dev" -o \
            "$MOZ_UPDATE_CHANNEL" = "release" -o \
            "$MOZ_UPDATE_CHANNEL" = "release-dev"; then
      if ! test "$MOZ_DEBUG"; then
        MOZ_STUB_INSTALLER=1
      fi
    fi
  fi
fi

# Enable building ./signmar and running libmar signature tests
MOZ_ENABLE_SIGNMAR=1

MOZ_CHROME_FILE_FORMAT=omni
MOZ_SERVICES_COMMON=1
MOZ_SERVICES_CRYPTO=1
MOZ_SERVICES_SYNC=1
MOZ_APP_VERSION=$FIREFOX_VERSION
MOZ_APP_VERSION_DISPLAY=$FIREFOX_VERSION_DISPLAY
MOZ_EXTENSIONS_DEFAULT=" gio"

MOZ_SERVICES_FXACCOUNTS=1
MOZ_DISABLE_EXPORT_JS=1
MOZ_WEBGL_CONFORMANT=1
MOZ_ACTIVITIES=1
MOZ_JSDOWNLOADS=1
MOZ_WEBM_ENCODER=1
MOZ_RUST_MP4PARSE=1

# Enable checking that add-ons are signed by the trusted root
MOZ_ADDON_SIGNING=1
if test "$MOZ_OFFICIAL_BRANDING"; then
  if test "$MOZ_UPDATE_CHANNEL" = "beta" -o \
          "$MOZ_UPDATE_CHANNEL" = "release" -o \
          "$MOZ_UPDATE_CHANNEL" = "esr"; then
    MOZ_REQUIRE_SIGNING=1
  fi
fi

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
MOZ_MEDIA_NAVIGATOR=1
if test "$OS_TARGET" = "WINNT" -o "$OS_TARGET" = "Darwin"; then
  MOZ_FOLD_LIBS=1
fi

# Include the DevTools client, not just the server (which is the default)
MOZ_DEVTOOLS=all
