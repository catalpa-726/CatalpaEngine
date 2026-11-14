#include "catpch.h"

#include "TestRunner.h"
#include "UI/UIRuntime.h"
#include "UI/UIInputRouter.h"
#include "UI/UIRuntimeConfig.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Runtime/Input/InputActions.h"
#include "UI/FontProfile.h"
#include "Assets/AssetTestFixture.h"

namespace Catalpa::Tests {
	void RunUIDomainTests(TestContext& test) {
		std::string error;
		test.Check(IsUIRelativePath("styles/hud.rcss") && !IsUIRelativePath("../outside.rcss") &&
		        !IsUIRelativePath("C:/outside.rml"),
		    "UI bundle paths reject traversal and absolute filesystem aliases");
		UIDocumentAsset asset;
		asset.entry = "hud.rml";
		asset.files[asset.entry] = "<rml><head><style>body { display: block; width: 100%; height: 100%; margin: 0; } "
		                           "#box { display: block; width: 100px; height: 80px; background-color: #40a08080; }"
		                           "</style></head><body><div id=\"box\"></div></body></rml>";
		test.Check(ValidateUIDocument(asset, error), "UI bundle validates its real entry text");
		std::vector<std::byte> encoded;
		test.Check(
		    UIDocumentLoader::Encode(asset, encoded, error), "UI bundle has a real cooked binary representation");
		auto bad = asset;
		bad.entry = "absent.rml";
		const auto old = encoded;
		test.Check(!UIDocumentLoader::Encode(bad, encoded, error) && encoded == old,
		    "Missing UI entry fails without publishing a partial payload");
		AssetVersionRef<UIDocumentAsset> fixed;
		AssetVersionRef<UIDocumentAsset>::TryCreate(
		    AssetId::FromWords(92, 1), 1, std::make_shared<const UIDocumentAsset>(asset), fixed, error);
		/* 在独立 UI 上下文中布局和绘制，随后检查绘制快照是否持有自己的几何数据。 */
		UIRuntime runtime;
		const bool started = runtime.Initialize(fixed, 320, 200, error);
		test.Check(started, "Actual RmlUi initializes a file-backed bundled document without window or GPU");
		if (started) {
			std::shared_ptr<const UIDrawData> draw;
			const bool rendered = runtime.Advance(0, error) && runtime.Render(draw, error);
			test.Check(rendered && draw && draw->width == 320 && draw->height == 200 && !draw->batches.empty() &&
			        !draw->vertices.empty() && !draw->indices.empty(),
			    "Actual RmlUi layout produces owned indexed draw geometry");
			if (draw && !draw->vertices.empty()) {
				const auto found = std::find_if(draw->vertices.begin(), draw->vertices.end(),
				    [](const auto& v) { return v.color[3] > .49f && v.color[3] < .51f; });
				test.Check(found != draw->vertices.end() && found->color[1] < found->color[3],
				    "UI vertex colors are decoded into linear premultiplied RGBA");
			}
			const auto previous = draw;
			test.Check(!runtime.SetValue("absent", "value", error) && !runtime.Advance(-1, error),
			    "UI bindings and clocks fail explicitly on invalid input");
			test.Check(!runtime.SetDensity(8, error) && runtime.SetDensity(1.5f, error),
			    "UI density rejects invalid scale and admits DP scaling");
			UIInputEvent text;
			text.type = UIInputEvent::Type::Text;
			text.codepoint = 0xD800;
			bool consumed = false;
			test.Check(!runtime.Input(text, consumed, error), "UI text rejects an isolated Unicode surrogate");
			runtime.Shutdown();
			test.Check(previous && !previous->vertices.empty() && !previous->indices.empty(),
			    "Draw snapshots remain owned after the RmlUi context is removed");
		}
		/* 把原生指针和文本事件转换到 UI 坐标系，检查键盘捕获如何影响玩法动作。 */
		UIInputRouter router;
		UIInputEvent event;
		const MouseMovedEvent move(30, 50);
		test.Check(router.Translate(move, 10, 20, 2, 3, event) && event.x == 40 && event.y == 90,
		    "UI pointer coordinates map into target pixels exactly once");
		const KeyTypedEvent typed(0x1F600);
		test.Check(router.Translate(typed, 0, 0, 1, 1, event) && event.codepoint == 0x1F600,
		    "UI routing retains supplementary-plane Unicode scalars");
		UIRuntimeConfig config;
		test.Check(!ReadUIRuntimeConfig("{\"schemaVersion\":1,\"density\":10,\"maxDocuments\":1}", config, error) &&
		        config.density == 1,
		    "Invalid UI runtime config preserves the previous configuration");

		InputActions actions;
		actions.Configure(MakeSandboxActionMap());
		actions.OnCapturedEvent(KeyPressedEvent(Key::W), true);
		actions.OnCapturedEvent(KeyReleasedEvent(Key::W), true);
		auto input = actions.BeginFrame();
		const auto* forward = input.Find("move.forward");
		test.Check(forward && !forward->held && !forward->pressed && !forward->released &&
		        actions.GetPhysicalSnapshot().keyPressed[Key::W] && actions.GetPhysicalSnapshot().keyReleased[Key::W],
		    "UI quick taps preserve raw physical edges while suppressing gameplay press/release ghosts");
		actions.OnCapturedEvent(KeyPressedEvent(Key::W), true);
		actions.BeginFrame();
		actions.OnEvent(KeyPressedEvent(Key::W, true));
		input = actions.BeginFrame();
		test.Check(!input.Find("move.forward")->held, "A UI-owned held key cannot resume gameplay through key repeat");
		actions.OnEvent(KeyReleasedEvent(Key::W));
		actions.OnEvent(KeyPressedEvent(Key::W));
		input = actions.BeginFrame();
		test.Check(
		    input.Find("move.forward")->held, "A fresh physical press after release returns the key to gameplay");
		actions.Reset();
		actions.OnEvent(MouseButtonPressedEvent(1));
		actions.OnEvent(MouseMovedEvent(10, 10));
		actions.OnCapturedEvent(MouseMovedEvent(20, 10));
		input = actions.BeginFrame();
		test.Check(input.Find("look.yaw")->value == 0 && actions.GetPhysicalSnapshot().deltaX == 10,
		    "UI pointer consumption retains the raw motion source");
		actions.OnEvent(MouseMovedEvent(21, 10));
		input = actions.BeginFrame();
		test.Check(
		    input.Find("look.yaw")->value == 1, "Leaving UI resumes pointer motion without a stale-position jump");
		FontProfile fontProfile;
		test.Check(ReadFontProfile("{\"schemaVersion\":1,\"previewText\":\"Inter\",\"previewPixelSize\":24,"
		                           "\"previewWidth\":640,\"previewHeight\":256}",
		               fontProfile, error) &&
		        fontProfile.previewPixelSize == 24,
		    "Font preview consumes the bounded deployed profile schema");

		/* 通过可编辑控件验证预编辑、提交、长度边界与外部赋值的相互作用。 */
		UIDocumentAsset editable;
		editable.entry = "input.rml";
		editable.files[editable.entry] = "<rml><head></head><body><input id=\"edit\" type=\"text\" maxlength=\"2\" />"
		                                 "<input id=\"other\" type=\"text\" /></body></rml>";
		AssetVersionRef<UIDocumentAsset> inputAsset;
		AssetVersionRef<UIDocumentAsset>::TryCreate(AssetId::FromWords(92, 2), 1,
		    std::make_shared<const UIDocumentAsset>(std::move(editable)), inputAsset, error);
		UIRuntime inputRuntime;
		if (inputRuntime.Initialize(inputAsset, 320, 200, error) && inputRuntime.Advance(0, error) &&
		    inputRuntime.Focus("edit", error)) {
			UIInputEvent composition;
			composition.type = UIInputEvent::Type::Composition;
			composition.composition.phase = TextComposition::Phase::Start;
			bool consumed = false;
			test.Check(inputRuntime.TextInput().active && inputRuntime.Input(composition, consumed, error) && consumed,
			    "RmlUi text focus activates the native text-input bridge");
			composition.composition = {TextComposition::Phase::Update, "中😀", 2};
			std::string value;
			test.Check(inputRuntime.Input(composition, consumed, error) &&
			        inputRuntime.GetValue("edit", value, error) && value == "中😀",
			    "IME preedit uses Unicode scalar cursor positions without flattening supplementary "
			    "characters");
			composition.composition = {TextComposition::Phase::Cancel};
			test.Check(inputRuntime.Input(composition, consumed, error) &&
			        inputRuntime.GetValue("edit", value, error) && value.empty(),
			    "Cancelling native composition restores the original authored text");
			composition.composition = {TextComposition::Phase::Commit, "中😀", 2};
			test.Check(inputRuntime.Input(composition, consumed, error) &&
			        inputRuntime.GetValue("edit", value, error) && value == "中😀",
			    "IME commit applies the complete Unicode composition exactly once");
			composition.composition = {TextComposition::Phase::Commit, "x", 1};
			test.Check(!inputRuntime.Input(composition, consumed, error) &&
			        inputRuntime.GetValue("edit", value, error) && value == "中😀",
			    "IME commit exceeding maxlength fails explicitly and retains the previous text");
			composition.composition = {TextComposition::Phase::Start};
			inputRuntime.Input(composition, consumed, error);
			composition.composition = {TextComposition::Phase::Update, "x", 1};
			test.Check(inputRuntime.Input(composition, consumed, error) && inputRuntime.SetValue("edit", "ok", error),
			    "External UI form assignment replaces the actual value and cancels old preedit");
			composition.composition = {TextComposition::Phase::Cancel};
			test.Check(inputRuntime.Input(composition, consumed, error) &&
			        inputRuntime.GetValue("edit", value, error) && value == "ok",
			    "A late composition cancel cannot overwrite an externally assigned form value");
			test.Check(!inputRuntime.SetValue("edit", "long", error) && inputRuntime.GetValue("edit", value, error) &&
			        value == "ok",
			    "External form assignment rejects authored length overflow without truncation");
			composition.composition = {TextComposition::Phase::Update, "x", 1};
			test.Check(inputRuntime.Input(composition, consumed, error) && inputRuntime.Focus("other", error) &&
			        inputRuntime.GetValue("edit", value, error) && value == "ok",
			    "Switching fields cancels preedit on its original owner");
			composition.composition = {TextComposition::Phase::Commit, "好", 1};
			test.Check(inputRuntime.TextInput().active && inputRuntime.Input(composition, consumed, error) &&
			        inputRuntime.GetValue("other", value, error) && value == "好" &&
			        inputRuntime.GetValue("edit", value, error) && value == "ok",
			    "The next IME commit goes only to the newly focused field");

			UIInputEvent blur;
			blur.type = UIInputEvent::Type::Blur;
			test.Check(inputRuntime.Input(blur, consumed, error) && !inputRuntime.TextInput().active,
			    "Blur cancels composition and releases native editable focus");
		} else
			test.Check(false, error);
		inputRuntime.Shutdown();

		/* 小二进制负载可能展开成大量 map 节点；通过真实异步 Loader 验证容量准入。 */
		AssetTest::Fixture fixture;
		UIDocumentAsset table;
		table.entry = "table.rml";
		table.files[table.entry] = "<rml><head></head><body></body></rml>";
		for (int index = 0; index < 256; ++index)
			table.values.emplace("v" + std::to_string(index), "");
		AssetContainer container;
		container.id = AssetTest::Id(9401);
		container.version = 1;
		AssetType::TryParse("ui_document", container.type);
		AssetRecord record;
		if (fixture.Create(error) && UIDocumentLoader::Encode(table, container.payload, error) &&
		    fixture.Save(container, record, error)) {
			JobSystem jobs;
			if (jobs.Start({1, 8}, error)) {
				VirtualFileSystem files;
				AssetRegistry registry;
				if (fixture.Mount(files, error) && registry.Replace({record}, error)) {
					auto limits = AssetTest::Config();
					limits.maxResidentBytes = 12 * 1024;
					AssetManager assets(jobs, files, registry, limits);
					test.Check(UIDocumentLoader::Register(assets, error) && assets.Request(record.id, error) &&
					        AssetTest::Await(assets, record.id) && assets.GetStatus(record.id) == AssetStatus::Failed &&
					        assets.GetDiagnostic(record.id).find("domain tree") != std::string::npos,
					    "UI loader rejects expanded value tables that exceed the resident budget");
					assets.Shutdown();
				} else
					test.Check(false, error);
				jobs.Shutdown();
			} else
				test.Check(false, error);
		} else
			test.Check(false, error);
	}
} /* 命名空间 Catalpa::Tests */
