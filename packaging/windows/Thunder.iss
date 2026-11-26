; Thunder's Windows installer (Inno Setup 6).
;
; Built by CI from the same files as the Windows zip:
;   iscc /DAppVersion=1.0.0 /DSourceDir=<unpacked package folder> /DOutputDir=<folder> packaging\windows\Thunder.iss
;
; Installs the VST3 and CLAP plugins into the standard folders every Windows DAW
; scans, and registers an uninstaller under Settings > Apps. Presets and
; captures in Documents\Thunder are never touched, also not on uninstall.

#ifndef AppVersion
  #error Pass the version: /DAppVersion=x.y.z
#endif
#ifndef SourceDir
  #error Pass the unpacked package folder: /DSourceDir=...
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; The AppId identifies Thunder to Windows across versions, so a new version
; replaces the old one. Never change it.
AppId={{722E3CD1-ABB9-45AB-B473-AAE474170C11}
AppName=Thunder
AppVersion={#AppVersion}
AppVerName=Thunder {#AppVersion}
AppPublisher=ProdByRnV
AppPublisherURL=https://github.com/ProdByRnV/Thunder
AppSupportURL=https://github.com/ProdByRnV/Thunder/issues
VersionInfoVersion={#AppVersion}
; The uninstaller and the third-party notices live here; the plugins go into
; the shared plugin folders below.
DefaultDirName={autopf}\Thunder
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Thunder-{#AppVersion}-windows-x86_64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Thunder (audio plugin)
CloseApplications=no

[Types]
Name: "full"; Description: "VST3 and CLAP"
Name: "custom"; Description: "Choose the formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (FL Studio, Ableton Live, Cubase, Studio One, Reaper, …)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (Bitwig, Reaper, …)"; Types: full custom

[InstallDelete]
; A new version replaces the whole bundle, so no file from an older one is left.
Type: filesandordirs; Name: "{commoncf64}\VST3\Thunder.vst3"; Components: vst3

[Files]
Source: "{#SourceDir}\VST3\Thunder.vst3\*"; DestDir: "{commoncf64}\VST3\Thunder.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\CLAP\Thunder.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#SourceDir}\Third-party notices.txt"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Thunder.vst3"

[Messages]
FinishedLabel=Thunder is installed. Open your DAW and rescan its plugins (in FL Studio: Options > Manage plugins > Find installed plugins), then add Thunder as an effect on the track with your melody.%n%nMade by @ProdByRnV. Follow on Instagram for new sounds and updates: instagram.com/prodbyrnv
