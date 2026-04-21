#define MyAppName "Audiocity"
#define MyAppVersion "1.0.0"

#ifndef SourceRoot
  #error SourceRoot must point at the staged release files.
#endif

#ifndef OutputDir
  #define OutputDir "..\\output"
#endif

#ifndef OutputBaseFilename
  #define OutputBaseFilename "Audiocity-1.0.0-windows-x64-setup"
#endif

[Setup]
AppId={{7A0C79C3-8AB4-4FD7-9B3C-B00FC2D98EB4}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=Audiocity
AppCopyright=Copyright (c) 2026 Michael A. McCloskey
AppPublisherURL=https://github.com/thetheosopher/Audiocity
AppSupportURL=https://github.com/thetheosopher/Audiocity
AppUpdatesURL=https://github.com/thetheosopher/Audiocity
DefaultDirName={code:GetDefaultInstallDir}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile=..\assets\icons\audiocity_icon_multi.ico
UninstallDisplayIcon={app}\Audiocity.exe
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Types]
Name: "full"; Description: "Standalone application and VST3 plugin"
Name: "standalone"; Description: "Standalone application only"
Name: "vst3"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone application"; Types: full standalone custom
Name: "vst3"; Description: "VST3 plugin"; Types: full vst3 custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceRoot}\standalone\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: standalone
Source: "{#SourceRoot}\VST3\Audiocity.vst3\*"; DestDir: "{code:GetVst3InstallDir}\Audiocity.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{userprograms}\Audiocity\Audiocity"; Filename: "{app}\Audiocity.exe"; WorkingDir: "{app}"; Components: standalone; Check: not IsAdminInstallMode
Name: "{commonprograms}\Audiocity\Audiocity"; Filename: "{app}\Audiocity.exe"; WorkingDir: "{app}"; Components: standalone; Check: IsAdminInstallMode
Name: "{userprograms}\Audiocity\Uninstall Audiocity"; Filename: "{uninstallexe}"; Components: standalone; Check: not IsAdminInstallMode
Name: "{commonprograms}\Audiocity\Uninstall Audiocity"; Filename: "{uninstallexe}"; Components: standalone; Check: IsAdminInstallMode
Name: "{userdesktop}\Audiocity"; Filename: "{app}\Audiocity.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Components: standalone; Check: not IsAdminInstallMode
Name: "{commondesktop}\Audiocity"; Filename: "{app}\Audiocity.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Components: standalone; Check: IsAdminInstallMode

[Code]
function GetDefaultInstallDir(Param: string): string;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commonpf64}\Audiocity')
  else
    Result := ExpandConstant('{localappdata}\Programs\Audiocity');
end;

function GetVst3InstallDir(Param: string): string;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commoncf64}\VST3')
  else
    Result := ExpandConstant('{localappdata}\Programs\Common\VST3');
end;
