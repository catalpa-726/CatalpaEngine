#include "catpch.h"

#include "TestRunner.h"
#include "Project/ProjectConfig.h"
#include "Application/EditorProject.h"
#include "Core/Serialization/JsonArchive.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/Id/StableIdGenerator.h"

namespace Catalpa::Tests {
	namespace {
		/* 每次用系统随机 ID 原子取得独占临时目录；测试绝不读写用户项目或原素材。 */
		struct TemporaryProject {
			std::filesystem::path root;
			std::filesystem::path canonicalRoot;
			bool owned = false;

			bool Create(std::string& error) {
				StableId id;
				if (!GenerateStableId(id, error))
					return false;
				std::error_code code;
				const auto temp = std::filesystem::temp_directory_path(code);
				if (code) {
					error = code.message();
					return false;
				}
				root = temp / ("CatalpaProjectTest-" + id.ToString());
				owned = std::filesystem::create_directory(root, code);
				if (!owned || code) {
					error = "cannot create exclusive test directory";
					return false;
				}
				canonicalRoot = std::filesystem::canonical(root, code);
				if (code) {
					error = code.message();
					return false;
				}
				for (const auto* child : {"Content", "Raw"}) {
					if (!std::filesystem::create_directory(root / child, code) || code) {
						error = "cannot create test fixture directory";
						return false;
					}
				}
				return true;
			}

			~TemporaryProject() {
				if (!owned || canonicalRoot.empty())
					return;
				std::error_code code;
				/* 若测试根被替换成别处的链接，不递归清理未知目标。 */
				const auto current = std::filesystem::canonical(root, code);
				if (!code && current == canonicalRoot)
					std::filesystem::remove_all(root, code);
			}
		};

		std::string ProjectText(std::string_view derived = "DerivedDataCache") {
			return std::string(R"({
                "schemaVersion":1,"id":"11111111-2222-4333-8444-555555555555","name":"Fixture",
                "contentRoot":"Content","externalSourceRoots":{"archive":"Raw"},
                "derivedDataRoot":")") +
			    std::string(derived) + R"(","cookedRoot":"Cooked",
                "metadata":"asset_metadata.json","buildProfiles":"profiles.json","buildProfile":"desktop-vulkan",
                "targetPlatform":"windows","buildConfiguration":"Debug","startupScene":null,"modules":[]})";
		}
		constexpr std::string_view ProfileText = R"({"schemaVersion":1,"profiles":{"desktop-vulkan":{
            "platform":"windows","architecture":"x86_64","shaderTarget":"spirv",
            "shaderProfile":"spirv_1_5","targetEnvironment":"vulkan1.3"}}})";
		constexpr std::string_view MetadataText = R"({"schemaVersion":1,"assets":[{
            "id":"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee","type":"mesh","path":"content:/mesh.raw","kind":"source",
            "version":18446744073709551615,"dependencies":["12345678-1234-4234-8234-123456789abc"],
            "source":{"uri":"test-fixture:mesh","license":"test-only"},"importSettings":{"unchanged":true}}]})";

		bool Store(const std::filesystem::path& file, std::string_view text, TestContext& context) {
			std::string error;
			const bool stored = AtomicFileWriter::Write(file, text, error);
			context.Check(stored, std::string("isolated fixture write: ") + error);
			return stored;
		}
	}

	void RunProjectConfigTests(TestContext& context) {
		std::unique_ptr<ArchiveValue> value;
		ArchiveError diagnostic;
		context.Check(!JsonArchive::Parse("{", value, diagnostic) && !value,
		    "failed initial parse leaves the archive owner empty");
		const bool parsed =
		    JsonArchive::Parse(R"({"schemaVersion":1,"count":18446744073709551615})", value, diagnostic);
		context.Check(parsed && value, "JSON keeps full unsigned 64-bit range");
		if (!parsed || !value)
			return;
		const auto* publishedValue = value.get();
		uint64_t count = 0;
		const auto* countValue = value->Find("count");
		context.Check(countValue && countValue->ReadUnsigned(count) && count == std::numeric_limits<uint64_t>::max(),
		    "archive does not round a 64-bit integer through double");
		context.Check(!JsonArchive::Parse(R"({"a":1,"a":2})", value, diagnostic) && diagnostic.path == "/a",
		    "duplicate JSON key rejected with field path");
		context.Check(value.get() == publishedValue && value->Find("count") != nullptr,
		    "parse failure preserves the same archive owner and prior value tree");
		context.Check(!JsonArchive::Parse(R"({"outer":{"a/b":1,"a/b":2}})", value, diagnostic) &&
		        diagnostic.path == "/outer/a~1b",
		    "nested duplicate uses escaped JSON pointer");
		ArchiveLimits limits;
		limits.maxDepth = 2;
		context.Check(!JsonArchive::Parse("[[[]]]", value, diagnostic, limits),
		    "nesting beyond limit rejected before unbounded recursion");
		limits = {};
		limits.maxBytes = 3;
		context.Check(!JsonArchive::Parse("null", value, diagnostic, limits), "input byte bound enforced");
		limits = {};
		limits.maxNodes = 2;
		context.Check(!JsonArchive::Parse("[1,2]", value, diagnostic, limits), "node count bound enforced");
		limits = {};
		limits.maxCollectionEntries = 1;
		context.Check(!JsonArchive::Parse("[1,2]", value, diagnostic, limits), "collection entry bound enforced");
		context.Check(value.get() == publishedValue && value->Find("count") != nullptr,
		    "bounded parse failures leave the published archive unchanged");
		context.Check(JsonArchive::Parse("null", value, diagnostic) && value &&
		        std::holds_alternative<std::nullptr_t>(value->data),
		    "successful parsing publishes a different root alternative");
		context.Check(JsonArchive::Parse(R"({"schemaVersion":1.0})", value, diagnostic) && value &&
		        !RequireArchiveVersion(*value, 1, diagnostic),
		    "floating schema version is not silently accepted as integer");
		context.Check(JsonArchive::Parse(R"({"schemaVersion":2})", value, diagnostic) && value &&
		        !RequireArchiveVersion(*value, 1, diagnostic),
		    "future schema version explicitly rejected");
		ArchiveValue nonfinite;
		nonfinite.data = std::numeric_limits<double>::infinity();
		std::string encoded = "preserved";
		context.Check(!JsonArchive::Write(nonfinite, encoded, diagnostic) && encoded == "preserved",
		    "non-finite output fails without replacing earlier output");
		ArchiveValue escaped;
		escaped.data = std::string(5, '\0');
		limits = {};
		limits.maxBytes = 8;
		context.Check(!JsonArchive::Write(escaped, encoded, diagnostic, limits) && encoded == "preserved",
		    "escaped output obeys hard byte limit while serializing");

		TemporaryProject fixture;
		std::string error;
		const bool created = fixture.Create(error);
		context.Check(created, std::string("exclusive temporary project: ") + error);
		if (!created)
			return;
		const auto projectFile = fixture.root / "Fixture.catalpaproj";
		const auto metadataFile = fixture.root / "Content" / "asset_metadata.json";
		if (!Store(projectFile, ProjectText(), context) ||
		    !Store(fixture.root / "profiles.json", ProfileText, context) ||
		    !Store(metadataFile, MetadataText, context) ||
		    !Store(fixture.root / "Content" / "mesh.raw", "retained raw fixture", context))
			return;

		std::unique_ptr<Tools::ProjectSession> session;
		context.Check(!Tools::LoadProject("Fixture.catalpaproj", session, error) && !session,
		    "failed initial load leaves the session owner empty");
		const bool loaded = Tools::LoadProject(projectFile, session, error);
		context.Check(loaded && session,
		    std::string("explicit absolute project loads configuration, mounts and registry: ") + error);
		if (!loaded || !session)
			return;
		const auto* publishedSession = session.get();
		const auto* publishedMetadata = session->metadata.Find("assets");
		const auto* publishedProfile = session->selectedBuildProfile.Find("platform");
		context.Check(session->config.contentRoot == std::filesystem::weakly_canonical(fixture.root / "Content"),
		    "content path resolves against project directory");
		context.Check(session->assets.GetRecords().size() == 1 &&
		        session->assets.GetRecords().front().version == std::numeric_limits<uint64_t>::max(),
		    "project registers stable identity and exact content version");
		context.Check(session->assets.GetMissingDependencies().size() == 1 && !session->diagnostics.empty(),
		    "missing dependency remains a resolvable diagnostic rather than losing registration");
		context.Check(!std::filesystem::exists(fixture.root / "DerivedDataCache") &&
		        !std::filesystem::exists(fixture.root / "Cooked"),
		    "read-only project load does not create output directories");
		VirtualPath assetPath;
		context.Check(
		    VirtualPath::Parse("content:/mesh.raw", assetPath, error) && session->assets.Find(assetPath) != nullptr,
		    "registered locator resolves to the same asset record");
		std::vector<std::byte> raw;
		context.Check(session->files.ReadFile(assetPath, 128, raw, error) && raw.size() == 20,
		    "project content is readable through shared VFS without changing source bytes");

		context.Check(!Tools::LoadProject("Fixture.catalpaproj", session, error) && session.get() == publishedSession &&
		        session->assets.GetRecords().size() == 1,
		    "relative project input rejected without replacing session");
		if (!Store(projectFile, ProjectText("Content/DerivedDataCache"), context))
			return;
		context.Check(!Tools::LoadProject(projectFile, session, error) && error.find("overlap") != std::string::npos,
		    "derived output inside source content is rejected");
		context.Check(session.get() == publishedSession && session->config.name == "Fixture" &&
		        session->assets.GetRecords().size() == 1 && publishedMetadata && publishedProfile &&
		        session->metadata.Find("assets") == publishedMetadata &&
		        session->selectedBuildProfile.Find("platform") == publishedProfile &&
		        session->files.ReadFile(assetPath, 128, raw, error) && raw.size() == 20,
		    "failed replacement retains the same session, archive trees, registry and working mounts");
		if (!Store(projectFile, ProjectText("Raw. /Derived"), context))
			return;
		context.Check(!Tools::LoadProject(projectFile, session, error),
		    "Windows trailing-dot/space aliases rejected before path canonicalization");
		if (!Store(projectFile, ProjectText(), context))
			return;
		auto saved = session->config;
		saved.name = "Explicit save";
		context.Check(Tools::SaveProjectConfig(saved, error), std::string("explicit atomic author save: ") + error);
		std::unique_ptr<Tools::ProjectSession> reopened;
		context.Check(
		    Tools::LoadProject(projectFile, reopened, error) && reopened && reopened->config.name == "Explicit save",
		    "saved author configuration can be read by the same shared contract");
		const bool replaced = Tools::LoadProject(projectFile, session, error);
		context.Check(replaced && session && session->config.name == "Explicit save" &&
		        session->assets.GetRecords().size() == 1 && session->metadata.Find("assets") &&
		        session->selectedBuildProfile.Find("platform") && session->files.ReadFile(assetPath, 128, raw, error) &&
		        raw.size() == 20,
		    "successful replacement publishes configuration, trees, registry and mounts as one session");
		auto aliasedSave = saved;
		aliasedSave.projectFile = fixture.root / "Content" / ".." / "Fixture.catalpaproj";
		aliasedSave.name = "Rejected replacement";
		context.Check(!Tools::SaveProjectConfig(aliasedSave, error),
		    "author save rejects a noncanonical target instead of following its alias");
		context.Check(
		    Tools::LoadProject(projectFile, reopened, error) && reopened && reopened->config.name == "Explicit save",
		    "rejected alias save leaves the existing author document unchanged");
		auto sourceSave = saved;
		sourceSave.projectFile = fixture.root / "Raw" / "Forbidden.catalpaproj";
		context.Check(!Tools::SaveProjectConfig(sourceSave, error) && !std::filesystem::exists(sourceSave.projectFile),
		    "explicit save cannot overwrite files inside a declared read-only source root");

		Editor::EditorProject editor;
		context.Check(
		    editor.Load(projectFile, error) && editor.GetSession() != nullptr, "editor consumes shared project loader");
		const auto* previousSession = editor.GetSession();
		if (!Store(metadataFile, R"({"schemaVersion":1,"assets":[],"unknown":1})", context))
			return;
		context.Check(!editor.Load(projectFile, error) && editor.GetSession() == previousSession,
		    "invalid metadata preserves the previous editor session");
		if (!Store(metadataFile,
		        R"({"schemaVersion":1,"assets":[{"id":"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee","type":"mesh","path":"undeclared:/mesh","kind":"source","version":1}]})",
		        context))
			return;
		context.Check(!Tools::LoadProject(projectFile, reopened, error), "undeclared source mounts are rejected");
		if (!Store(metadataFile, MetadataText, context))
			return;
		context.Check(editor.Save(error), "editor explicit save delegates to shared atomic config writer");
		editor.Close();
		context.Check(editor.GetSession() == nullptr && !editor.Save(error),
		    "closed editor project releases session and refuses save");
	}
}
