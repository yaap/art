 #!/bin/bash
#
# Copyright (C) 2023 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Builds and packages ART host tools for linux-x86.
#
# Usage:
#   build-linux-x86-host-tools.sh [options]
#
# Options:
#   --stage=host_binaries  Build only the host binaries.
#   --stage=art_release    Build only the ART release.
#   --stage=package        Package the host tools.
#
# If the user doesn't specify any flags, they will all be run in the order
# (art release -> binaries -> packaging).
# If multiple flags are used, the script will run them in the correct order e.g.
# build-linux-x86-host-tools.sh --stage=packaging --stage=art_release
# will build the art_release and then the packaging stage. In this example,
# it is the user's responsibility to have built the host binaries beforehand.

set -e

# Flags used to store which stages are run.
BUILD_HOST_BINARIES=false
BUILD_ART_RELEASE=false
PACKAGE=false

# Check if any stage is specified
STAGE_SPECIFIED=false
for arg in "$@"
do
    case $arg in
        --stage=*)
        STAGE_SPECIFIED=true
        break
        ;;
    esac
done

# Default to all stages if no stage argument is provided
if [ "$STAGE_SPECIFIED" = false ]; then
    BUILD_HOST_BINARIES=true
    BUILD_ART_RELEASE=true
    PACKAGE=true
else
    for arg in "$@"
    do
        case $arg in
            --stage=host_binaries)
            BUILD_HOST_BINARIES=true
            ;;
            --stage=art_release)
            BUILD_ART_RELEASE=true
            ;;
            --stage=package)
            PACKAGE=true
            ;;
        esac
    done
fi

if [ ! -e 'build/make/core/Makefile' ]; then
  echo "Script $0 needs to be run at the root of the android tree"
  exit 1
fi

vars="$(build/soong/soong_ui.bash --dumpvars-mode --vars="OUT_DIR DIST_DIR")"
# Assign to a variable and eval that, since bash ignores any error status from
# the command substitution if it's directly on the eval line.
eval $vars

HOST_BINARIES=(
  ${OUT_DIR}/host/linux-x86/bin/aapt2
  ${OUT_DIR}/host/linux-x86/bin/apksigner
  ${OUT_DIR}/host/linux-x86/bin/aprotoc
  ${OUT_DIR}/host/linux-x86/bin/deapexer
  ${OUT_DIR}/host/linux-x86/bin/debugfs_static
  ${OUT_DIR}/host/linux-x86/bin/derive_classpath
  ${OUT_DIR}/host/linux-x86/bin/dex2oat
  ${OUT_DIR}/host/linux-x86/bin/dex2oat64
  ${OUT_DIR}/host/linux-x86/bin/dex2oatd
  ${OUT_DIR}/host/linux-x86/bin/dex2oatd64
  ${OUT_DIR}/host/linux-x86/bin/oatdump
  ${OUT_DIR}/host/linux-x86/bin/profman
  ${OUT_DIR}/host/linux-x86/bin/soong_zip
  ${OUT_DIR}/host/linux-x86/bin/zipalign
  ${OUT_DIR}/host/linux-x86/framework/apksigner.jar
)

SOURCE_FILES=(
  system/apex/proto/apex_manifest.proto
)

if [ "$BUILD_ART_RELEASE" = true ]; then
  echo "Building ART Release"
  # Build art_release.zip and copy only art jars in a temporary zip
  build/soong/soong_ui.bash --make-mode dist "${DIST_DIR}/art_release.zip"
  prebuilts/build-tools/linux-x86/bin/zip2zip -i "${DIST_DIR}/art_release.zip" \
    -o "${DIST_DIR}/temp-art-jars.zip" "bootjars/*" "licenses/*/*"
  rm -f "${DIST_DIR}/art_release.zip"
fi

if [ "$BUILD_HOST_BINARIES" = true ]; then
  echo "Building Host Binaries"
  # Build statically linked musl binaries for linux-x86 hosts without the
  # standard glibc implementation.
  build/soong/soong_ui.bash --make-mode USE_HOST_MUSL=true BUILD_HOST_static=true ${HOST_BINARIES[*]}
  # Zip these binaries in a temporary file
  prebuilts/build-tools/linux-x86/bin/soong_zip -o "${DIST_DIR}/temp-host-tools.zip" \
    -j ${HOST_BINARIES[*]/#/-f } ${SOURCE_FILES[*]/#/-f }
fi

if [ "$PACKAGE" = true ]; then
  echo "Packaging Host Tools"
  if [ ! -f "${DIST_DIR}/temp-host-tools.zip" ]; then
    echo "Error: Missing ${DIST_DIR}/temp-host-tools.zip for packaging."
    exit 1
  fi
  if [ ! -f "${DIST_DIR}/temp-art-jars.zip" ]; then
    echo "Error: Missing ${DIST_DIR}/temp-art-jars.zip for packaging."
    exit 1
  fi
  # Merge both temporary zips into output zip
  prebuilts/build-tools/linux-x86/bin/merge_zips "${DIST_DIR}/art-host-tools-linux-x86.zip" \
    "${DIST_DIR}/temp-host-tools.zip" "${DIST_DIR}/temp-art-jars.zip"

  # Delete temporary zips
  rm "${DIST_DIR}/temp-host-tools.zip" "${DIST_DIR}/temp-art-jars.zip"
fi
