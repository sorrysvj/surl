; ---------------------------------------------------------------------------
; SURL Windows installer (Inno Setup 6.3+)
;
; Build it with:
;   ISCC.exe /DPayloadDir=<dir containing surl.exe> installer\windows\surl.iss
;
; The version is read from VERSION.txt so nothing has to be kept in sync by
; hand; pass /DAppVersion=... only to override it deliberately.
;
; Pages, in order:
;   Language -> Welcome -> License -> Install scope -> Directory ->
;   Components/Tasks (PATH, shortcuts) -> Source -> Ready -> Installing ->
;   Finish
;
; The installer ships surl.exe inside itself so it works offline, and can
; additionally fetch the published release asset from GitHub, verifying its
; SHA-256 against the release's checksums.txt before installing it.
; ---------------------------------------------------------------------------

#ifndef AppVersion
  #define VersionFile FileOpen(AddBackslash(SourcePath) + "..\..\VERSION.txt")
  #define AppVersion Trim(FileRead(VersionFile))
  #expr FileClose(VersionFile)
#endif

#if AppVersion == ""
  #error Could not determine the SURL version. Pass /DAppVersion=x.y.z
#endif

#ifndef PayloadDir
  #define PayloadDir "..\..\build\Release"
#endif

#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName          "SURL"
#define AppPublisher     "sorrysvj"
#define AppUrl           "https://github.com/sorrysvj/surl"
#define AppSupportUrl    "https://github.com/sorrysvj/surl/issues"
#define AppUpdatesUrl    "https://github.com/sorrysvj/surl/releases"
#define AppExeName       "surl.exe"
#define GitHubOwner      "sorrysvj"
#define GitHubRepo       "surl"
#define ReleaseAssetName "surl-windows-x64.zip"

[Setup]
; A stable AppId is what makes upgrading in place work instead of installing
; a second copy alongside the first.
AppId={{9F2C6E14-4E3B-4C7A-9E2D-1B7A0C55D3A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppSupportUrl}
AppUpdatesURL={#AppUpdatesUrl}

; Default to a per-user install so no UAC prompt appears unless the user asks
; for a machine-wide one on the scope page.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}

LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=surl-windows-x64-installer
SetupIconFile=assets\surl.ico
WizardImageFile=assets\wizard-large.bmp
WizardSmallImageFile=assets\wizard-small.bmp
WizardStyle=modern
WizardSizePercent=110

Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes

; SURL is a 64-bit console tool; there is no 32-bit build to fall back to.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; Tell the shell the environment changed so new terminals pick up PATH.
ChangesEnvironment=yes
CloseApplications=no
RestartIfNeededByRun=no
ShowLanguageDialog=yes
UsePreviousLanguage=yes
DisableWelcomePage=no
AppMutex=Global\SURL-Installer-Mutex

[Languages]
; Each language pairs Inno's own translation with SURL's custom strings, so
; adding German/French/Spanish later is one line plus one .isl file.
Name: "english"; MessagesFile: "compiler:Default.isl,translations\surl.en.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl,translations\surl.ru.isl"

[Tasks]
Name: "addtopath"; Description: "{cm:TaskAddToPath}"; GroupDescription: "{cm:TaskGroupIntegration}"
Name: "startmenu"; Description: "{cm:TaskStartMenu}"; GroupDescription: "{cm:TaskGroupShortcuts}"
Name: "desktopicon"; Description: "{cm:TaskDesktopIcon}"; GroupDescription: "{cm:TaskGroupShortcuts}"; Flags: unchecked

[Files]
; The bundled payload. "external" + a run-time source lets the same script
; install either the shipped binary or one downloaded from GitHub.
Source: "{#PayloadDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion; Check: not UseDownloadedPayload
Source: "{tmp}\extracted\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion external skipifsourcedoesntexist; Check: UseDownloadedPayload

Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "assets\surl.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{cm:ShortcutOpenShell}"; Filename: "{sys}\cmd.exe"; \
    Parameters: "/K ""{app}\{#AppExeName}"" --help"; WorkingDir: "{%USERPROFILE|{app}}"; \
    IconFilename: "{app}\surl.ico"; Tasks: startmenu
Name: "{group}\{cm:ShortcutDocumentation}"; Filename: "{#AppUrl}"; \
    IconFilename: "{app}\surl.ico"; Tasks: startmenu
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"; Tasks: startmenu

Name: "{autodesktop}\{#AppName}"; Filename: "{sys}\cmd.exe"; \
    Parameters: "/K ""{app}\{#AppExeName}"" --help"; WorkingDir: "{%USERPROFILE|{app}}"; \
    IconFilename: "{app}\surl.ico"; Tasks: desktopicon

[Registry]
; Only install metadata lives here. Runtime data belongs in %APPDATA%\SURL and
; %LOCALAPPDATA%\SURL, which the uninstaller deliberately leaves alone.
Root: HKA; Subkey: "Software\{#AppPublisher}\{#AppName}"; ValueType: string; \
    ValueName: "InstallLocation"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\{#AppPublisher}\{#AppName}"; ValueType: string; \
    ValueName: "Version"; ValueData: "{#AppVersion}"
Root: HKA; Subkey: "Software\{#AppPublisher}\{#AppName}"; ValueType: string; \
    ValueName: "Publisher"; ValueData: "{#AppPublisher}"

[Run]
Filename: "{cmd}"; Parameters: "/K ""{app}\{#AppExeName}"" --help"; \
    Description: "{cm:RunOpenShell}"; Flags: postinstall skipifsilent nowait unchecked
Filename: "{#AppUrl}"; Description: "{cm:RunOpenDocs}"; \
    Flags: postinstall skipifsilent shellexec nowait unchecked

[UninstallDelete]
; Anything SURL's own installer created but that is not tracked as a file.
Type: dirifempty; Name: "{app}"

[Code]
const
  { PATH is stored in different places depending on the install scope. }
  UserEnvironmentKey   = 'Environment';
  SystemEnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

var
  SourcePage: TInputOptionWizardPage;
  DownloadPage: TDownloadWizardPage;
  UsingDownload: Boolean;
  DownloadedExePath: String;

{ ------------------------------------------------------------------------- }
{ Helpers                                                                    }
{ ------------------------------------------------------------------------- }

function UseDownloadedPayload: Boolean;
begin
  Result := UsingDownload;
end;

function IsMachineWide: Boolean;
begin
  Result := IsAdminInstallMode;
end;

function EnvironmentRootKey: Integer;
begin
  if IsMachineWide then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

function EnvironmentSubKey: String;
begin
  if IsMachineWide then
    Result := SystemEnvironmentKey
  else
    Result := UserEnvironmentKey;
end;

{ Normalises a PATH entry for comparison: trimmed, lower-cased, no trailing
  separator. This is what stops a re-install adding a second copy. }
function NormalisePathEntry(const Value: String): String;
begin
  Result := Trim(Value);
  while (Length(Result) > 0) and
        ((Result[Length(Result)] = '\') or (Result[Length(Result)] = '/')) do
    Result := Copy(Result, 1, Length(Result) - 1);
  Result := Lowercase(Result);
end;

function ReadRawPath(var Value: String): Boolean;
begin
  Value := '';
  Result := RegQueryStringValue(EnvironmentRootKey, EnvironmentSubKey, 'Path', Value);
  if not Result then
  begin
    { A user who has never had a PATH value simply has no key yet. }
    Result := not IsMachineWide;
    Value := '';
  end;
end;

function PathContainsDirectory(const RawPath, Directory: String): Boolean;
var
  Parts: TArrayOfString;
  I: Integer;
  Target: String;
begin
  Result := False;
  Target := NormalisePathEntry(Directory);
  if Target = '' then
    Exit;

  Parts := StringSplitEx(RawPath, [';'], #0, stExcludeEmpty);
  for I := 0 to GetArrayLength(Parts) - 1 do
    if NormalisePathEntry(Parts[I]) = Target then
    begin
      Result := True;
      Exit;
    end;
end;

procedure AddDirectoryToPath(const Directory: String);
var
  RawPath: String;
  Updated: String;
begin
  if not ReadRawPath(RawPath) then
  begin
    Log('Could not read PATH; skipping PATH integration.');
    Exit;
  end;

  if PathContainsDirectory(RawPath, Directory) then
  begin
    Log('PATH already contains ' + Directory + '; not adding it twice.');
    Exit;
  end;

  Updated := RawPath;
  if (Length(Updated) > 0) and (Updated[Length(Updated)] <> ';') then
    Updated := Updated + ';';
  Updated := Updated + Directory;

  if RegWriteExpandStringValue(EnvironmentRootKey, EnvironmentSubKey, 'Path', Updated) then
    Log('Added ' + Directory + ' to PATH.')
  else
    Log('Failed to write PATH.');
end;

procedure RemoveDirectoryFromPath(const Directory: String);
var
  RawPath: String;
  Parts: TArrayOfString;
  Rebuilt: String;
  Target: String;
  I: Integer;
  Removed: Boolean;
begin
  if not ReadRawPath(RawPath) then
    Exit;

  Target := NormalisePathEntry(Directory);
  Parts := StringSplitEx(RawPath, [';'], #0, stExcludeEmpty);
  Rebuilt := '';
  Removed := False;

  for I := 0 to GetArrayLength(Parts) - 1 do
  begin
    if NormalisePathEntry(Parts[I]) = Target then
    begin
      Removed := True;
      Continue;
    end;
    if Rebuilt <> '' then
      Rebuilt := Rebuilt + ';';
    Rebuilt := Rebuilt + Parts[I];
  end;

  if not Removed then
    Exit;

  if RegWriteExpandStringValue(EnvironmentRootKey, EnvironmentSubKey, 'Path', Rebuilt) then
    Log('Removed ' + Directory + ' from PATH.');
end;

{ Cleans up any stale SURL directories left on PATH by an install that has
  since moved (per-user to machine-wide, or a different folder). Without this a
  user who reinstalls elsewhere ends up with several surl.exe on PATH. }
procedure RemoveStaleSurlPathEntries(const KeepDirectory: String);
var
  RawPath: String;
  Parts: TArrayOfString;
  Rebuilt: String;
  Keep: String;
  Candidate: String;
  I: Integer;
  Changed: Boolean;
begin
  if not ReadRawPath(RawPath) then
    Exit;

  Keep := NormalisePathEntry(KeepDirectory);
  Parts := StringSplitEx(RawPath, [';'], #0, stExcludeEmpty);
  Rebuilt := '';
  Changed := False;

  for I := 0 to GetArrayLength(Parts) - 1 do
  begin
    Candidate := NormalisePathEntry(Parts[I]);
    { Only drop entries that look like a SURL install and no longer hold the
      executable: never touch anything else the user put on PATH. }
    if (Candidate <> Keep) and (Pos('\surl', Candidate) > 0) and
       (not FileExists(Parts[I] + '\{#AppExeName}')) then
    begin
      Log('Dropping stale PATH entry ' + Parts[I]);
      Changed := True;
      Continue;
    end;
    if Rebuilt <> '' then
      Rebuilt := Rebuilt + ';';
    Rebuilt := Rebuilt + Parts[I];
  end;

  if Changed then
    RegWriteExpandStringValue(EnvironmentRootKey, EnvironmentSubKey, 'Path', Rebuilt);
end;

{ ------------------------------------------------------------------------- }
{ Download path                                                              }
{ ------------------------------------------------------------------------- }

function ReleaseBaseUrl: String;
begin
  Result := 'https://github.com/{#GitHubOwner}/{#GitHubRepo}/releases/download/v{#AppVersion}/';
end;

{ Pulls the expected SHA-256 for a file name out of a checksums.txt in the
  usual "<hash>  <name>" format. }
function ExpectedHashFromChecksums(const ChecksumsPath, FileName: String;
  var Hash: String): Boolean;
var
  Lines: TArrayOfString;
  I: Integer;
  Line: String;
  SpacePos: Integer;
  Candidate: String;
begin
  Result := False;
  Hash := '';
  if not LoadStringsFromFile(ChecksumsPath, Lines) then
    Exit;

  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Trim(Lines[I]);
    if Line = '' then
      Continue;
    SpacePos := Pos(' ', Line);
    if SpacePos <= 1 then
      Continue;
    Candidate := Trim(Copy(Line, SpacePos + 1, Length(Line)));
    { Some tools prefix the name with '*' for binary mode. }
    if (Length(Candidate) > 0) and (Candidate[1] = '*') then
      Candidate := Copy(Candidate, 2, Length(Candidate));
    if CompareText(Candidate, FileName) = 0 then
    begin
      Hash := Uppercase(Trim(Copy(Line, 1, SpacePos - 1)));
      Result := Length(Hash) = 64;
      Exit;
    end;
  end;
end;

function DownloadAndVerifyRelease: Boolean;
var
  ArchivePath: String;
  ChecksumsPath: String;
  ExpectedHash: String;
  ActualHash: String;
  ExtractDir: String;
begin
  Result := False;

  DownloadPage.Clear;
  DownloadPage.Add(ReleaseBaseUrl + 'checksums.txt', 'checksums.txt', '');
  DownloadPage.Add(ReleaseBaseUrl + '{#ReleaseAssetName}', '{#ReleaseAssetName}', '');
  DownloadPage.Show;

  try
    try
      DownloadPage.Download;
    except
      { Requirement: never leave a half-installed program behind when the
        network fails. Nothing has been written yet at this point. }
      MsgBox(CustomMessage('ErrorDownloadFailed') + #13#10#13#10 + GetExceptionMessage,
             mbCriticalError, MB_OK);
      Exit;
    end;

    ChecksumsPath := ExpandConstant('{tmp}\checksums.txt');
    ArchivePath := ExpandConstant('{tmp}\{#ReleaseAssetName}');

    if not ExpectedHashFromChecksums(ChecksumsPath, '{#ReleaseAssetName}', ExpectedHash) then
    begin
      MsgBox(CustomMessage('ErrorChecksumMissing'), mbCriticalError, MB_OK);
      Exit;
    end;

    ActualHash := Uppercase(GetSHA256OfFile(ArchivePath));
    if ActualHash <> ExpectedHash then
    begin
      Log('SHA-256 mismatch: expected ' + ExpectedHash + ', got ' + ActualHash);
      MsgBox(CustomMessage('ErrorChecksumMismatch'), mbCriticalError, MB_OK);
      Exit;
    end;
    Log('SHA-256 verified for {#ReleaseAssetName}.');

    ExtractDir := ExpandConstant('{tmp}\extracted');
    if not ForceDirectories(ExtractDir) then
    begin
      MsgBox(CustomMessage('ErrorExtractFailed'), mbCriticalError, MB_OK);
      Exit;
    end;

    try
      { Signature: ExtractArchive(ArchiveFilename, DestDir, Password,
        FullPaths, OnExtractionProgress). The archive is never encrypted. }
      ExtractArchive(ArchivePath, ExtractDir, '', True, nil);
    except
      MsgBox(CustomMessage('ErrorExtractFailed') + #13#10#13#10 + GetExceptionMessage,
             mbCriticalError, MB_OK);
      Exit;
    end;

    DownloadedExePath := ExtractDir + '\{#AppExeName}';
    if not FileExists(DownloadedExePath) then
    begin
      Log('Archive did not contain {#AppExeName}.');
      MsgBox(CustomMessage('ErrorExtractFailed'), mbCriticalError, MB_OK);
      Exit;
    end;

    Result := True;
  finally
    DownloadPage.Hide;
  end;
end;

{ ------------------------------------------------------------------------- }
{ Wizard                                                                     }
{ ------------------------------------------------------------------------- }

procedure InitializeWizard;
begin
  UsingDownload := False;

  SourcePage := CreateInputOptionPage(wpSelectTasks,
    CustomMessage('SourcePageCaption'),
    CustomMessage('SourcePageDescription'),
    CustomMessage('SourcePageSubCaption'),
    True, False);
  SourcePage.Add(CustomMessage('SourceBundled'));
  SourcePage.Add(CustomMessage('SourceDownload'));
  SourcePage.SelectedValueIndex := 0;

  DownloadPage := CreateDownloadPage(
    SetupMessage(msgWizardPreparing),
    CustomMessage('DownloadPageDescription'),
    nil);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = SourcePage.ID then
    UsingDownload := SourcePage.SelectedValueIndex = 1;

  { Fetch and verify before the Ready page, so a failure can be reported while
    nothing has been installed yet. }
  if (CurPageID = wpReady) and UsingDownload and (DownloadedExePath = '') then
    Result := DownloadAndVerifyRelease;
end;

function UpdateReadyMemo(const Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  Summary: String;
begin
  Summary := CustomMessage('ReadyScope') + NewLine + Space;
  if IsMachineWide then
    Summary := Summary + CustomMessage('ScopeAllUsers')
  else
    Summary := Summary + CustomMessage('ScopeCurrentUser');
  Summary := Summary + NewLine + NewLine;

  Summary := Summary + MemoDirInfo + NewLine + NewLine;

  Summary := Summary + CustomMessage('ReadySource') + NewLine + Space;
  if UsingDownload then
    Summary := Summary + CustomMessage('SourceDownload')
  else
    Summary := Summary + CustomMessage('SourceBundled');
  Summary := Summary + NewLine + NewLine;

  if MemoTasksInfo <> '' then
    Summary := Summary + MemoTasksInfo + NewLine;

  Result := Summary;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    RemoveStaleSurlPathEntries(ExpandConstant('{app}'));
    if WizardIsTaskSelected('addtopath') then
      AddDirectoryToPath(ExpandConstant('{app}'));
  end;
end;

procedure DeinitializeSetup;
var
  ExtractDir: String;
begin
  // Temporary files are cleaned up whether the install succeeded or failed.
  // Inno removes the temp directory itself; the extracted tree is ours.
  // (Note: braces cannot appear inside a Pascal comment, hence // here.)
  ExtractDir := ExpandConstant('{tmp}\extracted');
  if DirExists(ExtractDir) then
    DelTree(ExtractDir, True, True, True);
end;

{ ------------------------------------------------------------------------- }
{ Uninstall                                                                  }
{ ------------------------------------------------------------------------- }

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ConfigDir: String;
  CacheDir: String;
  Response: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveDirectoryFromPath(ExpandConstant('{app}'));
  end;

  if CurUninstallStep = usPostUninstall then
  begin
    ConfigDir := ExpandConstant('{userappdata}\SURL');
    CacheDir := ExpandConstant('{localappdata}\SURL');

    if DirExists(ConfigDir) or DirExists(CacheDir) then
    begin
      { Default to keeping user data: deleting someone's configuration and
        cache without asking is never the right surprise. Mirrored websites
        live wherever the user chose and are never touched at all. }
      Response := MsgBox(CustomMessage('UninstallRemoveDataPrompt'),
                         mbConfirmation, MB_YESNO or MB_DEFBUTTON2);
      if Response = IDYES then
      begin
        if DirExists(ConfigDir) then
          DelTree(ConfigDir, True, True, True);
        if DirExists(CacheDir) then
          DelTree(CacheDir, True, True, True);
      end;
    end;
  end;
end;
