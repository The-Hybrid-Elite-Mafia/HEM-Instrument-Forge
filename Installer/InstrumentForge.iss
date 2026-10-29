; HEM Instrument Forge - Inno Setup 6 script
;
; Build:  iscc /DBuildDir="<InstrumentForge_artefacts\Release>" /DLibraryDir="<built library>" /DAppVersion=1.0.0 Installer\InstrumentForge.iss
;
; Its own app: own AppId, install folder, settings and file type (.hemforge); it never touches
; Hybrid Studio or the Rework Playground.
;
; Upgrade safety:
;   - AppId is fixed forever -> new versions install over the old one in place.
;   - CloseApplications + AppMutex: setup will not overwrite the app while it is open (it asks to close it).
;   - Settings live in %APPDATA%\Hybrid Elite Mafia\Instrument Forge.settings (written by the app,
;     never listed here, so upgrades and uninstall leave them alone).
;   - Songs and exports live in Documents\Hybrid Elite Mafia\Instrument Forge, created with
;     uninsneveruninstall: your work survives upgrades and uninstall.
;   - The instrument library goes to {app}\Library and is replaced as a whole on every upgrade
;     (nothing of yours is stored there).
;   - The library is several GB, so the installer is split: the .exe plus .bin parts, which must be
;     in the same folder when you run it. FLAC samples are stored without recompression.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\InstrumentForge_artefacts\Release"
#endif

#define AppName      "HEM Instrument Forge"
#define AppPublisher "Hybrid Elite Mafia"
#define AppExe       "HEM Instrument Forge.exe"
#define ProgId       "HybridEliteMafia.HemForgeSong"

[Setup]
AppId={{C7C5D78A-12C1-4732-9DF0-379F2715E641}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppMutex=HEMInstrumentForgeRunning
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
UsePreviousAppDir=yes
UsePreviousGroup=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=HEM-Instrument-Forge-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/fast
SolidCompression=no
DiskSpanning=yes
DiskSliceSize=1900000000
SlicesPerDisk=1
WizardStyle=modern
PrivilegesRequired=admin
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
SetupIconFile=HEM-Forge.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoCopyright=Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia
AppCopyright=Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia. Free software - all rights reserved.
LicenseFile=EULA.txt
ExtraDiskSpaceRequired=104857600

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "fileassoc"; Description: "Open .hemforge song files with {#AppName}"; GroupDescription: "File associations:"

[Dirs]
Name: "{userdocs}\Hybrid Elite Mafia\Instrument Forge"; Flags: uninsneveruninstall
Name: "{userdocs}\Hybrid Elite Mafia\Instrument Forge\Songs"; Flags: uninsneveruninstall
Name: "{userdocs}\Hybrid Elite Mafia\Instrument Forge\Exports"; Flags: uninsneveruninstall

[InstallDelete]
; the library from an older version is replaced as a whole (no stale samples left behind)
Type: filesandordirs; Name: "{app}\Library"

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "EULA.txt"; DestDir: "{app}"; DestName: "License.txt"; Flags: ignoreversion
#ifdef LibraryDir
Source: "{#LibraryDir}\*.flac"; DestDir: "{app}\Library"; Flags: ignoreversion recursesubdirs createallsubdirs nocompression
Source: "{#LibraryDir}\*.json"; DestDir: "{app}\Library"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#LibraryDir}\*.txt"; DestDir: "{app}\Library"; Flags: ignoreversion
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{#AppName} License"; Filename: "{app}\License.txt"
Name: "{group}\Instrument Forge Songs"; Filename: "{userdocs}\Hybrid Elite Mafia\Instrument Forge"
Name: "{group}\Instrument Library Credits"; Filename: "{app}\Library\CREDITS.txt"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.hemforge"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "Instrument Forge Song"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
