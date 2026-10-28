#!/usr/bin/env python3
"""Generate per-test Visual Studio vcxproj files and update the solution.

No args: rewrites every tests/vs project and the whole src/eMuleQt.sln.
--only tst_A tst_B: writes just those projects and adds them to the .sln in place.
"""

import argparse
import os
from dataclasses import dataclass, replace
import uuid
import glob
import textwrap

PROJECT_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
TESTS_DIR = os.path.join(PROJECT_ROOT, "tests")
VS_DIR = os.path.join(TESTS_DIR, "vs")
SLN_PATH = os.path.join(PROJECT_ROOT, "src", "eMuleQt.sln")

# Namespace UUID for deterministic GUID generation
NAMESPACE = uuid.UUID("6ba7b810-9dad-11d1-80b4-00c04fd430c8")  # URL namespace

# Known project GUIDs from the solution
EMULECORE_GUID = "{AB94E5C4-E1F3-31B4-BA89-325740E6BE5B}"
EMULEIPC_GUID = "{FBDFD081-D65F-39D9-8A1C-72045318CC5B}"
EMULEUSENET_GUID = "{1BDD4987-DC42-5349-9E57-C6E045319FE7}"
EMULEINDEXER_GUID = "{3F2A6C41-9D18-4B77-A5E4-6B0C1D2E8A73}"
EMULEBT_GUID = "{EFB1FFAF-37F7-57F7-B433-11426F819533}"
EMULEED3K_GUID = "{1C170FEE-EBD2-54F0-9048-AA6C9F7FE73A}"
VS_CPP_PROJECT_TYPE = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
VS_FOLDER_TYPE = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}"
TESTS_FOLDER_GUID = "{E0A0B5F0-0001-0001-0001-000000000001}"

# Old test project to remove
OLD_TEST_GUID = "{2C8CBF44-CFAF-3A30-AA4B-1B826A227B41}"


def make_guid(name: str) -> str:
    return "{" + str(uuid.uuid5(NAMESPACE, f"emuleqt-test-{name}")).upper() + "}"


VCXPROJ_TEMPLATE = textwrap.dedent("""\
    <?xml version="1.0" encoding="utf-8"?>
    <Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
      <ItemGroup Label="ProjectConfigurations">
        <ProjectConfiguration Include="Release|x64">
          <Configuration>Release</Configuration>
          <Platform>x64</Platform>
        </ProjectConfiguration>
        <ProjectConfiguration Include="Debug|x64">
          <Configuration>Debug</Configuration>
          <Platform>x64</Platform>
        </ProjectConfiguration>
      </ItemGroup>
      <PropertyGroup Label="Globals">
        <ProjectGuid>{guid}</ProjectGuid>
        <RootNamespace>{name}</RootNamespace>
        <Keyword>QtVS_v304</Keyword>
        <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>
        <WindowsTargetPlatformMinVersion>10.0.26100.0</WindowsTargetPlatformMinVersion>
        <QtMsBuild Condition="'$(QtMsBuild)'=='' OR !Exists('$(QtMsBuild)\\qt.targets')">$(MSBuildProjectDirectory)\\QtMsBuild</QtMsBuild>
      </PropertyGroup>
      <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
      <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
        <PlatformToolset>v145</PlatformToolset>
        <OutputDirectory>release\\</OutputDirectory>
        <ATLMinimizesCRunTimeLibraryUsage>false</ATLMinimizesCRunTimeLibraryUsage>
        <CharacterSet>NotSet</CharacterSet>
        <ConfigurationType>Application</ConfigurationType>
        <IntermediateDirectory>release\\</IntermediateDirectory>
        <PrimaryOutput>{name}</PrimaryOutput>
      </PropertyGroup>
      <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
        <PlatformToolset>v145</PlatformToolset>
        <OutputDirectory>debug\\</OutputDirectory>
        <ATLMinimizesCRunTimeLibraryUsage>false</ATLMinimizesCRunTimeLibraryUsage>
        <CharacterSet>NotSet</CharacterSet>
        <ConfigurationType>Application</ConfigurationType>
        <IntermediateDirectory>debug\\</IntermediateDirectory>
        <PrimaryOutput>{name}</PrimaryOutput>
      </PropertyGroup>
      <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
      <Import Project="$(QtMsBuild)\\qt_defaults.props" Condition="Exists('$(QtMsBuild)\\qt_defaults.props')" />
      <Import Project="..\\..\\QtVersion.props" />
      <PropertyGroup Label="QtSettings" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
        <QtModules>{qt_modules}</QtModules>
      </PropertyGroup>
      <PropertyGroup Label="QtSettings" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
        <QtModules>{qt_modules}</QtModules>
      </PropertyGroup>
      <Target Name="QtMsBuildNotFound" BeforeTargets="CustomBuild;ClCompile" Condition="!Exists('$(QtMsBuild)\\qt.targets') OR !Exists('$(QtMsBuild)\\Qt.props')">
        <Message Importance="High" Text="QtMsBuild: could not locate qt.targets, qt.props; project may not build correctly." />
      </Target>
      <ImportGroup Label="ExtensionSettings" />
      <ImportGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="PropertySheets">
        <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" />
        <Import Project="$(QtMsBuild)\\Qt.props" />
      </ImportGroup>
      <ImportGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="PropertySheets">
        <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" />
        <Import Project="$(QtMsBuild)\\Qt.props" />
      </ImportGroup>
      <PropertyGroup Label="UserMacros" />
      <PropertyGroup>
        <OutDir Condition="'$(Configuration)|$(Platform)'=='Release|x64'">release\\</OutDir>
        <IntDir Condition="'$(Configuration)|$(Platform)'=='Release|x64'">release\\</IntDir>
        <TargetName Condition="'$(Configuration)|$(Platform)'=='Release|x64'">{name}</TargetName>
        <IgnoreImportLibrary Condition="'$(Configuration)|$(Platform)'=='Release|x64'">true</IgnoreImportLibrary>
        <LinkIncremental Condition="'$(Configuration)|$(Platform)'=='Release|x64'">false</LinkIncremental>
        <OutDir Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">debug\\</OutDir>
        <IntDir Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">debug\\</IntDir>
        <TargetName Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">{name}</TargetName>
        <IgnoreImportLibrary Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">true</IgnoreImportLibrary>
      </PropertyGroup>
      <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
        <ClCompile>
          <AdditionalIncludeDirectories>{includes}..;..\\..\\src\\core;..\\..\\src\\ipc;..\\..\\src\\usenet;..\\..\\src\\indexer;..\\..\\src\\vcpkg_installed\\x64-windows\\include;..\\..\\src\\vcpkg_installed\\x64-windows\\include\\miniupnpc;release;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
          <AdditionalOptions>-Zc:rvalueCast -Zc:inline -Zc:strictStrings -Zc:throwingNew -permissive- -Zc:__cplusplus -Zc:externConstexpr -utf-8 -w34100 -w34189 -w44996 -w44456 -w44457 -w44458 %(AdditionalOptions)</AdditionalOptions>
          <AssemblerListingLocation>release\\</AssemblerListingLocation>
          <BrowseInformation>false</BrowseInformation>
          <DebugInformationFormat>None</DebugInformationFormat>
          <DisableSpecificWarnings>4577;4467;%(DisableSpecificWarnings)</DisableSpecificWarnings>
          <ExceptionHandling>Sync</ExceptionHandling>
          <LanguageStandard>stdcpplatest</LanguageStandard>
          <ObjectFileName>release\\</ObjectFileName>
          <Optimization>MaxSpeed</Optimization>
          <PreprocessorDefinitions>{defines}$(EmuleQuicDefines)_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;NDEBUG;QT_NO_DEBUG;EMULE_TEST_DATA_DIR=../../tests/data/;EMULE_PROJECT_DATA_DIR=../../data/;%(PreprocessorDefinitions)</PreprocessorDefinitions>
          <PreprocessToFile>false</PreprocessToFile>
          <ProgramDataBaseFileName>
          </ProgramDataBaseFileName>
          <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
          <SuppressStartupBanner>true</SuppressStartupBanner>
          <TreatWChar_tAsBuiltInType>true</TreatWChar_tAsBuiltInType>
          <UseFullPaths>false</UseFullPaths>
          <WarningLevel>Level3</WarningLevel>
          <MultiProcessorCompilation>true</MultiProcessorCompilation>
        </ClCompile>
        <Link>
          <AdditionalDependencies>{libs}emuleusenet.lib;emuleindexer.lib;emulecore.lib;emuleipc.lib;$(EmuleQuicLibs)libssl.lib;libcrypto.lib;z.lib;miniupnpc.lib;yaml-cpp.lib;archive.lib;maxminddb.lib;ws2_32.lib;iphlpapi.lib;$(QTDIR)\\lib\\Qt6HttpServer.lib;%(AdditionalDependencies)</AdditionalDependencies>
          <AdditionalLibraryDirectories>..\\..\\bin\\$(Configuration);..\\..\\src\\vcpkg_installed\\x64-windows\\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
          <AdditionalOptions>"/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' publicKeyToken='6595b64144ccf1df' language='*' processorArchitecture='*'" %(AdditionalOptions)</AdditionalOptions>
          <DataExecutionPrevention>true</DataExecutionPrevention>
          <EnableCOMDATFolding>true</EnableCOMDATFolding>
          <GenerateDebugInformation>false</GenerateDebugInformation>
          <IgnoreImportLibrary>true</IgnoreImportLibrary>
          <LinkIncremental>false</LinkIncremental>
          <OptimizeReferences>true</OptimizeReferences>
          <OutputFile>$(OutDir)\\{name}.exe</OutputFile>
          <RandomizedBaseAddress>true</RandomizedBaseAddress>
          <SubSystem>Console</SubSystem>
          <SuppressStartupBanner>true</SuppressStartupBanner>
        </Link>
        <Midl>
          <DefaultCharType>Unsigned</DefaultCharType>
          <EnableErrorChecks>None</EnableErrorChecks>
          <WarningLevel>0</WarningLevel>
        </Midl>
        <ResourceCompile>
          <PreprocessorDefinitions>{defines}$(EmuleQuicDefines)_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;NDEBUG;QT_NO_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
        </ResourceCompile>
        <QtMoc>
          <CompilerFlavor>msvc</CompilerFlavor>
          <Include>./$(Configuration)/moc_predefs.h</Include>
          <ExecutionDescription>Moc'ing %(Identity)...</ExecutionDescription>
          <DynamicSource>output</DynamicSource>
          <QtMocDir>$(Configuration)</QtMocDir>
          <QtMocFileName>%(Filename).moc</QtMocFileName>
        </QtMoc>
      </ItemDefinitionGroup>
      <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
        <ClCompile>
          <AdditionalIncludeDirectories>{includes}..;..\\..\\src\\core;..\\..\\src\\ipc;..\\..\\src\\usenet;..\\..\\src\\indexer;..\\..\\src\\vcpkg_installed\\x64-windows\\include;..\\..\\src\\vcpkg_installed\\x64-windows\\include\\miniupnpc;debug;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
          <AdditionalOptions>-Zc:rvalueCast -Zc:inline -Zc:strictStrings -Zc:throwingNew -permissive- -Zc:__cplusplus -Zc:externConstexpr -utf-8 -w34100 -w34189 -w44996 -w44456 -w44457 -w44458 %(AdditionalOptions)</AdditionalOptions>
          <AssemblerListingLocation>debug\\</AssemblerListingLocation>
          <BrowseInformation>false</BrowseInformation>
          <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
          <DisableSpecificWarnings>4577;4467;%(DisableSpecificWarnings)</DisableSpecificWarnings>
          <ExceptionHandling>Sync</ExceptionHandling>
          <LanguageStandard>stdcpplatest</LanguageStandard>
          <ObjectFileName>debug\\</ObjectFileName>
          <Optimization>Disabled</Optimization>
          <PreprocessorDefinitions>{defines}$(EmuleQuicDefines)_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;EMULE_TEST_DATA_DIR=../../tests/data/;EMULE_PROJECT_DATA_DIR=../../data/;_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
          <PreprocessToFile>false</PreprocessToFile>
          <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
          <SuppressStartupBanner>true</SuppressStartupBanner>
          <TreatWChar_tAsBuiltInType>true</TreatWChar_tAsBuiltInType>
          <UseFullPaths>false</UseFullPaths>
          <WarningLevel>Level3</WarningLevel>
          <MultiProcessorCompilation>true</MultiProcessorCompilation>
        </ClCompile>
        <Link>
          <AdditionalDependencies>{libs}emuleusenet.lib;emuleindexer.lib;emulecore.lib;emuleipc.lib;$(EmuleQuicLibs)libssl.lib;libcrypto.lib;zd.lib;miniupnpc.lib;yaml-cpp.lib;archive.lib;maxminddb.lib;ws2_32.lib;iphlpapi.lib;$(QTDIR)\\lib\\Qt6HttpServerd.lib;%(AdditionalDependencies)</AdditionalDependencies>
          <AdditionalLibraryDirectories>..\\..\\bin\\$(Configuration);..\\..\\src\\vcpkg_installed\\x64-windows\\debug\\lib;..\\..\\src\\vcpkg_installed\\x64-windows\\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
          <AdditionalOptions>"/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' publicKeyToken='6595b64144ccf1df' language='*' processorArchitecture='*'" %(AdditionalOptions)</AdditionalOptions>
          <DataExecutionPrevention>true</DataExecutionPrevention>
          <GenerateDebugInformation>true</GenerateDebugInformation>
          <IgnoreImportLibrary>true</IgnoreImportLibrary>
          <OutputFile>$(OutDir)\\{name}.exe</OutputFile>
          <RandomizedBaseAddress>true</RandomizedBaseAddress>
          <SubSystem>Console</SubSystem>
          <SuppressStartupBanner>true</SuppressStartupBanner>
        </Link>
        <Midl>
          <DefaultCharType>Unsigned</DefaultCharType>
          <EnableErrorChecks>None</EnableErrorChecks>
          <WarningLevel>0</WarningLevel>
        </Midl>
        <ResourceCompile>
          <PreprocessorDefinitions>{defines}$(EmuleQuicDefines)_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
        </ResourceCompile>
        <QtMoc>
          <CompilerFlavor>msvc</CompilerFlavor>
          <Include>./$(Configuration)/moc_predefs.h</Include>
          <ExecutionDescription>Moc'ing %(Identity)...</ExecutionDescription>
          <DynamicSource>output</DynamicSource>
          <QtMocDir>$(Configuration)</QtMocDir>
          <QtMocFileName>%(Filename).moc</QtMocFileName>
        </QtMoc>
      </ItemDefinitionGroup>
      <ItemGroup>
        <ClCompile Include="..\\{name}.cpp" />{sources}
      </ItemGroup>
      <ItemGroup>
        <CustomBuild Include="..\\{name}.cpp.moc.cbt">
          <FileType>Document</FileType>
          <Command Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">"$(QTDIR)\\bin\\moc.exe" "..\\{name}.cpp" -o "$(IntDir){name}.moc" --include "./$(Configuration)/moc_predefs.h" -I".." -I"../../src/core" -I"../../src/ipc" -I"$(QTDIR)/include" -I"$(QTDIR)/include/QtCore" -I"$(QTDIR)/include/QtTest" -I"$(QTDIR)/include/QtNetwork" -I"$(QTDIR)/include/QtGui"</Command>
          <Command Condition="'$(Configuration)|$(Platform)'=='Release|x64'">"$(QTDIR)\\bin\\moc.exe" "..\\{name}.cpp" -o "$(IntDir){name}.moc" --include "./$(Configuration)/moc_predefs.h" -I".." -I"../../src/core" -I"../../src/ipc" -I"$(QTDIR)/include" -I"$(QTDIR)/include/QtCore" -I"$(QTDIR)/include/QtTest" -I"$(QTDIR)/include/QtNetwork" -I"$(QTDIR)/include/QtGui"</Command>
          <Message Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">Moc'ing {name}.cpp...</Message>
          <Message Condition="'$(Configuration)|$(Platform)'=='Release|x64'">Moc'ing {name}.cpp...</Message>
          <Outputs Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">$(IntDir){name}.moc;%(Outputs)</Outputs>
          <Outputs Condition="'$(Configuration)|$(Platform)'=='Release|x64'">$(IntDir){name}.moc;%(Outputs)</Outputs>
          <AdditionalInputs Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">..\\{name}.cpp;%(AdditionalInputs)</AdditionalInputs>
          <AdditionalInputs Condition="'$(Configuration)|$(Platform)'=='Release|x64'">..\\{name}.cpp;%(AdditionalInputs)</AdditionalInputs>
        </CustomBuild>
      </ItemGroup>
      <ItemGroup>
        <CustomBuild Include="debug\\moc_predefs.h.cbt">
          <FileType>Document</FileType>
          <AdditionalInputs Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">$(QTDIR)\\mkspecs\\features\\data\\dummy.cpp;%(AdditionalInputs)</AdditionalInputs>
          <Command Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">cl -Bx"$(QTDIR)\\bin\\qmake.exe" -nologo -Zc:wchar_t -FS -Zc:rvalueCast -Zc:inline -Zc:strictStrings -Zc:throwingNew -permissive- -Zc:__cplusplus -Zc:externConstexpr -Zi -MDd -std:c++latest -utf-8 -W3 -w34100 -w34189 -w44996 -w44456 -w44457 -w44458 -wd4577 -wd4467 -E $(QTDIR)\\mkspecs\\features\\data\\dummy.cpp 2&gt;NUL &gt;$(IntDir)\\moc_predefs.h</Command>
          <Message Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">Generate moc_predefs.h</Message>
          <Outputs Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">$(IntDir)\\moc_predefs.h;%(Outputs)</Outputs>
        </CustomBuild>
        <CustomBuild Include="release\\moc_predefs.h.cbt">
          <FileType>Document</FileType>
          <AdditionalInputs Condition="'$(Configuration)|$(Platform)'=='Release|x64'">$(QTDIR)\\mkspecs\\features\\data\\dummy.cpp;%(AdditionalInputs)</AdditionalInputs>
          <Command Condition="'$(Configuration)|$(Platform)'=='Release|x64'">cl -Bx"$(QTDIR)\\bin\\qmake.exe" -nologo -Zc:wchar_t -FS -Zc:rvalueCast -Zc:inline -Zc:strictStrings -Zc:throwingNew -permissive- -Zc:__cplusplus -Zc:externConstexpr -O2 -MD -std:c++latest -utf-8 -W3 -w34100 -w34189 -w44996 -w44456 -w44457 -w44458 -wd4577 -wd4467 -E $(QTDIR)\\mkspecs\\features\\data\\dummy.cpp 2&gt;NUL &gt;$(IntDir)\\moc_predefs.h</Command>
          <Message Condition="'$(Configuration)|$(Platform)'=='Release|x64'">Generate moc_predefs.h</Message>
          <Outputs Condition="'$(Configuration)|$(Platform)'=='Release|x64'">$(IntDir)\\moc_predefs.h;%(Outputs)</Outputs>
        </CustomBuild>
      </ItemGroup>
      <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
      <Import Project="$(QtMsBuild)\\qt.targets" Condition="Exists('$(QtMsBuild)\\qt.targets')" />
      <ImportGroup Label="ExtensionTargets" />
    </Project>
""")


DEFAULT_QT_MODULES = "core;network;testlib;gui;multimedia;websockets;httpserver;concurrent;sql"


@dataclass(frozen=True)
class TestExtras:
    """What a test needs beyond the default template. Mirrors its tests/CMakeLists.txt entry.
    Paths are repo-relative with '/'."""
    includes: tuple[str, ...] = ()
    defines: str = ""              # MSBuild macros/defines, ';'-terminated
    libs: str = ""                 # ';'-terminated
    sources: tuple[str, ...] = ()  # extra .cpp compiled into the test; Q_OBJECT headers moc'd
    widgets: bool = False
    deps: tuple[str, ...] = ()     # extra project GUIDs the test depends on in the .sln
    folder: str | None = None      # solution sub-folder under Tests


# BitTorrent (libtorrent) and the ed3k torrent join. $(EmuleBt*) / $(EmuleEd3k*) come from
# QtVersion.props, the same switches emulecored uses.
BT_INCLUDES = ("src/bt",)
BT_DEFINES = "$(EmuleBtDefines)"
BT_LIBS = "$(EmuleBtLibs)"
GUI_INCLUDES = ("src/gui", "tests/stubs")

TORRENT_LIST_SOURCES = (
    "src/gui/controls/TorrentListModel.cpp",
    "src/gui/controls/TorrentProgressDelegate.cpp",
    "src/gui/controls/BarShader.cpp",
)
TORRENT_DETAILS_SOURCES = (
    "src/gui/controls/TorrentFilterProxy.cpp",
    "src/gui/controls/TorrentListModel.cpp",
    "src/gui/controls/TorrentListsPane.cpp",
    "src/gui/controls/TorrentPieceGrid.cpp",
    "src/gui/dialogs/TorrentDetailsDialog.cpp",
    "src/gui/dialogs/DetailDialog.cpp",
    "src/gui/dialogs/ClientDetailDialog.cpp",
    "src/gui/utils/IpcFeedback.cpp",
    "src/gui/utils/CountryFlags.cpp",
    "src/gui/controls/ContentScrollArea.cpp",
    "src/gui/utils/Appearance.cpp",
    "src/gui/utils/DialogSizing.cpp",
    "src/gui/utils/MenuUtils.cpp",
    "src/gui/utils/PanelPoller.cpp",
    "src/gui/utils/StatusBarNotifier.cpp",
    "src/gui/utils/TorrentLinks.cpp",
    "src/gui/app/UiState.cpp",
    "src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp",
)

# ed3k tests: need EmuleEd3k (libs/packmule + cargo), like emuleed3k itself. emuleed3k
# links emulebt (when on) and the usenet join, so the same libs as emulecored.
ED3K_EXTRAS = TestExtras(
    includes=BT_INCLUDES + ("src/ed3k", "libs/packmule/crates/packmule-ffi/include"),
    # the hoster resolver switch as emulecored (QtVersion.props, CMake EMULE_HOSTER_JD)
    defines=BT_DEFINES + "$(EmuleEd3kDefines)$(EmuleHosterJdDefines)",
    libs=BT_LIBS + "$(EmuleEd3kLibs)rapidyenc.lib;par2-turbo.lib;gf16.lib;hasher.lib;",
    deps=(EMULEBT_GUID, EMULEED3K_GUID), folder="Ed3k")

TEST_EXTRAS: dict[str, TestExtras] = {
    "tst_BtSmoke": TestExtras(includes=BT_INCLUDES, defines=BT_DEFINES, libs=BT_LIBS,
                              deps=(EMULEBT_GUID,), folder="BitTorrent"),
    "tst_BtLocalSwarm": TestExtras(includes=BT_INCLUDES, defines=BT_DEFINES, libs=BT_LIBS,
                                   deps=(EMULEBT_GUID,), folder="BitTorrent"),
    "tst_Ed3kTorrentJoin": replace(ED3K_EXTRAS, folder="BitTorrent"),
    "tst_Ed3kSession": ED3K_EXTRAS,
    "tst_Ed3kUsenetJoin": ED3K_EXTRAS,
    "tst_TorrentLinks": TestExtras(includes=GUI_INCLUDES, defines=BT_DEFINES, widgets=True,
                                   sources=("src/gui/utils/TorrentLinks.cpp",),
                                   folder="BitTorrent"),
    "tst_TorrentListModel": TestExtras(includes=GUI_INCLUDES, defines=BT_DEFINES, widgets=True,
                                       sources=TORRENT_LIST_SOURCES, folder="BitTorrent"),
    "tst_TorrentDetailsGui": TestExtras(includes=GUI_INCLUDES, defines=BT_DEFINES, widgets=True,
                                        sources=TORRENT_DETAILS_SOURCES, folder="BitTorrent"),
    "tst_Ed3kShared": ED3K_EXTRAS,
    # hoster links: the join with a fake resolver, the JD resolver's and Java lookup's parts
    "tst_Ed3kHoster": ED3K_EXTRAS,
    # prefs → node: extensions, IPv6 source, filter state, LAN discovery, node list asks
    "tst_Ed3kPlumbing": ED3K_EXTRAS,
    # the daemon side: Ed3kBridge + BandwidthCoordinator
    "tst_Ed3kBridge": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                              sources=("src/daemon/Ed3kBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # friends and chat: Ed3kFriends + its IPC (Ed3kFriendsBridge, through Ed3kBridge)
    "tst_Ed3kFriends": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kBridge.cpp",
                                        "src/daemon/Ed3kFriendsBridge.cpp",
                                        "src/daemon/Ed3kNamesBridge.cpp",
                                        "src/daemon/Ed3kRoomsBridge.cpp",
                                        "src/daemon/Ed3kCreditBridge.cpp",
                                        "src/daemon/Ed3kHosterBridge.cpp",
                                        "src/daemon/Ed3kFiltersBridge.cpp",
                                        "src/daemon/Ed3kDownloadsBridge.cpp",
                                        "src/daemon/Ed3kSetsBridge.cpp",
                                        "src/daemon/BandwidthCoordinator.cpp")),
    # names and websites: Ed3kNames, Ed3kSiteServer + their IPC (Ed3kNamesBridge, through Ed3kBridge)
    "tst_Ed3kNames": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                             sources=("src/daemon/Ed3kBridge.cpp",
                                      "src/daemon/Ed3kFriendsBridge.cpp",
                                      "src/daemon/Ed3kNamesBridge.cpp",
                                      "src/daemon/Ed3kRoomsBridge.cpp",
                                      "src/daemon/Ed3kCreditBridge.cpp",
                                      "src/daemon/Ed3kHosterBridge.cpp",
                                      "src/daemon/Ed3kFiltersBridge.cpp",
                                      "src/daemon/Ed3kDownloadsBridge.cpp",
                                      "src/daemon/Ed3kSetsBridge.cpp",
                                      "src/daemon/BandwidthCoordinator.cpp")),
    # credits: Ed3kCredit (key file, ledger config) + its IPC (Ed3kCreditBridge, through Ed3kBridge)
    "tst_Ed3kCredit": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                              sources=("src/daemon/Ed3kBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # ed3k keyword search (Ed3kSearch)
    "tst_Ed3kSearch": ED3K_EXTRAS,
    # plain downloads: Ed3kDownloads + its IPC (Ed3kDownloadsBridge, asked directly)
    "tst_Ed3kDownloads": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                                 sources=("src/daemon/Ed3kDownloadsBridge.cpp",)),
    # sets and live: Ed3kSets, Ed3kLive + their IPC (Ed3kSetsBridge, asked directly); OpenSSL signs
    "tst_Ed3kSets": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                            sources=("src/daemon/Ed3kSetsBridge.cpp",)),
    "tst_Ed3kLive": ED3K_EXTRAS,
    # rooms: Ed3kRooms + its IPC (Ed3kRoomsBridge, asked directly)
    "tst_Ed3kRooms": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                             sources=("src/daemon/Ed3kRoomsBridge.cpp",)),
    # stake tokens: Ed3kCredit (burn, claim, stake-claims.json) + its IPC (through Ed3kBridge)
    "tst_Ed3kStake": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                             sources=("src/daemon/Ed3kBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # the upload queue view: Ed3kUploads + its GetUploads rows (Ed3kUploadsBridge)
    "tst_Ed3kUploads": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kUploadsBridge.cpp",)),
    # filter lists: Ed3kFilters + its IPC (Ed3kFiltersBridge, through Ed3kBridge)
    "tst_Ed3kFilters": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # GUI sources against the stub pch; include order as in CMake picks the pch.h
    "tst_LogTextView": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                  sources=("src/gui/controls/LogTextView.cpp",)),
    "tst_TaskbarProgress": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                      sources=("src/gui/utils/TaskbarProgress.cpp",)),
    "tst_UrlPrefField": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                   sources=("src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp",)),
    "tst_Ed3kSetsDialog": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                     sources=("src/gui/dialogs/Ed3kSetsDialog.cpp", "src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp")),
    "tst_IpcTls": TestExtras(includes=GUI_INCLUDES + ("src/daemon",), widgets=True,
                             sources=("src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp",
                                      "src/daemon/IpcTlsIdentity.cpp")),
    "tst_SingleInstance": TestExtras(includes=("tests/stubs", "src/gui"),
                                     sources=("src/gui/app/SingleInstance.cpp",)),
    "tst_MassRenameDialog": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                       sources=("src/gui/dialogs/MassRenameDialog.cpp",
                                                "src/gui/dialogs/SimpleCleanupDialog.cpp",
                                                "src/gui/utils/DialogSizing.cpp",
                                                "src/gui/controls/ContentScrollArea.cpp",
                                                "src/gui/app/UiState.cpp",
                                                "src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp")),
}
NO_EXTRAS = TestExtras()


def discover_tests() -> list[str]:
    """Find all tst_*.cpp files and return sorted test names."""
    pattern = os.path.join(TESTS_DIR, "tst_*.cpp")
    names = []
    for path in glob.glob(pattern):
        name = os.path.splitext(os.path.basename(path))[0]
        names.append(name)
    return sorted(names)


def vs_path(repo_rel: str) -> str:
    """Repo-relative path as seen from tests/vs."""
    return "..\\..\\" + repo_rel.replace("/", "\\")


def moc_headers(sources: tuple[str, ...]) -> list[str]:
    """Headers next to the extra sources that declare Q_OBJECT."""
    headers = []
    for src in sources:
        header = os.path.splitext(src)[0] + ".h"
        path = os.path.join(PROJECT_ROOT, header)
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as f:
                if "Q_OBJECT" in f.read():
                    headers.append(header)
    return headers


def sources_xml(extras: TestExtras) -> str:
    """Extra ClCompile items, then QtMoc items in their own ItemGroup."""
    xml = "".join(f'\n    <ClCompile Include="{vs_path(s)}" />' for s in extras.sources)
    headers = moc_headers(extras.sources)
    if headers:
        xml += "\n  </ItemGroup>\n  <ItemGroup>"
        for h in headers:
            xml += (f'\n    <QtMoc Include="{vs_path(h)}">'
                    "\n      <QtMocFileName>moc_%(Filename).cpp</QtMocFileName>"
                    "\n    </QtMoc>")
    return xml


def generate_vcxproj(name: str) -> str:
    extras = TEST_EXTRAS.get(name, NO_EXTRAS)
    return VCXPROJ_TEMPLATE.format(
        name=name,
        guid=make_guid(name),
        includes="".join(vs_path(i) + ";" for i in extras.includes),
        defines=extras.defines,
        libs=extras.libs,
        qt_modules=DEFAULT_QT_MODULES + (";widgets" if extras.widgets else ""),
        sources=sources_xml(extras),
    ).rstrip("\r\n")


def test_deps(name: str) -> list[str]:
    extras = TEST_EXTRAS.get(name, NO_EXTRAS)
    return [EMULECORE_GUID, EMULEIPC_GUID, EMULEINDEXER_GUID, EMULEUSENET_GUID, *extras.deps]


def folder_guid(folder: str) -> str:
    return make_guid(f"folder-{folder}")


def test_parent(name: str) -> str:
    """Solution folder a test sits in: Tests, or a sub-folder of it."""
    folder = TEST_EXTRAS.get(name, NO_EXTRAS).folder
    return folder_guid(folder) if folder else TESTS_FOLDER_GUID


def sub_folders(test_names: list[str]) -> list[str]:
    return sorted({TEST_EXTRAS[t].folder for t in test_names
                   if t in TEST_EXTRAS and TEST_EXTRAS[t].folder})


def project_lines(name: str, rel_path: str, guid: str, deps: list[str],
                  type_guid: str = VS_CPP_PROJECT_TYPE) -> list[str]:
    lines = [f'Project("{type_guid}") = "{name}", "{rel_path}", "{guid}"']
    if deps:
        lines.append("\tProjectSection(ProjectDependencies) = postProject")
        lines += [f"\t\t{d} = {d}" for d in deps]
        lines.append("\tEndProjectSection")
    lines.append("EndProject")
    return lines


def config_lines(guid: str) -> list[str]:
    return [f"\t\t{guid}.{cfg}|x64.{kind} = {cfg}|x64"
            for cfg in ("Debug", "Release") for kind in ("ActiveCfg", "Build.0")]


def generate_sln(test_names: list[str]) -> str:
    """Generate a complete .sln file with all projects."""
    lines = []
    lines.append("Microsoft Visual Studio Solution File, Format Version 12.00")
    lines.append("# Visual Studio Version 17")

    # Existing projects (emulecore, emuleipc, emuleindexer, emuleusenet,
    # emulecored, emuleqt).
    # Anything absent from this list is dropped from the .sln on the next run,
    # so a new library must be added here as well as to the solution.
    existing_projects = [
        ("emulecore", "core\\emulecore.vcxproj", EMULECORE_GUID, []),
        ("emuleipc", "ipc\\emuleipc.vcxproj", EMULEIPC_GUID, []),
        ("emuleindexer", "indexer\\emuleindexer.vcxproj", EMULEINDEXER_GUID,
         [EMULECORE_GUID]),
        ("emuleusenet", "usenet\\emuleusenet.vcxproj", EMULEUSENET_GUID, [EMULECORE_GUID]),
        ("emulebt", "bt\\emulebt.vcxproj", EMULEBT_GUID, [EMULECORE_GUID]),
        ("emuleed3k", "ed3k\\emuleed3k.vcxproj", EMULEED3K_GUID, [EMULECORE_GUID]),
        ("emulecored", "daemon\\emulecored.vcxproj", "{4ECEE8D7-03DE-3DE1-A2C6-C56B3BD68584}",
         [EMULECORE_GUID, EMULEIPC_GUID, EMULEINDEXER_GUID, EMULEUSENET_GUID, EMULEBT_GUID,
          EMULEED3K_GUID]),
        ("emuleqt", "gui\\emuleqt.vcxproj", "{CEA0CD5B-F0C0-3EB9-8BEA-C9CEDA11C838}",
         [EMULECORE_GUID, EMULEIPC_GUID, EMULEBT_GUID]),
    ]

    for name, path, guid, deps in existing_projects:
        lines += project_lines(name, path, guid, deps)

    # Tests solution folder and its sub-folders
    lines += project_lines("Tests", "Tests", TESTS_FOLDER_GUID, [], VS_FOLDER_TYPE)
    folders = sub_folders(test_names)
    for folder in folders:
        lines += project_lines(folder, folder, folder_guid(folder), [], VS_FOLDER_TYPE)

    # Test projects
    test_guids = []
    for tname in test_names:
        guid = make_guid(tname)
        test_guids.append((tname, guid))
        lines += project_lines(tname, f"..\\tests\\vs\\{tname}.vcxproj", guid, test_deps(tname))

    # Global section
    lines.append("Global")

    # Solution configuration platforms
    lines.append("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    lines.append("\t\tDebug|x64 = Debug|x64")
    lines.append("\t\tRelease|x64 = Release|x64")
    lines.append("\tEndGlobalSection")

    # Project configuration platforms
    lines.append("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    all_guids = [g for _, _, g, _ in existing_projects] + [g for _, g in test_guids]
    for guid in all_guids:
        lines += config_lines(guid)
    lines.append("\tEndGlobalSection")

    # Nested projects - put tests under Tests folder (or a sub-folder of it)
    lines.append("\tGlobalSection(NestedProjects) = preSolution")
    for folder in folders:
        lines.append(f"\t\t{folder_guid(folder)} = {TESTS_FOLDER_GUID}")
    for tname, guid in test_guids:
        lines.append(f"\t\t{guid} = {test_parent(tname)}")
    lines.append("\tEndGlobalSection")

    lines.append("\tGlobalSection(ExtensibilityGlobals) = postSolution")
    lines.append("\tEndGlobalSection")
    lines.append("\tGlobalSection(ExtensibilityAddIns) = postSolution")
    lines.append("\tEndGlobalSection")
    lines.append("EndGlobal")

    # Plain "\n" here on purpose: main() opens the file with newline="\r\n", which
    # translates every "\n" on write. Joining with "\r\n" as well produced "\r\r\n" on
    # every line — a solution file VS tolerates but git sees as a whole-file rewrite.
    return "\n".join(lines) + "\n"


def patch_sln(sln: str, test_names: list[str]) -> str:
    """Add the given tests (and their sub-folders) to an existing .sln, leaving everything
    else as is. Entries already present are kept."""
    lines = sln.split("\n")

    def present(guid: str) -> bool:
        return any(line.startswith("Project(") and line.rstrip().endswith(f'"{guid}"')
                   for line in lines)

    new_projects, new_configs, new_nested = [], [], []
    for folder in sub_folders(test_names):
        guid = folder_guid(folder)
        if not present(guid):
            new_projects += project_lines(folder, folder, guid, [], VS_FOLDER_TYPE)
            new_nested.append(f"\t\t{guid} = {TESTS_FOLDER_GUID}")
    for tname in test_names:
        guid = make_guid(tname)
        if present(guid):
            continue
        new_projects += project_lines(tname, f"..\\tests\\vs\\{tname}.vcxproj", guid,
                                      test_deps(tname))
        new_configs += config_lines(guid)
        new_nested.append(f"\t\t{guid} = {test_parent(tname)}")

    def insert_before_end_of(section: str, new: list[str]) -> None:
        start = next(i for i, line in enumerate(lines) if section in line)
        end = next(i for i in range(start, len(lines)) if "EndGlobalSection" in lines[i])
        lines[end:end] = new

    if new_projects:
        glob_idx = next(i for i, line in enumerate(lines) if line.rstrip() == "Global")
        lines[glob_idx:glob_idx] = new_projects
        insert_before_end_of("GlobalSection(ProjectConfigurationPlatforms)", new_configs)
        insert_before_end_of("GlobalSection(NestedProjects)", new_nested)
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="+", metavar="TEST",
                        help="write just these tests' vcxproj files and add them to the "
                             "existing .sln in place (keeps projects this script doesn't know)")
    args = parser.parse_args()

    test_names = discover_tests()
    if args.only:
        unknown = sorted(set(args.only) - set(test_names))
        if unknown:
            parser.error(f"no tests/<name>.cpp for: {', '.join(unknown)}")
        test_names = sorted(args.only)
    print(f"Found {len(test_names)} tests")

    os.makedirs(VS_DIR, exist_ok=True)

    # Generate vcxproj files
    for name in test_names:
        path = os.path.join(VS_DIR, f"{name}.vcxproj")
        content = generate_vcxproj(name)
        with open(path, "w", encoding="utf-8", newline="\r\n") as f:
            f.write(content)

    print(f"Generated {len(test_names)} vcxproj files in tests/vs/")

    # Update solution file
    if args.only:
        # utf-8-sig: keep the BOM VS writes; newline="" keeps the CRLFs as read
        with open(SLN_PATH, encoding="utf-8-sig", newline="") as f:
            sln = f.read()
        sln_content = patch_sln(sln.replace("\r\n", "\n"), test_names).replace("\n", "\r\n")
        with open(SLN_PATH, "w", encoding="utf-8-sig", newline="") as f:
            f.write(sln_content)
    else:
        sln_content = generate_sln(test_names)
        with open(SLN_PATH, "w", encoding="utf-8", newline="\r\n") as f:
            f.write(sln_content)

    print(f"Updated {SLN_PATH}")


if __name__ == "__main__":
    main()
