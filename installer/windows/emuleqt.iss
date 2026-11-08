; eMule Qt -- Windows installer (Inno Setup 6.3+)
;
; Built by scripts\installer-win.ps1 from the tree bundle-win.ps1 staged:
;   ISCC /DAppVersion=0.6.12 /DStageDir=<repo>\stage\eMule /O<repo>\build emuleqt.iss
;
; Two wizard pages: destination folder, then three checkboxes (desktop shortcut,
; Start Menu shortcut, import from eMule). The import itself is not done here:
; setup runs elevated and must not write a user's profile, so it only leaves a
; request file that the daemon carries out on its next start
; (src/core/prefs/LegacyConfigImport).
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
AppPublisher={#AppName}
AppPublisherURL=https://emule-qt.org
AppSupportURL=https://emule-qt.org
VersionInfoVersion={#AppVersion}
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
ImportEmule=&Import settings from eMule
ImportEmuleNotFound=(no eMule installation found)
de.StartMenuShortcut=&Startmenü-Verknüpfung erstellen
de.ImportEmule=Einstellungen aus eMule &importieren
de.ImportEmuleNotFound=(keine eMule-Installation gefunden)
es.StartMenuShortcut=Crear un acceso directo en el menú &Inicio
es.ImportEmule=I&mportar la configuración de eMule
es.ImportEmuleNotFound=(no se encontró ninguna instalación de eMule)
fr.StartMenuShortcut=Créer un raccourci dans le menu &Démarrer
fr.ImportEmule=&Importer les paramètres d'eMule
fr.ImportEmuleNotFound=(aucune installation d'eMule trouvée)
it.StartMenuShortcut=Crea un collegamento nel menu &Start
it.ImportEmule=&Importa le impostazioni da eMule
it.ImportEmuleNotFound=(nessuna installazione di eMule trovata)
ja.StartMenuShortcut=スタート メニューにショートカットを作成する(&S)
ja.ImportEmule=eMule の設定をインポートする(&I)
ja.ImportEmuleNotFound=(eMule のインストールが見つかりません)
nl.StartMenuShortcut=Een snelkoppeling in het &Startmenu maken
nl.ImportEmule=Instellingen uit eMule &importeren
nl.ImportEmuleNotFound=(geen eMule-installatie gevonden)
pl.StartMenuShortcut=Utwórz skrót w menu &Start
pl.ImportEmule=&Importuj ustawienia z eMule
pl.ImportEmuleNotFound=(nie znaleziono instalacji eMule)
ptbr.StartMenuShortcut=Criar um atalho no Menu &Iniciar
ptbr.ImportEmule=I&mportar as configurações do eMule
ptbr.ImportEmuleNotFound=(nenhuma instalação do eMule encontrada)
ru.StartMenuShortcut=Создать ярлык в меню «&Пуск»
ru.ImportEmule=&Импортировать настройки из eMule
ru.ImportEmuleNotFound=(установка eMule не найдена)
#if HaveKorean
ko.StartMenuShortcut=시작 메뉴에 바로 가기 만들기(&S)
ko.ImportEmule=eMule 설정 가져오기(&I)
ko.ImportEmuleNotFound=(eMule 설치를 찾을 수 없음)
#endif
#if HaveChinese
zh.StartMenuShortcut=创建开始菜单快捷方式(&S)
zh.ImportEmule=从 eMule 导入设置(&I)
zh.ImportEmuleNotFound=(未找到 eMule 安装)
#endif
#if HaveChineseTW
zhtw.StartMenuShortcut=建立開始功能表捷徑(&S)
zhtw.ImportEmule=從 eMule 匯入設定(&I)
zhtw.ImportEmuleNotFound=(找不到 eMule 安裝)
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"
Name: "startmenuicon"; Description: "{cm:StartMenuShortcut}"
; Unchecked here so a silent install never imports unasked; the wizard ticks it
; when an eMule config was found (SetupImportTask)
Name: "importemule"; Description: "{cm:ImportEmule}"; Flags: unchecked

[InstallDelete]
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
const
  ImportRequestFile = 'mfc-import.yml';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1';

var
  // Config dir of an existing eMule, '' when none was found
  EmuleConfigDir: String;
  // The same as written to the request: %LOCALAPPDATA% / %APPDATA% stay
  // unexpanded, so the path resolves for the user who later runs the daemon
  // even when UAC was answered with another account
  EmuleConfigRequest: String;
  ImportTaskInitialized: Boolean;

function HasPrefsIni(const Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + 'preferences.ini');
end;

procedure TryConfigDir(const Dir, RequestForm: String);
begin
  if (EmuleConfigDir = '') and HasPrefsIni(Dir) then
  begin
    EmuleConfigDir := RemoveBackslashUnlessRoot(Dir);
    EmuleConfigRequest := RequestForm;
    if EmuleConfigRequest = '' then
      EmuleConfigRequest := EmuleConfigDir;
  end;
end;

// Folder of emule.exe out of a registry command line: "<exe>" "%1", a bare
// path, or the unquoted "<exe> -AutoStart" of the Run key. '' unless the
// program really is emule.exe -- other clients register ed2k:// too.
function EmuleExeDir(Command: String): String;
var
  P: Integer;
begin
  Result := '';
  P := Pos('.exe', Lowercase(Command));
  if P = 0 then
    Exit;
  Command := Copy(Command, 1, P + 3);
  if (Length(Command) > 0) and (Command[1] = '"') then
    Delete(Command, 1, 1);
  if CompareText(ExtractFileName(Command), 'emule.exe') = 0 then
    Result := ExtractFilePath(Command);
end;

procedure TryExeDir(const ExeDir: String);
begin
  if ExeDir <> '' then
    TryConfigDir(AddBackslash(ExeDir) + 'config', '');
end;

procedure TryExeDirFromRegistry(const SubKey, ValueName: String);
var
  Command: String;
begin
  if RegQueryStringValue(HKCU, SubKey, ValueName, Command) then
    TryExeDir(EmuleExeDir(Command));
end;

// Program-directory mode: eMule records no install path, only its own exe in
// the handlers it registers.
procedure TryProgramDirs;
begin
  TryExeDirFromRegistry('Software\Classes\ed2k\shell\open\command', '');
  TryExeDirFromRegistry('Software\Classes\eMule\shell\open\command', '');
  TryExeDirFromRegistry('Software\Microsoft\Windows\CurrentVersion\Run', 'eMuleAutoStart');
  TryExeDir(ExpandConstant('{commonpf32}\eMule'));
  TryExeDir(ExpandConstant('{commonpf64}\eMule'));
end;

// Same order as eMule's CPreferences::GetDefaultDirectory: the mode stored in
// the registry when there is one, else the first preferences.ini found in the
// per-user, the all-users and the program directory.
procedure DetectEmuleConfig;
var
  Mode: Cardinal;
  LocalDir, CommonDir: String;
begin
  EmuleConfigDir := '';
  EmuleConfigRequest := '';
  LocalDir := ExpandConstant('{localappdata}\eMule\config');
  CommonDir := ExpandConstant('{commonappdata}\eMule\config');

  if RegQueryDWordValue(HKCU, 'Software\eMule', 'UsePublicUserDirectories', Mode) then
  begin
    case Mode of
      0: TryConfigDir(LocalDir, '%LOCALAPPDATA%\eMule\config');
      1: TryConfigDir(CommonDir, '');
      2: TryProgramDirs;
    end;
  end;

  TryConfigDir(LocalDir, '%LOCALAPPDATA%\eMule\config');
  TryConfigDir(CommonDir, '');
  TryProgramDirs;
  // Windows XP layout, left behind by an old install
  TryConfigDir(ExpandConstant('{userappdata}\eMule\config'), '%APPDATA%\eMule\config');
end;

function IsUpgrade: Boolean;
begin
  Result := RegKeyExists(HKLM, UninstallKey);
end;

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

// Cores of other sessions and ones that didn't stop in time
procedure KillAppCores;
var
  App, Cmd: String;
  ResultCode: Integer;
begin
  App := AddBackslash(ExpandConstant('{app}'));
  StringChangeEx(App, #39, #39#39, True);
  // no line may start with #: ISPP reads it as a directive
  Cmd := '-NoProfile -NonInteractive -Command "Get-Process ' + ChangeFileExt(CoreExe, '') +
    ' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith(' + #39 +
    App + #39 + ', [StringComparison]::OrdinalIgnoreCase) } | ' +
    'Stop-Process -Force -PassThru | Wait-Process -Timeout 10"';
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Cmd, '',
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Log('Could not run PowerShell to end ' + CoreExe);
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

function InitializeSetup: Boolean;
begin
  DetectEmuleConfig;
  if EmuleConfigDir <> '' then
    Log('eMule config found: ' + EmuleConfigDir)
  else
    Log('No eMule config found');
  Result := True;
end;

// Found: enabled, path shown, ticked by default (not on an upgrade -- that
// profile is in use already). Not found: greyed out.
procedure SetupImportTask;
var
  I, Index: Integer;
  Title: String;
begin
  Title := CustomMessage('ImportEmule');
  Index := -1;
  for I := 0 to WizardForm.TasksList.Items.Count - 1 do
    if Pos(Title, WizardForm.TasksList.ItemCaption[I]) = 1 then
      Index := I;
  if Index < 0 then
    Exit;

  if EmuleConfigDir = '' then
  begin
    WizardForm.TasksList.Checked[Index] := False;
    WizardForm.TasksList.ItemEnabled[Index] := False;
    WizardForm.TasksList.ItemCaption[Index] := Title + ' ' + CustomMessage('ImportEmuleNotFound');
  end
  else
  begin
    WizardForm.TasksList.ItemCaption[Index] := Title + ' (' + EmuleConfigDir + ')';
    // Silent: the page is still passed, and /TASKS alone decides
    if not ImportTaskInitialized and not WizardSilent then
      WizardForm.TasksList.Checked[Index] := not IsUpgrade;
  end;
  ImportTaskInitialized := True;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectTasks then
    SetupImportTask;
end;

// YAML single-quoted scalar: backslashes stay literal, a quote is doubled
function YamlQuote(S: String): String;
begin
  StringChangeEx(S, #39, #39#39, True);
  Result := #39 + S + #39;
end;

procedure WriteImportRequest;
var
  Path: String;
  Lines: TArrayOfString;
begin
  Path := ExpandConstant('{app}\config\') + ImportRequestFile;
  if WizardIsTaskSelected('importemule') and (EmuleConfigDir <> '') then
  begin
    SetArrayLength(Lines, 2);
    Lines[0] := '# Written by setup: import this eMule config on the next start, once per user.';
    Lines[1] := 'source: ' + YamlQuote(EmuleConfigRequest);
    if not SaveStringsToUTF8File(Path, Lines, False) then
      Log('Could not write ' + Path);
  end
  else
    // A request of an earlier setup run the user has now declined
    DeleteFile(Path);
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
    WriteImportRequest;
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
