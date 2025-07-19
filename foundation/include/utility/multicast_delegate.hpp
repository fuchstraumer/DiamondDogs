#pragma once
#ifndef DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
#define DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
#include "delegate.hpp"
#include <vector>
#include <functional>
#include <memory>

/**
 * @brief Multicast delegate supporting multiple callable targets
 * 
 * A collection of delegates that can be invoked together. Supports adding
 * multiple callables and invoking them all with a single function call.
 * Useful for implementing observer patterns or event systems.
 * 
 * @tparam Result Return type of the callables  
 * @tparam Args Parameter types of the callables
 */
template<typename Result, typename...Args>
class multicast_delegate_t<Result(Args...)> final : private base_delegate_t<Result(Args...)>
{
public:

    /// Default constructor - creates empty multicast delegate
    multicast_delegate_t() {}
    /// Destructor
    ~multicast_delegate_t() {}

    /**
     * @brief Check if the multicast delegate is empty
     * 
     * @return True if no delegates are stored, false otherwise
     */
    bool empty() const noexcept
    {
        return invocationVector.empty();
    }

    /**
     * @brief Equality comparison with nullptr
     * 
     * @param ptr Pointer to compare with (should be nullptr)
     * @return True if multicast delegate is empty and ptr is nullptr
     */
    bool operator==(void* ptr) const noexcept
    {
        return (ptr == nullptr) && invocationVector.empty();
    }

    /**
     * @brief Inequality comparison with nullptr
     * 
     * @param ptr Pointer to compare with (should be nullptr)
     * @return True if multicast delegate is not empty or ptr is not nullptr
     */
    bool operator!=(void* ptr) const noexcept
    {
        return (ptr != nullptr) || (!invocationVector.empty());
    }

    /**
     * @brief Get the number of stored delegates
     * 
     * @return Number of delegates that will be called during invocation
     */
    size_t size() const noexcept
    {
        return invocationVector.size();
    }

    /**
     * @brief Add a delegate to the multicast delegate
     * 
     * @param fn Delegate to add
     * @return Reference to this multicast delegate for chaining
     * @note Empty delegates are ignored and not added
     */
    multicast_delegate_t& operator+=(const delegate_t<Result(Args...)>& fn)
    {
        if (fn.empty())
        {
            return *this;
        }
        invocationVector.emplace_back(std::make_unique<list_value_t>(fn.invocation.object, fn.invocation.stub));
    }

    /**
     * @brief Invoke all stored delegates with given arguments
     * 
     * @param args Arguments to pass to each delegate
     * @note Return values from delegates are discarded when using this overload
     */
    void operator()(Args...args) const noexcept
    {
        for (const auto& item : invocationVector)
        {
            (*(item->stub))(item->object, args...);
        }
    }

    /**
     * @brief Invoke all delegates and handle each return value
     * 
     * @param args Arguments to pass to each delegate
     * @param handler Callback to handle each delegate's return value
     */
    void operator()(Args...args, delegate_t<void(size_t,Result*)> handler) const
    {
        size_t idx{ 0u };
        for (const auto& item : invocationVector)
        {
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
