#pragma once
#ifndef DIAMOND_DOGS_CORE_DELEGATE_HPP
#define DIAMOND_DOGS_CORE_DELEGATE_HPP
#include <utility>
#include <functional>

template<typename T>
class base_delegate_t;

/**
 * @brief Base class providing common functionality for delegate types
 * 
 * Internal implementation detail that provides the core function pointer
 * and object pointer storage mechanism used by delegate_t and multicast_delegate_t.
 * 
 * @tparam Result Return type of the callable
 * @tparam Args Parameter types of the callable
 */
template<typename Result, typename...Args>
class base_delegate_t<Result(Args...)>
{
protected:
    using func_stub_t = Result(*)(void* this_ptr, Args...);

    struct invocation_element_t
    {
        invocation_element_t() = default;
        invocation_element_t(void* this_ptr, func_stub_t _stub) : object(this_ptr), stub(_stub) {}

        void copy_to(invocation_element_t& destination) const noexcept
        {
            destination.stub = stub;
            destination.object = object;
        }

        bool operator==(const invocation_element_t& other) const noexcept
        {
            return (other.stub == stub) && (other.object == object);
        }

        bool operator!=(const invocation_element_t& other) const noexcept
        {
            return (other.stub != stub) || (other.object != object);
        }

        void* object = nullptr;
        func_stub_t stub = nullptr;
    };
};

template<typename T>
class delegate_t;

template<typename T>
class multicast_delegate_t;

/**
 * @brief Type-erased function wrapper supporting member functions and free functions
 * 
 * A lightweight alternative to std::function that can store and invoke callable objects
 * including free functions, member functions, and functors. Optimized for performance
 * with minimal overhead.
 * 
 * @tparam Result Return type of the callable
 * @tparam Args Parameter types of the callable
 */
template<typename Result, typename...Args>
class delegate_t<Result(Args...)> final : private base_delegate_t<Result(Args...)>
{
    friend class multicast_delegate_t<Result(Args...)>;
public:

    /// Default constructor - creates empty delegate
    delegate_t() = default;

    /**
     * @brief Check if the delegate is empty (not bound to any callable)
     * 
     * @return True if delegate is empty, false otherwise
     */
    bool empty() const noexcept
    {
        return invocation.stub == nullptr;
    }

    /**
     * @brief Boolean conversion operator
     * 
     * @return True if delegate is bound to a callable, false otherwise
     */
    operator bool() const noexcept
    {
        return invocation.stub != nullptr;
    }

    /**
     * @brief Equality comparison with nullptr
     * 
     * @param ptr Pointer to compare with (should be nullptr)
     * @return True if delegate is empty and ptr is nullptr
     */
    bool operator==(const void* ptr) const noexcept
    {
        return (ptr == nullptr) && (empty());
    }

    /**
     * @brief Inequality comparison with nullptr
     * 
     * @param ptr Pointer to compare with (should be nullptr)  
     * @return True if delegate is not empty or ptr is not nullptr
     */
    bool operator!=(const void* ptr) const noexcept
    {
        return (ptr != nullptr) || (!empty());
    }

    /**
     * @brief Copy constructor
     * 
     * @param other Delegate to copy from
     */
    delegate_t(const delegate_t& other) noexcept
    {
        other.invocation.copy_to(invocation);
    }

    /**
     * @brief Copy assignment operator
     * 
     * @param other Delegate to copy from
     * @return Reference to this delegate
     */
    delegate_t& operator=(const delegate_t& other) noexcept
    {
        other.invocation.copy_to(invocation);
        return *this;
    }

    /**
     * @brief Move constructor
     * 
     * @param other Delegate to move from
     */
    delegate_t(delegate_t&& other) noexcept : invocation(std::move(other.invocation)) {}

    /**
     * @brief Move assignment operator
     * 
     * @param other Delegate to move from
     * @return Reference to this delegate
     */
    delegate_t& operator=(delegate_t&& other) noexcept
    {
        invocation = std::move(other.invocation);
        return *this;
    }

    /**
     * @brief Logical NOT operator
     * 
     * @return True if delegate is empty, false otherwise
     */
    bool operator!() const noexcept
    {
        return invocation.stub == nullptr;
    }

    /**
     * @brief Equality comparison with another delegate
     * 
     * @param other Delegate to compare with
     * @return True if both delegates refer to the same callable
     */
    bool operator==(const delegate_t& other) const noexcept
    {
        return invocation == other.invocation;
    }

    /**
     * @brief Inequality comparison with another delegate
     * 
     * @param other Delegate to compare with
     * @return True if delegates refer to different callables
     */
    bool operator!=(const delegate_t& other) const noexcept
    {
        return invocation != other.invocation;
    }

    /**
     * @brief Function call operator - invokes the stored callable
     * 
     * @param args Arguments to pass to the callable
     * @return Result of calling the stored function
     * @note Behavior is undefined if delegate is empty
     */
    Result operator()(Args...args) const
    {
        return (*invocation.stub)(invocation.object,args...);
    }

    /**
     * @brief Generate hash value for this delegate
     * 
     * @return Hash value based on both function stub and object pointer
     */
    std::size_t hash() const noexcept
    {
        std::hash<void*> ptr_hasher;
        std::hash<typename base_delegate_t<Result(Args...)>::func_stub_t> func_hasher;
        
        // Combine hashes of both the function stub and object pointer
        // Use a simple hash combining technique (similar to boost::hash_combine)
        std::size_t seed = func_hasher(invocation.stub);
        seed ^= ptr_hasher(invocation.object) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

    /**
     * @brief Create a delegate bound to a member function
     * 
     * @tparam T Class type containing the member function
     * @tparam Method Pointer to the member function
     * @param object Pointer to object instance to call the method on
     * @return Delegate bound to the specified member function
     */
    template<class T, Result(T::*Method)(Args...)>
    static delegate_t create(T* object)
    {
        return delegate_t(object, method_stub<T,Method>);
    }

    template<class T, Result(T::*Method)(Args...) const>
    static delegate_t create(const T* object)
    {
        return delegate_t(const_cast<T*>(object), const_method_stub<T,Method>);
    }

    template<Result(*Function)(Args...)>
    static delegate_t create()
    {
        return delegate_t(nullptr, function_stub<Function>);
    }

private:

    delegate_t(void* obj, typename base_delegate_t<Result(Args...)>::func_stub_t stub)
    {
        this->invocation.object = obj;
        this->invocation.stub = stub;
    }

    void assign(void* object_ptr, typename base_delegate_t<Result(Args...)>::func_stub_t _stub)
    {
        this->invocation.object = object_ptr;
        this->invocation.stub = _stub;
    }

    template<class T, Result(T::*Method)(Args...)>
    static Result method_stub(void* this_ptr, Args...args)
    {
        T* object_ptr = static_cast<T*>(this_ptr);
        return (object_ptr->*Method)(args...);
    }

    template<class T, Result(T::*Method)(Args...) const>
    static Result const_method_stub(void* this_ptr, Args...args)
    {
        T* const object_ptr = static_cast<T*>(this_ptr);
        return (object_ptr->*Method)(args...);
    }

    template<Result(*Function)(Args...)>
    static Result function_stub(void* this_ptr, Args...args)
    {
        return (Function)(args...);
    }

    typename base_delegate_t<Result(Args...)>::invocation_element_t invocation;

};

// std::hash specialization for delegate_t
template<typename Result, typename...Args>
struct std::hash<delegate_t<Result(Args...)>>
{
    std::size_t operator()(const delegate_t<Result(Args...)>& delegate) const noexcept
    {
        return delegate.hash();
    }
};

#endif //!DIAMOND_DOGS_CORE_DELEGATE_HPP
