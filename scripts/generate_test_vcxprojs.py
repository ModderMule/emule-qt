#!/usr/bin/env python3
"""Generate per-test Visual Studio vcxproj files and update the solution.

Tests, their extra sources, include dirs and Widgets use come from tests/CMakeLists.txt
(emule_add_test / target_include_directories / target_link_libraries), so a source added
there reaches the vcxproj too. TEST_EXTRAS adds what CMake can't say in MSBuild terms
(defines, libs, .sln deps, solution folder). A project's existing GUID is kept.

No args: writes every changed tests/vs project; the .sln gains missing tests and loses
tests no longer in CMake. Other .sln projects are never touched.
--only tst_A tst_B: just these tests.
--check: list what would change, write nothing (exit 1 on drift).
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, replace
import uuid
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
SPECIALDLP_GUID = "{3A9EE43B-4961-4F30-A565-A48EB043B6C4}"
EMULEQT_UPDATER_GUID ="{0F06CD43-CBCD-4E5D-A90A-B3A4028B2A92}"
VS_CPP_PROJECT_TYPE = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
VS_FOLDER_TYPE = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}"
TESTS_FOLDER_GUID = "{E0A0B5F0-0001-0001-0001-000000000001}"


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
    "src/gui/utils/FileTypeIcons.cpp",
)
TORRENT_DETAILS_SOURCES = (
    "src/gui/controls/TorrentFilterProxy.cpp",
    "src/gui/controls/TorrentListModel.cpp",
    "src/gui/utils/FileTypeIcons.cpp",
    "src/gui/controls/TorrentListsPane.cpp",
    "src/gui/controls/TorrentFileTree.cpp",
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
    # runs the built updater exe; relative to the bin\<Config> run dir like the data dirs
    # loads the real specialdlp.dll from the build tree (CMake: $<TARGET_FILE:specialdlp>)
    "tst_Dlp": TestExtras(includes=("external/specialdlp/include",),
                          defines='SPECIALDLP_LIB_PATH="../../../bin/$(Configuration)/specialdlp.dll";',
                          deps=(SPECIALDLP_GUID,)),
    "tst_Updater": TestExtras(defines="EMULEQT_UPDATER_PATH=../../bin/$(Configuration)/emuleqt-updater.exe;",
                              deps=(EMULEQT_UPDATER_GUID,), folder="Daemon"),
    # compiles the daemon source standalone (tests/CMakeLists.txt)
    "tst_AutoUpdater": TestExtras(includes=("src/daemon",), sources=("src/daemon/AutoUpdater.cpp",),
                                  folder="Daemon"),
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
    "tst_HosterSolvers": ED3K_EXTRAS,
    # prefs → node: extensions, IPv6 source, filter state, LAN discovery, node list asks
    "tst_Ed3kPlumbing": ED3K_EXTRAS,
    # the daemon side: Ed3kBridge + BandwidthCoordinator
    "tst_Ed3kBridge": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                              sources=("src/daemon/Ed3kBridge.cpp",
                                       "src/daemon/Ed3kLinksBridge.cpp",
                                       "src/daemon/Ed3kOwnSetsBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/Ed3kPaidBridge.cpp",
                                       "src/daemon/Ed3kPaidSellerBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # friends and chat: Ed3kFriends + its IPC (Ed3kFriendsBridge, through Ed3kBridge)
    "tst_Ed3kFriends": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kBridge.cpp",
                                        "src/daemon/Ed3kLinksBridge.cpp",
                                        "src/daemon/Ed3kOwnSetsBridge.cpp",
                                        "src/daemon/Ed3kFriendsBridge.cpp",
                                        "src/daemon/Ed3kNamesBridge.cpp",
                                        "src/daemon/Ed3kRoomsBridge.cpp",
                                        "src/daemon/Ed3kCreditBridge.cpp",
                                        "src/daemon/Ed3kHosterBridge.cpp",
                                        "src/daemon/Ed3kFiltersBridge.cpp",
                                        "src/daemon/Ed3kDownloadsBridge.cpp",
                                        "src/daemon/Ed3kSetsBridge.cpp",
                                        "src/daemon/Ed3kPaidBridge.cpp",
                                        "src/daemon/Ed3kPaidSellerBridge.cpp",
                                        "src/daemon/BandwidthCoordinator.cpp")),
    # names and websites: Ed3kNames, Ed3kSiteServer + their IPC (Ed3kNamesBridge, through Ed3kBridge)
    "tst_Ed3kNames": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                             sources=("src/daemon/Ed3kBridge.cpp",
                                      "src/daemon/Ed3kLinksBridge.cpp",
                                      "src/daemon/Ed3kOwnSetsBridge.cpp",
                                      "src/daemon/Ed3kFriendsBridge.cpp",
                                      "src/daemon/Ed3kNamesBridge.cpp",
                                      "src/daemon/Ed3kRoomsBridge.cpp",
                                      "src/daemon/Ed3kCreditBridge.cpp",
                                      "src/daemon/Ed3kHosterBridge.cpp",
                                      "src/daemon/Ed3kFiltersBridge.cpp",
                                      "src/daemon/Ed3kDownloadsBridge.cpp",
                                      "src/daemon/Ed3kSetsBridge.cpp",
                                      "src/daemon/Ed3kPaidBridge.cpp",
                                      "src/daemon/Ed3kPaidSellerBridge.cpp",
                                      "src/daemon/BandwidthCoordinator.cpp")),
    # credits: Ed3kCredit (key file, ledger config) + its IPC (Ed3kCreditBridge, through Ed3kBridge)
    "tst_Ed3kCredit": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                              sources=("src/daemon/Ed3kBridge.cpp",
                                       "src/daemon/Ed3kLinksBridge.cpp",
                                       "src/daemon/Ed3kOwnSetsBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/Ed3kPaidBridge.cpp",
                                       "src/daemon/Ed3kPaidSellerBridge.cpp",
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
    # paid subscriptions: Ed3kPaid + its IPC (Ed3kPaidBridge, asked directly); OpenSSL signs
    "tst_Ed3kPaid": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                            sources=("src/daemon/Ed3kPaidBridge.cpp",)),
    # selling: Ed3kPaidSeller + its IPC (Ed3kPaidSellerBridge, asked directly)
    "tst_Ed3kPaidSeller": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                                  sources=("src/daemon/Ed3kPaidSellerBridge.cpp",)),
    # My ed3k Sets: Ed3kOwnSets + its IPC (Ed3kOwnSetsBridge; SellEd3k kind 2 through Ed3kPaidSellerBridge)
    "tst_Ed3kOwnSets": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kOwnSetsBridge.cpp",
                                        "src/daemon/Ed3kPaidSellerBridge.cpp")),
    # rooms: Ed3kRooms + its IPC (Ed3kRoomsBridge, asked directly)
    "tst_Ed3kRooms": replace(ED3K_EXTRAS, includes=("src/daemon", "src/gui") + ED3K_EXTRAS.includes, widgets=True,
                             sources=("src/daemon/Ed3kRoomsBridge.cpp",
                                      "src/gui/panels/Ed3kRoomTab.cpp",
                                      "src/gui/panels/ChatChannelView.cpp",
                                      "src/gui/controls/LogTextView.cpp")),
    # stake tokens: Ed3kCredit (burn, claim, stake-claims.json) + its IPC (through Ed3kBridge)
    "tst_Ed3kStake": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                             sources=("src/daemon/Ed3kBridge.cpp",
                                      "src/daemon/Ed3kLinksBridge.cpp",
                                      "src/daemon/Ed3kOwnSetsBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/Ed3kPaidBridge.cpp",
                                       "src/daemon/Ed3kPaidSellerBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # hiding (batch 15): Ed3kHide + its IPC (Ed3kHideBridge, status through Ed3kBridge)
    "tst_Ed3kHide": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                            sources=("src/daemon/Ed3kHideBridge.cpp",
                                     "src/daemon/Ed3kBridge.cpp",
                                     "src/daemon/Ed3kOwnSetsBridge.cpp",
                                     "src/daemon/Ed3kLinksBridge.cpp",
                                     "src/daemon/Ed3kFriendsBridge.cpp",
                                     "src/daemon/Ed3kNamesBridge.cpp",
                                     "src/daemon/Ed3kRoomsBridge.cpp",
                                     "src/daemon/Ed3kCreditBridge.cpp",
                                     "src/daemon/Ed3kHosterBridge.cpp",
                                     "src/daemon/Ed3kFiltersBridge.cpp",
                                     "src/daemon/Ed3kDownloadsBridge.cpp",
                                     "src/daemon/Ed3kSetsBridge.cpp",
                                     "src/daemon/Ed3kPaidBridge.cpp",
                                     "src/daemon/Ed3kPaidSellerBridge.cpp",
                                     "src/daemon/BandwidthCoordinator.cpp")),
    # the upload queue view: Ed3kUploads + its GetUploads rows (Ed3kUploadsBridge)
    "tst_Ed3kUploads": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kUploadsBridge.cpp",)),
    # filter lists: Ed3kFilters + its IPC (Ed3kFiltersBridge, through Ed3kBridge)
    "tst_Ed3kFilters": replace(ED3K_EXTRAS, includes=("src/daemon",) + ED3K_EXTRAS.includes,
                               sources=("src/daemon/Ed3kBridge.cpp",
                                        "src/daemon/Ed3kLinksBridge.cpp",
                                        "src/daemon/Ed3kOwnSetsBridge.cpp",
                                       "src/daemon/Ed3kFriendsBridge.cpp",
                                       "src/daemon/Ed3kNamesBridge.cpp",
                                       "src/daemon/Ed3kRoomsBridge.cpp",
                                       "src/daemon/Ed3kCreditBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/Ed3kFiltersBridge.cpp",
                                       "src/daemon/Ed3kDownloadsBridge.cpp",
                                       "src/daemon/Ed3kSetsBridge.cpp",
                                       "src/daemon/Ed3kPaidBridge.cpp",
                                       "src/daemon/Ed3kPaidSellerBridge.cpp",
                                       "src/daemon/Ed3kHosterBridge.cpp",
                                       "src/daemon/BandwidthCoordinator.cpp")),
    # GUI sources against the stub pch; include order as in CMake picks the pch.h
    "tst_LogTextView": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                  sources=("src/gui/controls/LogTextView.cpp",)),
    "tst_TaskbarProgress": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                      sources=("src/gui/utils/TaskbarProgress.cpp",)),
    "tst_UrlPrefField": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                   sources=("src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp",)),
    # the ed3k rows of the download list and the eD2K bar painter
    "tst_Ed3kDownloadRows": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                       sources=("src/gui/controls/Ed3kDownloadRows.cpp",
                                                "src/gui/utils/Appearance.cpp",
                                                "src/gui/controls/DownloadListModel.cpp",
                                                "src/gui/controls/DownloadProgressDelegate.cpp",
                                                "src/gui/controls/BarShader.cpp",
                                                "src/gui/utils/ClientIcons.cpp",
                                                "src/gui/utils/CountryFlags.cpp",
                                                "src/gui/utils/PriorityText.cpp",
                                                "src/gui/utils/RatingIcons.cpp",
                                                "src/gui/utils/FileTypeIcons.cpp")),
    "tst_Ed3kSetsDialog": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                     sources=("src/gui/dialogs/Ed3kSetsDialog.cpp", "src/gui/dialogs/Ed3kSellDialogs.cpp", "src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp")),
    # My ed3k Sets in the GUI (batch 15)
    "tst_Ed3kOwnSetDialog": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                       sources=("src/gui/dialogs/Ed3kOwnSetDialog.cpp", "src/gui/panels/Ed3kOwnSetsView.cpp",
                                                "src/gui/app/SkinColors.cpp", "src/gui/utils/Appearance.cpp",
                                                "src/gui/dialogs/Ed3kSellDialogs.cpp", "src/gui/utils/MenuUtils.cpp",
                                                "src/gui/utils/SharedFilesFetch.cpp", "src/gui/app/IpcClient.cpp",
                                                "src/gui/app/KnownCores.cpp")),
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
    # eNode result rows, Meta API account and web links dialogs
    # the Hoster tab's package rows, off pushed items
    "tst_HosterPanel": TestExtras(includes=GUI_INCLUDES, defines="$(EmuleHosterJdDefines)", widgets=True,
                                  sources=("src/gui/panels/HosterPanel.cpp",
                                           "src/gui/dialogs/HosterPuzzleDialog.cpp",
                                           "src/gui/utils/Appearance.cpp",
                                           "src/gui/utils/FileTypeIcons.cpp",
                                           "src/gui/utils/ListActivation.cpp",
                                           "src/gui/dialogs/FindInListDialog.cpp",
                                           "src/gui/utils/MenuUtils.cpp",
                                           "src/gui/app/UiState.cpp",
                                           "src/gui/app/IpcClient.cpp",
                                           "src/gui/app/KnownCores.cpp",
                                           "src/gui/app/ClickNLoadServer.cpp",
                                           "resources/emuleqt.qrc")),
    # Click'n'Load 2 server on the GUI's port 9666
    "tst_ClickNLoadServer": TestExtras(includes=GUI_INCLUDES, widgets=True,
                                       sources=("src/gui/app/ClickNLoadServer.cpp",)),
    # a torrent/Usenet search result's Details, off hand-built inspection maps
    "tst_MetaResultDetailDialog": TestExtras(includes=GUI_INCLUDES, widgets=True, sources=(
        "src/gui/dialogs/MetaResultDetailDialog.cpp",
        "src/gui/controls/TorrentFileTree.cpp",
        "src/gui/dialogs/DetailDialog.cpp",
        "src/gui/dialogs/ClientDetailDialog.cpp",
        "src/gui/utils/IpcFeedback.cpp",
        "src/gui/utils/CountryFlags.cpp",
        "src/gui/controls/ContentScrollArea.cpp",
        "src/gui/utils/Appearance.cpp",
        "src/gui/utils/DialogSizing.cpp",
        "src/gui/utils/MenuUtils.cpp",
        "src/gui/utils/StatusBarNotifier.cpp",
        "src/gui/app/UiState.cpp",
        "src/gui/app/IpcClient.cpp", "src/gui/app/KnownCores.cpp")),
    "tst_MetaSearchGui": TestExtras(includes=GUI_INCLUDES + ("src/core",), widgets=True,
                                    sources=(
                                              "src/gui/utils/Appearance.cpp",
                                              "src/gui/dialogs/WebLinksDialog.cpp",
                                              "src/gui/utils/Ed2kLinkImporter.cpp",
                                              "src/gui/utils/TorrentLinks.cpp",
                                              "src/gui/utils/HttpCacheLinkImporter.cpp",
                                              "src/gui/utils/DialogSizing.cpp",
                                              "src/gui/utils/StatusBarNotifier.cpp",
                                              "src/gui/controls/ContentScrollArea.cpp",
                                              "src/gui/app/UiState.cpp",
                                              "src/gui/controls/SearchResultsModel.cpp",
                                              "src/gui/controls/SearchResultsProxy.cpp",
                                              "src/gui/controls/FilterEdit.cpp",
                                              "src/gui/dialogs/MetaAccountDialog.cpp",
                                              "src/gui/utils/RatingIcons.cpp",
                                              "src/gui/utils/FileTypeIcons.cpp",
                                              "src/gui/utils/MenuUtils.cpp",
                                              "src/gui/utils/ViewSelection.cpp",
                                              "src/gui/app/IpcClient.cpp",
                                              "src/gui/app/KnownCores.cpp",
                                              "resources/emuleqt.qrc")),
    # every dialog at a small screen size
    "tst_SmallScreenDialogs": TestExtras(includes=GUI_INCLUDES + ("src/core",), widgets=True,
                                         sources=(
                                                   "src/gui/dialogs/AddFriendDialog.cpp",
                                                   "src/gui/dialogs/AddSourceDialog.cpp",
                                                   "src/gui/dialogs/ArchivePreviewPanel.cpp",
                                                   "src/gui/dialogs/BugReportDialog.cpp",
                                                   "src/gui/dialogs/FirstStartWizard.cpp",
                                                   "src/gui/dialogs/MetaAccountDialog.cpp",
                                                   "src/gui/dialogs/OptionsDialog.cpp",
                                                   "src/gui/dialogs/Ed3kOptionsPages.cpp",
                                                   "src/gui/dialogs/Ed3kFilterPage.cpp",
                                                   "src/gui/dialogs/Ed3kSellPage.cpp",
                                                   "src/gui/dialogs/Ed3kSellDialogs.cpp",
                                                   "src/gui/dialogs/Ed3kStakeGroup.cpp",
                                                   "src/gui/dialogs/Ed3kHidingGroup.cpp",
                                                   "src/gui/app/Ed3kNameOpener.cpp",
                                                   "src/gui/dialogs/ToolbarCustomizeDialog.cpp",
                                                   "src/gui/controls/AccordionSidebar.cpp",
                                                   "src/gui/controls/LogTextView.cpp",
                                                   "src/gui/controls/LogWidget.cpp",
                                                   "src/gui/controls/StatsGraph.cpp",
                                                   "src/gui/panels/StatisticsPanel.cpp",
                                                   "src/gui/utils/FileAssociation.cpp",
                                                   "src/gui/utils/PanelPoller.cpp",
                                                   "src/gui/utils/WebServices.cpp",
                                                   "src/gui/app/AutoStart.cpp",
                                                   "src/gui/app/Ed2kSchemeHandler.cpp",
                                                   "src/gui/dialogs/CategoryDialog.cpp",
                                                   "src/gui/dialogs/ClientDetailDialog.cpp",
                                                   "src/gui/dialogs/CollectionCreateDialog.cpp",
                                                   "src/gui/dialogs/CollectionViewDialog.cpp",
                                                   "src/gui/dialogs/CommentEditPanel.cpp",
                                                   "src/gui/dialogs/CommentsPanel.cpp",
                                                   "src/gui/dialogs/CoreConnectDialog.cpp",
                                                   "src/gui/dialogs/DetailDialog.cpp",
                                                   "src/gui/dialogs/FileDetailDialog.cpp",
                                                   "src/gui/dialogs/FindInListDialog.cpp",
                                                   "src/gui/dialogs/ImportDownloadsDialog.cpp",
                                                   "src/gui/dialogs/MetadataPage.cpp",
                                                   "src/gui/dialogs/NetworkInfoDialog.cpp",
                                                   "src/gui/dialogs/NzbFileChooserDialog.cpp",
                                                   "src/gui/dialogs/PasteTextDialog.cpp",
                                                   "src/gui/dialogs/SearchDetailDialog.cpp",
                                                   "src/gui/dialogs/UsenetArchiveEntryDialog.cpp",
                                                   "src/gui/dialogs/UsenetDetailsDialog.cpp",
                                                   "src/gui/dialogs/WebLinksDialog.cpp",
                                                   "src/gui/utils/Ed2kLinkImporter.cpp",
                                                   "src/gui/utils/TorrentLinks.cpp",
                                                   "src/gui/utils/HttpCacheLinkImporter.cpp",
                                                   "src/gui/controls/BarShader.cpp",
                                                   "src/gui/controls/ContentScrollArea.cpp",
                                                   "src/gui/controls/UsenetFileCheckList.cpp",
                                                   "src/gui/controls/UsenetProgressDelegate.cpp",
                                                   "src/gui/controls/UsenetQueueModel.cpp",
                                                   "src/gui/utils/Appearance.cpp",
                                                   "src/gui/utils/CountryFlags.cpp",
                                                   "src/gui/utils/DialogSizing.cpp",
                                                   "src/gui/utils/FileTypeIcons.cpp",
                                                   "src/gui/utils/IpcFeedback.cpp",
                                                   "src/gui/utils/ListActivation.cpp",
                                                   "src/gui/utils/MenuUtils.cpp",
                                                   "src/gui/utils/PreviewLauncher.cpp",
                                                   "src/gui/utils/RatingIcons.cpp",
                                                   "src/gui/utils/SharedFilesFetch.cpp",
                                                   "src/gui/utils/StatusBarNotifier.cpp",
                                                   "src/gui/app/UiState.cpp",
                                                   "src/gui/app/IpcClient.cpp",
                                                   "src/gui/app/KnownCores.cpp",
                                                   "resources/emuleqt.qrc")),
}
NO_EXTRAS = TestExtras()


@dataclass(frozen=True)
class CMakeTest:
    """One emule_add_test() with what tests/CMakeLists.txt adds to it. Repo-relative '/' paths."""
    sources: tuple[str, ...] = ()   # besides tests/<name>.cpp
    includes: tuple[str, ...] = ()
    widgets: bool = False
    libs: tuple[str, ...] = ()      # LIBRARIES + target_link_libraries, as written


CMAKE_LISTS = os.path.join(TESTS_DIR, "CMakeLists.txt")


def cmake_commands(text: str):
    """(command, [args]) for every call in a CMake file; comments and quotes handled."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "#":
            i = text.find("\n", i)
            i = n if i < 0 else i
            continue
        m = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(").match(text, i)
        if not m or (i > 0 and (text[i - 1].isalnum() or text[i - 1] == "_")):
            i += 1
            continue
        args, cur, depth, j = [], "", 1, m.end()
        while j < n and depth:
            ch = text[j]
            if ch == '"':
                k = j + 1
                while k < n and text[k] != '"':
                    k += 2 if text[k] == "\\" else 1
                cur += text[j + 1:k]
                j = k + 1
                continue
            if ch == "#":
                j = text.find("\n", j)
                j = n if j < 0 else j
                continue
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if not depth:
                    break
            if ch.isspace() and depth == 1:
                if cur:
                    args.append(cur)
                cur = ""
            else:
                cur += ch
            j += 1
        if cur:
            args.append(cur)
        yield m.group(1).lower(), args
        i = j + 1


def cmake_path(arg: str) -> str | None:
    """Repo-relative path of a CMake path argument; None if it's a variable we can't resolve."""
    arg = arg.replace("${CMAKE_SOURCE_DIR}/", "").replace("${PROJECT_SOURCE_DIR}/", "")
    if arg.startswith("${CMAKE_CURRENT_SOURCE_DIR}"):
        arg = "tests" + arg[len("${CMAKE_CURRENT_SOURCE_DIR}"):]
    elif "$" in arg:
        return None
    elif not arg.startswith(("src/", "libs/", "resources/", "external/", "tests")):
        arg = "tests/" + arg
    return os.path.normpath(arg).replace(os.sep, "/")


def parse_cmake_tests(path: str = CMAKE_LISTS) -> dict[str, CMakeTest]:
    with open(path, encoding="utf-8") as f:
        text = f.read()
    found: dict[str, dict] = {}
    for cmd, args in cmake_commands(text):
        if cmd == "emule_add_test" and args and args[0].startswith("tst_"):
            name, srcs, mode = args[0], [], None
            for a in args[1:]:
                if a in ("SOURCES", "LIBRARIES", "FOLDER"):
                    mode = a
                elif mode == "SOURCES":
                    p = cmake_path(a)
                    if p and p != f"tests/{name}.cpp" and not p.endswith(".h"):
                        srcs.append(p)
                elif mode == "LIBRARIES":
                    found.setdefault(name, {}).setdefault("libs", []).append(a)
            t = found.setdefault(name, {})
            t["sources"] = srcs
            # as emule_add_test(): GUI sources pull in SkinIcons and Widgets
            if any(s.startswith("src/gui/") for s in srcs):
                if "src/gui/app/SkinIcons.cpp" not in srcs:
                    srcs.append("src/gui/app/SkinIcons.cpp")
                t["widgets"] = True
        elif cmd in ("target_include_directories", "target_link_libraries", "target_sources") \
                and args and args[0] in found:
            t = found[args[0]]
            vals = [a for a in args[1:] if a not in ("PRIVATE", "PUBLIC", "INTERFACE")]
            if cmd == "target_link_libraries":
                t.setdefault("libs", []).extend(vals)
            elif cmd == "target_sources":
                t["sources"] += [p for p in map(cmake_path, vals) if p and not p.endswith(".h")]
            else:
                t.setdefault("includes", []).extend(
                    p for p in map(cmake_path, vals) if p and p != "tests")
    return {n: CMakeTest(tuple(t.get("sources", ())), tuple(t.get("includes", ())),
                         t.get("widgets", False) or any("Widgets" in a for a in t.get("libs", ())),
                         tuple(t.get("libs", ())))
            for n, t in found.items()}


CMAKE_TESTS = parse_cmake_tests()


def discover_tests() -> list[str]:
    """Tests CMake builds that have a tests/<name>.cpp."""
    return sorted(n for n in CMAKE_TESTS if os.path.exists(os.path.join(TESTS_DIR, n + ".cpp")))


def merged_extras(name: str) -> TestExtras:
    """TEST_EXTRAS plus the sources, include dirs and Widgets CMake gives the test."""
    cm = CMAKE_TESTS.get(name, CMakeTest())
    extras = TEST_EXTRAS.get(name) or default_extras(cm)
    sources = list(cm.sources) + [s for s in extras.sources if s not in cm.sources]
    # the template already has tests and these
    default = {"src/core", "src/ipc", "src/usenet", "src/indexer"}
    includes = list(extras.includes) + [i for i in cm.includes
                                        if i not in extras.includes and i not in default]
    return replace(extras, sources=tuple(dict.fromkeys(sources)), includes=tuple(includes),
                   widgets=extras.widgets or cm.widgets)


def default_extras(cm: CMakeTest) -> TestExtras:
    """For a test without a TEST_EXTRAS entry: the ed3k or BT switches when CMake links them."""
    if "eMule::Ed3k" in cm.libs:
        return ED3K_EXTRAS
    if "eMule::Bt" in cm.libs:
        return TestExtras(includes=BT_INCLUDES, defines=BT_DEFINES, libs=BT_LIBS,
                          deps=(EMULEBT_GUID,), folder="BitTorrent")
    return NO_EXTRAS


def existing_guid(name: str) -> str | None:
    """The GUID a project already has: its vcxproj, else its .sln entry."""
    path = os.path.join(VS_DIR, f"{name}.vcxproj")
    if os.path.exists(path):
        with open(path, encoding="utf-8-sig", errors="replace") as f:
            m = re.search(r"<ProjectGuid>(\{[0-9A-Fa-f-]+\})</ProjectGuid>", f.read())
            if m:
                return m.group(1).upper()
    if os.path.exists(SLN_PATH):
        with open(SLN_PATH, encoding="utf-8-sig", errors="replace") as f:
            m = re.search(r'^Project\("[^"]+"\) = "' + re.escape(name) + r'", "[^"]+", "(\{[^}]+\})"',
                          f.read(), re.M)
            if m:
                return m.group(1).upper()
    return None


def project_guid(name: str) -> str:
    return existing_guid(name) or make_guid(name)


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
    """Extra ClCompile and QtRcc items, then QtMoc items in their own ItemGroup."""
    cpps = tuple(s for s in extras.sources if not s.endswith(".qrc"))
    xml = "".join(f'\n    <ClCompile Include="{vs_path(s)}" />' for s in cpps)
    xml += "".join(f'\n    <QtRcc Include="{vs_path(s)}" />' for s in extras.sources if s.endswith(".qrc"))
    headers = moc_headers(cpps)
    if headers:
        xml += "\n  </ItemGroup>\n  <ItemGroup>"
        for h in headers:
            xml += (f'\n    <QtMoc Include="{vs_path(h)}">'
                    "\n      <QtMocFileName>moc_%(Filename).cpp</QtMocFileName>"
                    "\n    </QtMoc>")
    return xml


def generate_vcxproj(name: str) -> str:
    extras = merged_extras(name)
    return VCXPROJ_TEMPLATE.format(
        name=name,
        guid=project_guid(name),
        includes="".join(vs_path(i) + ";" for i in extras.includes),
        defines=extras.defines,
        libs=extras.libs,
        qt_modules=DEFAULT_QT_MODULES + (";widgets" if extras.widgets else ""),
        sources=sources_xml(extras),
    ).rstrip("\r\n")


def test_deps(name: str) -> list[str]:
    extras = merged_extras(name)
    return [EMULECORE_GUID, EMULEIPC_GUID, EMULEINDEXER_GUID, EMULEUSENET_GUID, *extras.deps]


def folder_guid(folder: str) -> str:
    return make_guid(f"folder-{folder}")


def test_parent(name: str) -> str:
    """Solution folder a test sits in: Tests, or a sub-folder of it."""
    folder = merged_extras(name).folder
    return folder_guid(folder) if folder else TESTS_FOLDER_GUID


def sub_folders(test_names: list[str]) -> list[str]:
    return sorted({f for t in test_names if (f := merged_extras(t).folder)})


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


def prune_sln(sln: str, keep: set[str]) -> tuple[str, list[str]]:
    """Drop test projects (..\\tests\\vs\\*.vcxproj) not in @p keep, with their config and
    nesting lines. Everything else stays."""
    lines = sln.split("\n")
    gone: dict[str, str] = {}
    for line in lines:
        m = re.match(r'Project\("[^"]+"\) = "([^"]+)", "\.\.\\tests\\vs\\[^"]+", "(\{[^}]+\})"', line)
        if m and m.group(1) not in keep:
            gone[m.group(2).upper()] = m.group(1)
    if not gone:
        return sln, []
    out, skip = [], False
    for line in lines:
        if line.startswith("Project("):
            skip = any(line.rstrip().upper().endswith(f'"{g}"') for g in gone)
        if skip:
            if line.strip() == "EndProject":
                skip = False
            continue
        if any(line.strip().upper().startswith(g) for g in gone):
            continue   # config and nesting lines start with the GUID
        out.append(line)
    return "\n".join(out), sorted(gone.values())


def patch_sln(sln: str, test_names: list[str]) -> str:
    """Add the given tests (and their sub-folders) to an existing .sln, leaving everything
    else as is. Entries already present are kept."""
    lines = sln.split("\n")

    def present(guid: str, name: str | None = None) -> bool:
        return any(line.startswith("Project(") and (line.rstrip().upper().endswith(f'"{guid}"')
                   or (name and f'= "{name}", ' in line)) for line in lines)

    new_projects, new_configs, new_nested = [], [], []
    for folder in sub_folders(test_names):
        guid = folder_guid(folder)
        if not present(guid):
            new_projects += project_lines(folder, folder, guid, [], VS_FOLDER_TYPE)
            new_nested.append(f"\t\t{guid} = {TESTS_FOLDER_GUID}")
    for tname in test_names:
        guid = project_guid(tname)
        if present(guid, tname):
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
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", nargs="+", metavar="TEST",
                        help="just these tests; the .sln only gains them")
    parser.add_argument("--check", action="store_true",
                        help="list what would change, write nothing; exit 1 on drift")
    args = parser.parse_args()

    all_tests = discover_tests()
    test_names = all_tests
    if args.only:
        unknown = sorted(set(args.only) - set(all_tests))
        if unknown:
            parser.error(f"not an emule_add_test() in tests/CMakeLists.txt: {', '.join(unknown)}")
        test_names = sorted(args.only)
    print(f"{len(test_names)} tests from tests/CMakeLists.txt")

    changed = []
    for name in test_names:
        path = os.path.join(VS_DIR, f"{name}.vcxproj")
        content = generate_vcxproj(name)
        old = None
        if os.path.exists(path):
            with open(path, encoding="utf-8-sig", newline="") as f:
                old = f.read().replace("\r\n", "\n").rstrip("\n")
        if old == content.rstrip("\n"):
            continue
        changed.append(name)
        print(f"  {'new' if old is None else 'changed'}: {name}")
        if not args.check:
            os.makedirs(VS_DIR, exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\r\n") as f:
                f.write(content)

    # utf-8-sig: keep the BOM VS writes; CRLFs kept as read
    with open(SLN_PATH, encoding="utf-8-sig", newline="") as f:
        sln = f.read()
    crlf = "\r\n" in sln
    new_sln = sln.replace("\r\n", "\n")
    removed = []
    if not args.only:
        new_sln, removed = prune_sln(new_sln, set(all_tests))
    new_sln = patch_sln(new_sln, test_names)
    if crlf:
        new_sln = new_sln.replace("\n", "\r\n")
    for name in removed:
        print(f"  .sln drops: {name}")
    sln_changed = new_sln != sln
    if sln_changed:
        print(f"  .sln changed")
        if not args.check:
            with open(SLN_PATH, "w", encoding="utf-8-sig", newline="") as f:
                f.write(new_sln)

    verb = "would change" if args.check else "written"
    print(f"{len(changed)} vcxproj {verb}; .sln {'changed' if sln_changed else 'unchanged'}")
    if args.check and (changed or sln_changed):
        sys.exit(1)


if __name__ == "__main__":
    main()
