#pragma once
#ifndef DIAMOND_DOGS_CORE_DELEGATE_HPP
#define DIAMOND_DOGS_CORE_DELEGATE_HPP
#include <utility>
#include <functional>

namespace Foundation
{

    template<typename T>
    class BaseDelegate;

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
    class BaseDelegate<Result(Args...)>
    {
    protected:
        using FuncStubType = Result(*)(void* this_ptr, Args...);

        struct InvocationElement
        {
            InvocationElement() = default;
            InvocationElement(void* this_ptr, FuncStubType _stub) : object(this_ptr), stub(_stub) {}

            void CopyTo(InvocationElement& destination) const noexcept
            {
                destination.stub = stub;
                destination.object = object;
            }

            bool operator==(const InvocationElement& other) const noexcept
            {
                return (other.stub == stub) && (other.object == object);
            }

            bool operator!=(const InvocationElement& other) const noexcept
            {
                return (other.stub != stub) || (other.object != object);
            }

            void* object = nullptr;
            FuncStubType stub = nullptr;
        };
    };

    template<typename T>
    class Delegate;

    template<typename T>
    class MulticastDelegate;

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
    class Delegate final : private BaseDelegate<Result(Args...)>
    {
        friend class MulticastDelegate<Result(Args...)>;
    public:

        /// Default constructor - creates empty delegate
        Delegate() = default;

        /**
         * @brief Check if the delegate is empty (not bound to any callable)
         * 
         * @return True if delegate is empty, false otherwise
         */
        bool Empty() const noexcept
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
        Delegate(const Delegate& other) noexcept
        {
            other.invocation.CopyTo(invocation);
        }

        /**
         * @brief Copy assignment operator
         * 
         * @param other Delegate to copy from
         * @return Reference to this delegate
         */
        Delegate& operator=(const Delegate& other) noexcept
        {
            other.invocation.CopyTo(invocation);
            return *this;
        }

        /**
         * @brief Move constructor
         * 
         * @param other Delegate to move from
         */
        Delegate(Delegate&& other) noexcept : invocation(std::move(other.invocation)) {}

        /**
         * @brief Move assignment operator
         * 
         * @param other Delegate to move from
         * @return Reference to this delegate
         */
        Delegate& operator=(Delegate&& other) noexcept
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
        bool operator==(const Delegate& other) const noexcept
        {
            return invocation == other.invocation;
        }

        /**
         * @brief Inequality comparison with another delegate
         * 
         * @param other Delegate to compare with
         * @return True if delegates refer to different callables
         */
        bool operator!=(const Delegate& other) const noexcept
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
            std::hash<typename BaseDelegate<Result(Args...)>::FuncStubType> func_hasher;

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
        static Delegate Create(T* object)
        {
            return Delegate(object, method_stub<T,Method>);
        }

        template<class T, Result(T::*Method)(Args...) const>
        static Delegate Create(const T* object)
        {
            return Delegate(const_cast<T*>(object), const_method_stub<T,Method>);
        }

        template<Result(*Function)(Args...)>
        static Delegate Create()
        {
            return Delegate(nullptr, function_stub<Function>);
        }

    private:

        Delegate(void* obj, typename BaseDelegate<Result(Args...)>::FuncStubType stub)
        {
            this->invocation.object = obj;
            this->invocation.stub = stub;
        }

        void Assign(void* object_ptr, typename BaseDelegate<Result(Args...)>::FuncStubType _stub)
        {
            this->invocation.object = object_ptr;
            this->invocation.stub = _stub;
        }

        template<class T, Result(T::*Method)(Args...)>
        static Result MethodStub(void* this_ptr, Args...args)
        {
            T* object_ptr = static_cast<T*>(this_ptr);
            return (object_ptr->*Method)(args...);
        }

        template<class T, Result(T::*Method)(Args...) const>
        static Result ConstMethodStub(void* this_ptr, Args...args)
        {
            T* const object_ptr = static_cast<T*>(this_ptr);
            return (object_ptr->*Method)(args...);
        }

        template<Result(*Function)(Args...)>
        static Result FunctionStub(void* this_ptr, Args...args)
        {
            return (Function)(args...);
        }

        typename BaseDelegate<Result(Args...)>::InvocationElementType invocation;

    };

}


// std::hash specialization for Delegate
template<typename Result, typename...Args>
struct std::hash<Foundation::Utility::Delegate<Result(Args...)>>
{
    std::size_t operator()(const Foundation::Utility::Delegate<Result(Args...)>& delegate) const noexcept
    {
        return delegate.hash();
    }
};

#endif //!DIAMOND_DOGS_CORE_DELEGATE_HPP
