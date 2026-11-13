; eMule Qt -- Windows installer (Inno Setup 6.3+)
;
; Built by scripts\installer-win.ps1 from the tree bundle-win.ps1 staged:
;   ISCC /DAppVersion=0.6.12 /DStageDir=<repo>\stage\eMule /O<repo>\build emuleqt.iss
;
; Two wizard pages: destination folder, then two checkboxes (desktop shortcut,
; Start Menu shortcut). Importing from eMule / aMule is the first page of the
; app's own first start wizard (src/core/prefs/LegacyConfigImport): it runs as the
; user, finds the installs the same way and can import downloads too.
;
; Setup also leaves a copy of itself in {app}\setup -- not in {app}\config,
; which is seeded into every user's config dir. The daemon copies it into the
; user's Incoming dir on its next start, so it gets shared (src/core/app/InstallerCopy).
;
; Saved as UTF-8 with BOM: the translated [CustomMessages] need it.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\stage\eMule"
#endif

#define AppName "eMule Qt"
#define AppPublisher "eMuleQt team"
#define AppCopyright "Copyright (C) eMuleQt team. GPL-2.0"
#define AppExe "emuleqt.exe"
; Name of the shared copy; InstallerCopy looks for eMuleQt-v*-win64-setup.exe
#define SetupCopyName "eMuleQt-v" + AppVersion + "-win64-setup.exe"
; Never change: identifies the install for upgrades and the uninstaller
#define AppGuid "CB5B0652-B8AA-5EA7-952F-DDB1DFF59929"

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
; Publisher + version info: AV heuristics score anonymous setups higher (#179).
; Keep in sync with resources/versioninfo.rc
AppPublisher={#AppPublisher}
AppPublisherURL=https://emule-qt.org
AppSupportURL=https://emule-qt.org
AppUpdatesURL=https://emule-qt.org
AppCopyright={#AppCopyright}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoCopyright={#AppCopyright}
VersionInfoDescription={#AppName} Setup
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile=..\..\resources\icons\MuleApp.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
; As few pages as possible
DisableWelcomePage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
; GUI and daemon hold their own exe and the Qt DLLs open
CloseApplications=force
RestartApplications=no
; The GUI starts no core while this exists (main.cpp installerRunning): it would
; start the one StopAppCores just stopped again (#157). Global: GUIs of other sessions
SetupMutex=eMuleQtSetupRunning,Global\eMuleQtSetupRunning
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=eMuleQt-v{#AppVersion}-win64-setup

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
; Not in every Inno Setup distribution
#define HaveKorean FileExists(AddBackslash(CompilerPath) + "Languages\Korean.isl")
#define HaveChinese FileExists(AddBackslash(CompilerPath) + "Languages\ChineseSimplified.isl")
#define HaveChineseTW FileExists(AddBackslash(CompilerPath) + "Languages\ChineseTraditional.isl")
#if HaveKorean
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
#endif
#if HaveChinese
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif
#if HaveChineseTW
Name: "zhtw"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
#endif

[CustomMessages]
StartMenuShortcut=Create a &Start Menu shortcut
de.StartMenuShortcut=&Startmenü-Verknüpfung erstellen
es.StartMenuShortcut=Crear un acceso directo en el menú &Inicio
fr.StartMenuShortcut=Créer un raccourci dans le menu &Démarrer
it.StartMenuShortcut=Crea un collegamento nel menu &Start
ja.StartMenuShortcut=スタート メニューにショートカットを作成する(&S)
nl.StartMenuShortcut=Een snelkoppeling in het &Startmenu maken
pl.StartMenuShortcut=Utwórz skrót w menu &Start
ptbr.StartMenuShortcut=Criar um atalho no Menu &Iniciar
ru.StartMenuShortcut=Создать ярлык в меню «&Пуск»
#if HaveKorean
ko.StartMenuShortcut=시작 메뉴에 바로 가기 만들기(&S)
#endif
#if HaveChinese
zh.StartMenuShortcut=创建开始菜单快捷方式(&S)
#endif
#if HaveChineseTW
zhtw.StartMenuShortcut=建立開始功能表捷徑(&S)
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"
Name: "startmenuicon"; Description: "{cm:StartMenuShortcut}"

[InstallDelete]
; Import request older setups left; the first start wizard asks now
Type: files; Name: "{app}\config\mfc-import.yml"
; Setup copy of the version this one replaces
Type: filesandordirs; Name: "{app}\setup"
; OS DLLs older bundles shipped; app-dir copy shadows System32 (icuuc.dll -> missing icu.dll)
Type: files; Name: "{app}\icu.dll"
Type: files; Name: "{app}\icuuc.dll"
Type: files; Name: "{app}\icuin.dll"
Type: files; Name: "{app}\d3dcompiler_47.dll"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Switches the app to the per-user config dir; never seeded into it (AppConfig kNeverSeed)
Source: "preferences.yml"; DestDir: "{app}\config"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: startmenuicon
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; .nzb, .torrent and magnet: associations the GUI registered under HKCU
Filename: "{app}\{#AppExe}"; Parameters: "--unregister-file-types"; Flags: runhidden; RunOnceId: "UnregisterFileTypes"

[UninstallDelete]
Type: files; Name: "{app}\config\mfc-import.yml"
; Copied by [Code], so not in the uninstall log
Type: filesandordirs; Name: "{app}\setup"

[Code]
// --- Stopping a running core (#157) ---
// Restart Manager can't close emulecored: started detached, it has no console
// and only a hidden window, so it kept archive.dll & co. open and setup failed
// with "DeleteFile failed; code 5". WM_CLOSE to that window is the GUI's own
// stop (daemon WinShutdownHandler::kWindowClass, GUI main.cpp stopCoreProcess):
// downloads and settings get saved. A core that won't stop is killed.

const
  CoreWindowClass = 'eMuleQtCoreShutdownWindow';
  CoreExe = 'emulecored.exe';
  CoreWmClose = $0010;
  PROCESS_QUERY_LIMITED_INFORMATION = $1000;
  // Saving many part files takes a while; 120 x 250 ms
  CoreStopPolls = 120;

function FindWindowExW(Parent, ChildAfter: HWND; ClassName: String; WindowName: Longint): HWND;
  external 'FindWindowExW@user32.dll stdcall';
function GetWindowThreadProcessId(Wnd: HWND; var ProcessId: Cardinal): Cardinal;
  external 'GetWindowThreadProcessId@user32.dll stdcall';
// HWND: the pointer-sized type Pascal Script surely knows, also for the process handle
function OpenProcess(Access: Cardinal; Inherit: Boolean; ProcessId: Cardinal): HWND;
  external 'OpenProcess@kernel32.dll stdcall';
function QueryFullProcessImageNameW(Process: HWND; Flags: Cardinal; ExeName: String; var Size: Cardinal): Boolean;
  external 'QueryFullProcessImageNameW@kernel32.dll stdcall';
function CloseHandle(Handle: HWND): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

// Only cores of this install: a portable copy elsewhere keeps running
function IsAppCoreWindow(Wnd: HWND): Boolean;
var
  Pid, Size: Cardinal;
  Process: HWND;
  Path: String;
begin
  Result := False;
  Pid := 0;
  GetWindowThreadProcessId(Wnd, Pid);
  if Pid = 0 then
    Exit;
  Process := OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, Pid);
  if Process = 0 then
    Exit;
  Size := 1024;
  SetLength(Path, Size);
  if QueryFullProcessImageNameW(Process, 0, Path, Size) then
  begin
    Path := Lowercase(Copy(Path, 1, Size));
    Result := Pos(Lowercase(AddBackslash(ExpandConstant('{app}'))), Path) = 1;
  end;
  CloseHandle(Process);
end;

// Asks every core of this install to stop; False when none runs
function CloseAppCores(Post: Boolean): Boolean;
var
  Wnd: HWND;
begin
  Result := False;
  Wnd := FindWindowExW(0, 0, CoreWindowClass, 0);
  while Wnd <> 0 do
  begin
    if IsAppCoreWindow(Wnd) then
    begin
      Result := True;
      if Post then
        PostMessage(Wnd, CoreWmClose, 0, 0);
    end;
    Wnd := FindWindowExW(0, Wnd, CoreWindowClass, 0);
  end;
end;

// Counts cores of this install in any session, ends them when Kill. In-process
// WMI: the hidden powershell.exe Stop-Process it replaces looked like malware
// to AV heuristics (#179)
function AppCoresWmi(Kill: Boolean): Integer;
var
  Locator, Service, Procs, Proc, ExePath: Variant;
  App, PathStr: String;
  I: Integer;
begin
  Result := 0;
  App := Lowercase(AddBackslash(ExpandConstant('{app}')));
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT ProcessId, ExecutablePath FROM Win32_Process WHERE Name = ''' +
                               CoreExe + '''');
    for I := 0 to Procs.Count - 1 do
    begin
      Proc := Procs.ItemIndex(I);
      // Null for processes we may not query
      ExePath := Proc.ExecutablePath;
      if VarIsNull(ExePath) or VarIsEmpty(ExePath) then
        Continue;
      PathStr := ExePath;
      if Pos(App, Lowercase(PathStr)) <> 1 then
        Continue;
      Result := Result + 1;
      if Kill then
        Proc.Terminate(1);
    end;
  except
    Log('WMI query for ' + CoreExe + ' failed: ' + GetExceptionMessage);
  end;
end;

// Cores of other sessions and ones that didn't stop in time
procedure KillAppCores;
var
  I: Integer;
begin
  if AppCoresWmi(True) = 0 then
    Exit;
  Log('Ended ' + CoreExe + ' that did not stop');
  // Terminate returns before the process lets go of its files; 40 x 250 ms
  I := 0;
  while (I < 40) and (AppCoresWmi(False) > 0) do
  begin
    Sleep(250);
    I := I + 1;
  end;
end;

procedure StopAppCores;
var
  I: Integer;
begin
  if not DirExists(ExpandConstant('{app}')) then
    Exit;
  if CloseAppCores(True) then
  begin
    Log('Asked running ' + CoreExe + ' to stop');
    I := 0;
    // Asked again each time: a core started meanwhile (GUI of an older version) too
    while (I < CoreStopPolls) and CloseAppCores(True) do
    begin
      Sleep(250);
      I := I + 1;
    end;
    if I >= CoreStopPolls then
      Log(CoreExe + ' did not stop in time');
  end;
  KillAppCores;
  // Window gone before the process lets go of its DLLs
  Sleep(500);
end;

// Runs before Setup checks for files in use
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopAppCores;
  Result := '';
end;

function InitializeUninstall: Boolean;
begin
  // SetupMutex is setup's only; held until the uninstaller exits
  CreateMutex('eMuleQtSetupRunning');
  CreateMutex('Global\eMuleQtSetupRunning');
  StopAppCores;
  Result := True;
end;

// Canonical name, even when the download was renamed. A failed copy only
// means nothing gets shared -- the install itself is fine.
procedure StageSetupCopy;
var
  Dir: String;
begin
  Dir := ExpandConstant('{app}\setup');
  if not ForceDirectories(Dir) then
  begin
    Log('Could not create ' + Dir);
    Exit;
  end;
  if not FileCopy(ExpandConstant('{srcexe}'), AddBackslash(Dir) + '{#SetupCopyName}', False) then
    Log('Could not copy setup to ' + Dir);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    StageSetupCopy;
  end;
end;

function PointsIntoApp(const Value: String): Boolean;
begin
  Result := Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Value)) > 0;
end;

// What the GUI registered for itself under HKCU. Settings and downloads in
// %LOCALAPPDATA%\eMuleQt are the user's and stay.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  if RegQueryStringValue(HKCU, 'Software\Classes\ed2k\shell\open\command', '', Value)
     and PointsIntoApp(Value) then
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\ed2k');
  // --unregister-file-types ran before ([UninstallRun]); this catches a copy of
  // magnet: written by another install path of ours
  if RegQueryStringValue(HKCU, 'Software\Classes\magnet\shell\open\command', '', Value)
     and PointsIntoApp(Value) then
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\magnet');
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'eMuleQt', Value)
     and PointsIntoApp(Value) then
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'eMuleQt');
end;
