#!/usr/bin/env python3
"""Generate per-test Visual Studio vcxproj files and update the solution."""

import os
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
        <PlatformToolset>v143</PlatformToolset>
        <OutputDirectory>release\\</OutputDirectory>
        <ATLMinimizesCRunTimeLibraryUsage>false</ATLMinimizesCRunTimeLibraryUsage>
        <CharacterSet>NotSet</CharacterSet>
        <ConfigurationType>Application</ConfigurationType>
        <IntermediateDirectory>release\\</IntermediateDirectory>
        <PrimaryOutput>{name}</PrimaryOutput>
      </PropertyGroup>
      <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
        <PlatformToolset>v143</PlatformToolset>
        <OutputDirectory>debug\\</OutputDirectory>
        <ATLMinimizesCRunTimeLibraryUsage>false</ATLMinimizesCRunTimeLibraryUsage>
        <CharacterSet>NotSet</CharacterSet>
        <ConfigurationType>Application</ConfigurationType>
        <IntermediateDirectory>debug\\</IntermediateDirectory>
        <PrimaryOutput>{name}</PrimaryOutput>
      </PropertyGroup>
      <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
      <Import Project="$(QtMsBuild)\\qt_defaults.props" Condition="Exists('$(QtMsBuild)\\qt_defaults.props')" />
      <PropertyGroup Label="QtSettings" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
        <QtInstall>6.8.3_msvc2022_64</QtInstall>
        <QtModules>core;network;test;gui;multimedia;websockets;httpserver;concurrent</QtModules>
      </PropertyGroup>
      <PropertyGroup Label="QtSettings" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
        <QtInstall>6.8.3_msvc2022_64</QtInstall>
        <QtModules>core;network;test;gui;multimedia;websockets;httpserver;concurrent</QtModules>
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
          <AdditionalIncludeDirectories>..;..\\..\\src\\core;..\\..\\src\\ipc;..\\..\\src\\vcpkg_installed\\x64-windows\\include;..\\..\\src\\vcpkg_installed\\x64-windows\\include\\miniupnpc;release;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
          <AdditionalOptions>-Zc:rvalueCast -Zc:inline -Zc:strictStrings -Zc:throwingNew -permissive- -Zc:__cplusplus -Zc:externConstexpr -utf-8 -w34100 -w34189 -w44996 -w44456 -w44457 -w44458 %(AdditionalOptions)</AdditionalOptions>
          <AssemblerListingLocation>release\\</AssemblerListingLocation>
          <BrowseInformation>false</BrowseInformation>
          <DebugInformationFormat>None</DebugInformationFormat>
          <DisableSpecificWarnings>4577;4467;%(DisableSpecificWarnings)</DisableSpecificWarnings>
          <ExceptionHandling>Sync</ExceptionHandling>
          <LanguageStandard>stdcpplatest</LanguageStandard>
          <ObjectFileName>release\\</ObjectFileName>
          <Optimization>MaxSpeed</Optimization>
          <PreprocessorDefinitions>_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;NDEBUG;QT_NO_DEBUG;EMULE_TEST_DATA_DIR=\\"..\\\\..\\\\tests\\\\data\\\\\\";EMULE_PROJECT_DATA_DIR=\\"..\\\\..\\\\data\\\\\\";%(PreprocessorDefinitions)</PreprocessorDefinitions>
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
          <AdditionalDependencies>emulecore.lib;emuleipc.lib;libssl.lib;libcrypto.lib;zlib.lib;miniupnpc.lib;yaml-cpp.lib;archive.lib;ws2_32.lib;iphlpapi.lib;$(QTDIR)\\lib\\Qt6HttpServer.lib;%(AdditionalDependencies)</AdditionalDependencies>
          <AdditionalLibraryDirectories>..\\..\\src\\core\\$(Configuration);..\\..\\src\\ipc\\$(Configuration);..\\..\\src\\vcpkg_installed\\x64-windows\\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
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
          <PreprocessorDefinitions>_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;NDEBUG;QT_NO_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
        </ResourceCompile>
        <QtMoc>
          <CompilerFlavor>msvc</CompilerFlavor>
          <Include>./$(Configuration)/moc_predefs.h</Include>
          <ExecutionDescription>Moc'ing %(Identity)...</ExecutionDescription>
          <DynamicSource>output</DynamicSource>
          <QtMocDir>$(Configuration)</QtMocDir>
          <QtMocFileName>moc_%(Filename).cpp</QtMocFileName>
        </QtMoc>
      </ItemDefinitionGroup>
      <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
        <ClCompile>
          <AdditionalIncludeDirectories>..;..\\..\\src\\core;..\\..\\src\\ipc;..\\..\\src\\vcpkg_installed\\x64-windows\\include;..\\..\\src\\vcpkg_installed\\x64-windows\\include\\miniupnpc;debug;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
          <AdditionalOptions>-Zc:rvalueCast -Zc:inline -Zc:strictStrings -Zc:throwingNew -permissive- -Zc:__cplusplus -Zc:externConstexpr -utf-8 -w34100 -w34189 -w44996 -w44456 -w44457 -w44458 %(AdditionalOptions)</AdditionalOptions>
          <AssemblerListingLocation>debug\\</AssemblerListingLocation>
          <BrowseInformation>false</BrowseInformation>
          <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
          <DisableSpecificWarnings>4577;4467;%(DisableSpecificWarnings)</DisableSpecificWarnings>
          <ExceptionHandling>Sync</ExceptionHandling>
          <LanguageStandard>stdcpplatest</LanguageStandard>
          <ObjectFileName>debug\\</ObjectFileName>
          <Optimization>Disabled</Optimization>
          <PreprocessorDefinitions>_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;EMULE_TEST_DATA_DIR=\\"..\\\\..\\\\tests\\\\data\\\\\\";EMULE_PROJECT_DATA_DIR=\\"..\\\\..\\\\data\\\\\\";_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
          <PreprocessToFile>false</PreprocessToFile>
          <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
          <SuppressStartupBanner>true</SuppressStartupBanner>
          <TreatWChar_tAsBuiltInType>true</TreatWChar_tAsBuiltInType>
          <UseFullPaths>false</UseFullPaths>
          <WarningLevel>Level3</WarningLevel>
          <MultiProcessorCompilation>true</MultiProcessorCompilation>
        </ClCompile>
        <Link>
          <AdditionalDependencies>emulecore.lib;emuleipc.lib;libssl.lib;libcrypto.lib;zlib.lib;miniupnpc.lib;yaml-cpp.lib;archive.lib;ws2_32.lib;iphlpapi.lib;$(QTDIR)\\lib\\Qt6HttpServerd.lib;%(AdditionalDependencies)</AdditionalDependencies>
          <AdditionalLibraryDirectories>..\\..\\src\\core\\$(Configuration);..\\..\\src\\ipc\\$(Configuration);..\\..\\src\\vcpkg_installed\\x64-windows\\debug\\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
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
          <PreprocessorDefinitions>_CONSOLE;UNICODE;_UNICODE;WIN32;_ENABLE_EXTENDED_ALIGNED_STORAGE;WIN64;NOMINMAX;WIN32_LEAN_AND_MEAN;_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
        </ResourceCompile>
        <QtMoc>
          <CompilerFlavor>msvc</CompilerFlavor>
          <Include>./$(Configuration)/moc_predefs.h</Include>
          <ExecutionDescription>Moc'ing %(Identity)...</ExecutionDescription>
          <DynamicSource>output</DynamicSource>
          <QtMocDir>$(Configuration)</QtMocDir>
          <QtMocFileName>moc_%(Filename).cpp</QtMocFileName>
        </QtMoc>
      </ItemDefinitionGroup>
      <ItemGroup>
        <ClCompile Include="..\\{name}.cpp" />
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


def discover_tests() -> list[str]:
    """Find all tst_*.cpp files and return sorted test names."""
    pattern = os.path.join(TESTS_DIR, "tst_*.cpp")
    names = []
    for path in glob.glob(pattern):
        name = os.path.splitext(os.path.basename(path))[0]
        names.append(name)
    return sorted(names)


def generate_vcxproj(name: str) -> str:
    guid = make_guid(name)
    return VCXPROJ_TEMPLATE.format(name=name, guid=guid)


def generate_sln(test_names: list[str]) -> str:
    """Generate a complete .sln file with all projects."""
    lines = []
    lines.append("Microsoft Visual Studio Solution File, Format Version 12.00")
    lines.append("# Visual Studio Version 17")

    # Existing projects (emulecore, emuleipc, emulecored, emuleqt)
    existing_projects = [
        ("emulecore", "core\\emulecore.vcxproj", EMULECORE_GUID, []),
        ("emuleipc", "ipc\\emuleipc.vcxproj", EMULEIPC_GUID, []),
        ("emulecored", "daemon\\emulecored.vcxproj", "{4ECEE8D7-03DE-3DE1-A2C6-C56B3BD68584}",
         [EMULECORE_GUID, EMULEIPC_GUID]),
        ("emuleqt", "gui\\emuleqt.vcxproj", "{CEA0CD5B-F0C0-3EB9-8BEA-C9CEDA11C838}",
         [EMULECORE_GUID, EMULEIPC_GUID]),
    ]

    for name, path, guid, deps in existing_projects:
        lines.append(f'Project("{VS_CPP_PROJECT_TYPE}") = "{name}", "{path}", "{guid}"')
        if deps:
            lines.append("\tProjectSection(ProjectDependencies) = postProject")
            for d in deps:
                lines.append(f"\t\t{d} = {d}")
            lines.append("\tEndProjectSection")
        lines.append("EndProject")

    # Tests solution folder
    lines.append(f'Project("{VS_FOLDER_TYPE}") = "Tests", "Tests", "{TESTS_FOLDER_GUID}"')
    lines.append("EndProject")

    # Test projects
    test_guids = []
    for tname in test_names:
        guid = make_guid(tname)
        test_guids.append((tname, guid))
        rel_path = f"..\\tests\\vs\\{tname}.vcxproj"
        lines.append(f'Project("{VS_CPP_PROJECT_TYPE}") = "{tname}", "{rel_path}", "{guid}"')
        lines.append("\tProjectSection(ProjectDependencies) = postProject")
        lines.append(f"\t\t{EMULECORE_GUID} = {EMULECORE_GUID}")
        lines.append(f"\t\t{EMULEIPC_GUID} = {EMULEIPC_GUID}")
        lines.append("\tEndProjectSection")
        lines.append("EndProject")

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
        lines.append(f"\t\t{guid}.Debug|x64.ActiveCfg = Debug|x64")
        lines.append(f"\t\t{guid}.Debug|x64.Build.0 = Debug|x64")
        lines.append(f"\t\t{guid}.Release|x64.ActiveCfg = Release|x64")
        lines.append(f"\t\t{guid}.Release|x64.Build.0 = Release|x64")
    lines.append("\tEndGlobalSection")

    # Nested projects - put tests under Tests folder
    lines.append("\tGlobalSection(NestedProjects) = preSolution")
    for _, guid in test_guids:
        lines.append(f"\t\t{guid} = {TESTS_FOLDER_GUID}")
    lines.append("\tEndGlobalSection")

    lines.append("\tGlobalSection(ExtensibilityGlobals) = postSolution")
    lines.append("\tEndGlobalSection")
    lines.append("\tGlobalSection(ExtensibilityAddIns) = postSolution")
    lines.append("\tEndGlobalSection")
    lines.append("EndGlobal")

    return "\r\n".join(lines) + "\r\n"


def main():
    test_names = discover_tests()
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
    sln_content = generate_sln(test_names)
    with open(SLN_PATH, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(sln_content)

    print(f"Updated {SLN_PATH}")


if __name__ == "__main__":
    main()
