#pragma once
#ifndef DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
#define DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
#include "Delegate.hpp"
#include <vector>
#include <memory>

namespace Foundation
{

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
    class MulticastDelegate final : private BaseDelegate<Result(Args...)>
    {
    public:

        /// Default constructor - creates empty multicast delegate
        MulticastDelegate() noexcept = default;
        /// Destructor
        ~MulticastDelegate() noexcept = default;

        /**
         * @brief Check if the multicast delegate is empty
         * 
         * @return True if no delegates are stored, false otherwise
         */
        bool Empty() const noexcept
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
        size_t Size() const noexcept
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
        MulticastDelegate& operator+=(const Delegate<Result(Args...)>& fn)
        {
            if (fn.Empty())
            {
                return *this;
            }
            invocationVector.emplace_back(std::make_unique<typename BaseDelegate<Result(Args...)>::InvocationElement>(fn.invocation.object, fn.invocation.stub));
            return *this;
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

        using ListValueType = std::unique_ptr<typename BaseDelegate<Result(Args...)>::InvocationElement>;
        std::vector<ListValueType> invocationVector;

    };

} // namespace Foundation::Utility

#endif //!DIAMOND_DOGS_MULTICAST_DELEGATE_HPP
