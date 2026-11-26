#!/usr/bin/env bash
# Builds Thunder's macOS installer package from the unpacked macOS zip.
#
#   packaging/macos/build-pkg.sh <version> <unpacked package folder> <output .pkg>
#
# The package installs the AU, VST3 and CLAP plugins into the standard folders
# for all users (/Library/Audio/Plug-Ins/...); the installer's Customize button
# lets people leave formats out. Plugins installed this way are not held back
# by macOS's quarantine, so nothing has to be run in Terminal. Presets and
# captures in ~/Documents/Thunder are never touched.
set -euo pipefail

version="$1"
source_dir="$2"
output="$3"
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# One component package per format. Bundles are marked non-relocatable: by
# default Installer would put the new version wherever it finds a bundle with
# the same ID (an old copy in ~/Library, a build folder) instead of the folder
# below.
component() {
    local id="$1" bundle="$2" location="$3"
    mkdir -p "$work/root-$id"
    cp -R "$source_dir/$bundle" "$work/root-$id/"
    pkgbuild --analyze --root "$work/root-$id" "$work/$id.plist" > /dev/null
    # Each root holds exactly one bundle, so the plist has one entry.
    if plutil -extract 1 xml1 -o - "$work/$id.plist" > /dev/null 2>&1; then
        echo "Expected one bundle in $bundle" >&2
        exit 1
    fi
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/$id.plist"
    pkgbuild --root "$work/root-$id" --component-plist "$work/$id.plist" \
        --identifier "com.prodbyrnv.thunder.$id" --version "$version" \
        --install-location "$location" "$work/thunder-$id.pkg" > /dev/null
}

component au AU/Thunder.component /Library/Audio/Plug-Ins/Components
component vst3 VST3/Thunder.vst3 /Library/Audio/Plug-Ins/VST3
component clap CLAP/Thunder.clap /Library/Audio/Plug-Ins/CLAP

sed "s/@VERSION@/$version/g" "$here/distribution.xml" > "$work/distribution.xml"
mkdir -p "$work/resources"
cp "$here/welcome.html" "$work/resources/"

productbuild --distribution "$work/distribution.xml" --package-path "$work" \
    --resources "$work/resources" "$output"
echo "Built $output"
