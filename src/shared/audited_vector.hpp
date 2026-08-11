#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <new>

namespace vector_audit {

// -----------------------------------------------------------------------------
// Per-thread allocation tracking
// -----------------------------------------------------------------------------

struct Allocation
{
    void* ptr;
    std::size_t bytes;
    const char* function;
    unsigned long long scope;
};

inline thread_local std::vector<Allocation> allocations;

inline thread_local unsigned long long current_scope = 0;


// -----------------------------------------------------------------------------
// FunctionAudit
// -----------------------------------------------------------------------------

class FunctionAudit
{
public:
    explicit FunctionAudit(const char* function)
        : function_(function)
        , parent_scope_(current_scope)
        , scope_(++current_scope)
    {
    }

    ~FunctionAudit()
    {
        for (const auto& a : allocations)
        {
            if (a.scope == scope_)
            {
                std::fprintf(stderr,
                    "[vector-audit] LEAK: %s: %zu bytes at %p\n",
                    function_,
                    a.bytes,
                    a.ptr);
            }
        }

        current_scope = parent_scope_;
    }

    FunctionAudit(const FunctionAudit&) = delete;
    FunctionAudit& operator=(const FunctionAudit&) = delete;

    unsigned long long scope() const
    {
        return scope_;
    }

private:
    const char* function_;
    unsigned long long parent_scope_;
    unsigned long long scope_;
};


// -----------------------------------------------------------------------------
// Allocator
// -----------------------------------------------------------------------------

template<class T>
struct AuditedAllocator
{
    using value_type = T;

    AuditedAllocator() noexcept = default;

    template<class U>
    AuditedAllocator(const AuditedAllocator<U>&) noexcept {}

    T* allocate(std::size_t n)
    {
        const std::size_t bytes = n * sizeof(T);

        void* p = ::operator new(bytes);

        allocations.push_back({
            p,
            bytes,
            "unknown",
            current_scope
        });

        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t n) noexcept
    {
        auto it = std::find_if(
            allocations.begin(),
            allocations.end(),
            [p](const Allocation& a)
            {
                return a.ptr == p;
            });

        if (it != allocations.end())
            allocations.erase(it);

        ::operator delete(p);
    }

    template<class U>
    bool operator==(const AuditedAllocator<U>&) const noexcept
    {
        return true;
    }

    template<class U>
    bool operator!=(const AuditedAllocator<U>&) const noexcept
    {
        return false;
    }
};


// -----------------------------------------------------------------------------
// Audited vector
// -----------------------------------------------------------------------------

// template<class T>
// using vector = std::vector<T, AuditedAllocator<T>>;

} // namespace vector_audit