#include "catpch.h"
#include "TestRunner.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/PlatformServices.h"
#include "Scene/Scene.h"

namespace Catalpa::Tests {
    // 检查持久文本协议、随机生成失败边界及真实Scene身份消费，不初始化窗口/GPU。
    void RunStableIdTests(TestContext& context)
    {
        const auto known = StableId::FromWords(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
        context.Check(known.ToString() == "01234567-89ab-cdef-fedc-ba9876543210", "ID text follows byte order, not host endianness");
        StableId parsed;
        context.Check(StableId::TryParse("01234567-89AB-CDEF-FEDC-BA9876543210", parsed) && parsed == known,
            "Uppercase input preserves arbitrary existing 128-bit IDs");
        context.Check(parsed.ToString() == known.ToString(), "Canonical text always uses lowercase hexadecimal");
        for (std::string_view invalid : { "", "00000000-0000-0000-0000-000000000000",
            "0123456789abcdeffedcba9876543210", "{01234567-89ab-cdef-fedc-ba9876543210}",
            "01234567_89ab-cdef-fedc-ba9876543210", "01234567-89ab-cdef-fedc-ba987654321g",
            "01234567-89ab-cdef-fedc-ba9876543210 ", " 1234567-89ab-cdef-fedc-ba9876543210" }) {
            parsed = known;
            context.Check(!StableId::TryParse(invalid, parsed) && parsed == known, "Malformed or zero ID leaves output unchanged");
        }
        auto embeddedNull = known.ToString();
        embeddedNull[17] = '\0';
        context.Check(!StableId::TryParse(embeddedNull, parsed) && parsed == known, "Embedded NUL is not accepted as truncated ID text");

        std::string error;
        std::set<StableId> generated;
        for (size_t i = 0; i < 128; ++i) {
            StableId id;
            const bool ok = GenerateStableId(id, error);
            context.Check(ok, "System random ID generation succeeds");
            if (!ok) { context.Check(false, error); break; }
            context.Check(id.HasValue() && (id.bytes[6] & 0xf0) == 0x40 && (id.bytes[8] & 0xc0) == 0x80,
                "New IDs carry UUIDv4 version and variant bits");
            context.Check(generated.insert(id).second, "Generated sample has no duplicate identity");
            context.Check(StableId::TryParse(id.ToString(), parsed) && parsed == id, "Generated ID round-trips without re-numbering");
        }
        std::vector<std::byte> tooLarge(1024 * 1024 + 1, std::byte{ 0x5a });
        context.Check(!FillRandomBytes(tooLarge, error)
            && std::all_of(tooLarge.begin(), tooLarge.end(), [](auto value) { return value == std::byte{ 0x5a }; }),
            "Rejected random byte request preserves destination");

        PersistentEntityId entityId;
        const bool created = GeneratePersistentEntityId(entityId, error);
        context.Check(created, "Scene author identity uses the same platform generator");
        if (!created) return;
        PersistentEntityId restored;
        context.Check(PersistentEntityId::TryParse(entityId.ToString(), restored) && restored == entityId,
            "Restoring a persistent entity retains its identity");
        Scene scene("Generated identity");
        context.Check(scene.QueueCreate(entityId, "Author entity", {}).IsSuccess() && scene.CommitPending().IsSuccess(),
            "Existing scene creation consumes the generated author ID");
        const EntityId runtime = scene.Resolve(restored);
        context.Check(scene.GetWorld().IsAlive(runtime), "Text-restored persistent ID resolves to the live entity");
        context.Check(scene.QueueCreate(restored, "Duplicate", {}).IsSuccess() && !scene.CommitPending().IsSuccess(),
            "Duplicate author ID is rejected at the real structural safe point");
        context.Check(scene.Resolve(entityId) == runtime && scene.GetWorld().GetEntityCount() == 1,
            "Duplicate identity cannot replace an existing entity");
    }
}
