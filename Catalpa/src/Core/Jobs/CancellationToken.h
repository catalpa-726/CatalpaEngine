#pragma once
#include "catpch.h"
namespace Catalpa {
    // Cooperative cancellation: work must still finish and release its captured owners.
    class CancellationToken {
    public:
        CancellationToken() = default;
        [[nodiscard]] bool IsCancellationRequested() const noexcept { return m_State && m_State->load(std::memory_order_acquire); }
    private:
        friend class CancellationSource;
        explicit CancellationToken(std::shared_ptr<std::atomic_bool> state) : m_State(std::move(state)) {}
        std::shared_ptr<std::atomic_bool> m_State;
    };
    class CancellationSource {
    public:
        CancellationSource() : m_State(std::make_shared<std::atomic_bool>(false)) {}
        [[nodiscard]] CancellationToken Token() const noexcept { return CancellationToken(m_State); }
        void Cancel() const noexcept { if (m_State) m_State->store(true, std::memory_order_release); }
    private:
        std::shared_ptr<std::atomic_bool> m_State;
    };
}
