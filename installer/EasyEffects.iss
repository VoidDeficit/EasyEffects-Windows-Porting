; Inno Setup script for EasyEffects for Windows.
;
; Built by .github/workflows/release.yml:
;   ISCC installer\EasyEffects.iss /DAppVersion=0.2.0 /DRootDir=<repo> /DOutputDir=<repo>\releases
;
; The installed copy differs from the portable zip in three ways:
;   - install.ini marks it as installed, so the app can update itself
;     (it downloads the next *-Setup.exe and runs it silently);
;   - the "\EasyEffects\Launch" scheduled task lets it run with administrator
;     rights without a UAC prompt (needed for Synchronous Audio Router);
;   - optionally the "\EasyEffects\Autostart" task starts it at sign-in.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef RootDir
  #define RootDir ".."
#endif
#ifndef OutputDir
  #define OutputDir RootDir + "\releases"
#endif
#ifndef Repo
  #define Repo "VoidDeficit/EasyEffects-Windows-Porting"
#endif
#define BuildDir RootDir + "\build\EasyEffects_artefacts\Release"
; Icon the build generates from assets\icon.png
#define IconFile RootDir + "\build\EasyEffects_artefacts\JuceLibraryCode\icon.ico"

[Setup]
AppId={{6B0F5E2A-3C1D-4E7B-9A8F-2D4C6E8B1A35}
AppName=EasyEffects for Windows
AppVersion={#AppVersion}
AppVerName=EasyEffects for Windows {#AppVersion}
AppPublisher=EasyEffects for Windows contributors
AppPublisherURL=https://github.com/{#Repo}
AppSupportURL=https://github.com/{#Repo}/issues
AppUpdatesURL=https://github.com/{#Repo}/releases
DefaultDirName={autopf}\EasyEffects
DefaultGroupName=EasyEffects
DisableProgramGroupPage=yes
LicenseFile={#RootDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=EasyEffects-Windows-v{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\EasyEffects.exe
UninstallDisplayName=EasyEffects for Windows
VersionInfoVersion={#AppVersion}
#if FileExists(IconFile)
SetupIconFile={#IconFile}
#endif

[Tasks]
Name: "autostart";   Description: "Start EasyEffects automatically when I sign in (with administrator rights)"; GroupDescription: "Startup:"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\Standalone\EasyEffects.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RootDir}\installer\tasks.ps1";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#RootDir}\QUICK_START.md";             DestDir: "{app}"; Flags: ignoreversion
Source: "{#RootDir}\LICENSE";                    DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; The VST3 plug-in was dropped after 0.2.3; remove a copy installed by an older version.
Type: filesandordirs; Name: "{commoncf64}\VST3\EasyEffects.vst3"

[INI]
Filename: "{app}\install.ini"; Section: "Install"; Key: "Channel"; String: "installer"
Filename: "{app}\install.ini"; Section: "Install"; Key: "Repository"; String: "{#Repo}"

[Icons]
Name: "{autoprograms}\EasyEffects"; Filename: "{app}\EasyEffects.exe"
Name: "{autodesktop}\EasyEffects";  Filename: "{app}\EasyEffects.exe"; Tasks: desktopicon

[Run]
; Scheduled tasks for elevated starts (with or without the sign-in trigger).
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tasks.ps1"" -Install -Exe ""{app}\EasyEffects.exe"" -Autostart"; Flags: runhidden waituntilterminated; Tasks: autostart; StatusMsg: "Registering EasyEffects with Task Scheduler..."
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tasks.ps1"" -Install -Exe ""{app}\EasyEffects.exe"""; Flags: runhidden waituntilterminated; Tasks: not autostart; StatusMsg: "Registering EasyEffects with Task Scheduler..."
; Start the app afterwards. It hands itself over to the Launch task, so it
; ends up running with administrator rights without a UAC prompt. Also runs
; after a silent self-update.
Filename: "{app}\EasyEffects.exe"; Description: "{cm:LaunchProgram,EasyEffects}"; Flags: nowait postinstall runasoriginaluser

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tasks.ps1"" -Uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveTasks"

[UninstallDelete]
Type: files; Name: "{app}\install.ini"
