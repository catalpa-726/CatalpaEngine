#include "catpch.h"
#include "Project/ScriptIdeProject.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Core/IO/AtomicFileWriter.h"

namespace Catalpa::Tools {
	namespace fs = std::filesystem;
	namespace {
		/** @brief 文件名在 MSBuild 项目中同时经过 XML 解析与属性、通配符展开。 */
		std::string ProjectPath(const fs::path& path) {
			std::string escaped;
			for (char ch : AssetPathText(path)) {
				if (std::string_view("%$@;*?'()").find(ch) != std::string_view::npos) {
					constexpr char hex[] = "0123456789ABCDEF";
					const auto byte = static_cast<unsigned char>(ch);
					escaped += '%';
					escaped += hex[byte >> 4];
					escaped += hex[byte & 15];
				} else if (ch == '&') {
					escaped += "&amp;";
				} else if (ch == '"') {
					escaped += "&quot;";
				} else if (ch == '<') {
					escaped += "&lt;";
				} else if (ch == '>') {
					escaped += "&gt;";
				} else {
					escaped += ch;
				}
			}
			return escaped;
		}
	}

	bool PrepareScriptIdeProject(const ProjectConfig& project, const fs::path& script, const fs::path& apiAssembly,
	    fs::path& ideProject, std::string& error) try {
		const auto source = fs::canonical(script);
		if (!fs::is_regular_file(source) || AssetSourceExtension(source) != ".cs") {
			error = "Select a C# source file in the project";
			return false;
		}
		const auto api = fs::canonical(apiAssembly);
		const auto directory = project.derivedDataRoot / "ScriptIDE" / AssetBuildIdentity(AssetPathText(source)).ToString();
		fs::create_directories(directory);
		const auto target = directory / "Script.csproj";
		const auto xml = std::string(R"(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net48</TargetFramework>
    <LangVersion>7.3</LangVersion>
    <EnableDefaultItems>false</EnableDefaultItems>
    <DisableImplicitFrameworkReferences>true</DisableImplicitFrameworkReferences>
    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="System" />
    <Reference Include="System.Core" />
    <Reference Include="System.Numerics" />
    <Reference Include="Catalpa.Scripting">
      <HintPath>)") + ProjectPath(api) + R"(</HintPath>
    </Reference>
    <Compile Include=")" + ProjectPath(source) + R"(" Link=")" + ProjectPath(source.filename()) + R"(" />
  </ItemGroup>
</Project>
)";
		std::ifstream existing(target, std::ios::binary);
		const bool current = existing && std::string(std::istreambuf_iterator<char>(existing), {}) == xml;
		existing.close();
		if (!current && !AtomicFileWriter::Write(target, xml, error))
			return false;
		ideProject = target;
		error.clear();
		return true;
	} catch (const std::exception& failure) {
		error = failure.what();
		return false;
	}
}
