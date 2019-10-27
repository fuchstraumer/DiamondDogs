#pragma once
#ifndef DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
#define DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
#include "delegate.hpp"
#include <vector>
#include <functional>
#include <memory>

template<typename Result, typename...Args>
class multicast_delegate_t<Result(Args...)> final : private base_delegate_t<Result(Args...)> {
public:

    multicast_delegate_t() {}
    ~multicast_delegate_t() {}

    bool empty() const noexcept {
        return invocationVector.empty();
    }

    bool operator==(void* ptr) const noexcept {
        return (ptr == nullptr) && invocationVector.empty();
    }

    bool operator!=(void* ptr) const noexcept {
        return (ptr != nullptr) || (!invocationVector.empty());
    }

    size_t size() const noexcept {
        return invocationVector.size();
    }

    multicast_delegate_t& operator+=(const delegate_t<Result(Args...)>& fn) {
        if (fn.empty()) {
            return *this;
        }
        invocationVector.emplace_back(std::make_unique<list_value_t>(fn.invocation.object, fn.invocation.stub));
    }

    void operator()(Args...args) const noexcept {
        for (const auto& item : invocationVector) {
            (*(item->stub))(item->object, args...);
        }
    }

    void operator()(Args...args, delegate_t<void(size_t,Result*)> handler) const {
        size_t idx{ 0u };
        for (const auto& item : invocationVector) {
            Result value = (*(item->stub))(item->object, args...);
            handler(idx, &value);
            ++idx;
        }
    }

private:

    using list_value_t = std::unique_ptr<typename base_delegate_t<Result(Args...)>::invocation_element_t>;
    std::vector<list_value_t> invocationVector;

};

#endif //!DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
