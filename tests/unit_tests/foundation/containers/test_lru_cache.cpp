#include <gtest/gtest.h>

#include "LifetimeTracked.hpp"
#include "containers/LruCache.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <random>
#include <utility>

namespace containers
{

namespace
{
    /** Maps every key to one of three hashes, so that probe runs are long and overlap. With the cache's Fibonacci
     *  mixing, 144 lands at 0.997 of the table, so its home is the last bucket and its run wraps to bucket 0, where
     *  the run for hash 0 starts. Backward-shift deletion across that wrap is the case most likely to go wrong. */
    struct WrappingCollisionHash
    {
        [[nodiscard]] std::size_t operator()(int32_t key) const noexcept;
    };

    enum class Operation : uint8_t
    {
        Invalid = 0,
        Put,
        Find,
        Peek,
        Erase,
        MoveRoundTrip,
        Clear,
    };

    [[nodiscard]] const char* ToString(Operation operation);

    /** Runs random operations on an LruCache, and mirrors each one on a std::list kept in recency order, most recent
     *  first. Matches peeks every key in the key space, so a lost or a stale entry shows up on the step that caused it. */
    template<typename Hasher>
    class LruModelHarness
    {
    public:
        LruModelHarness(int32_t _capacity, uint32_t seed);
        [[nodiscard]] Operation Step();
        [[nodiscard]] ::testing::AssertionResult Matches() const;

    private:
        using Cache = foundation::containers::LruCache<int32_t, LifetimeTracked, Hasher>;
        using Model = std::list<std::pair<int32_t, int32_t>>;

        [[nodiscard]] int32_t randomIndex(int32_t upper_bound);
        [[nodiscard]] Model::iterator modelFind(int32_t key);
        [[nodiscard]] Model::const_iterator modelFind(int32_t key) const;
        [[nodiscard]] Operation randomOperation();
        void apply(Operation operation, int32_t key);
        void put(int32_t key);
        void find(int32_t key);
        void peek(int32_t key);
        void erase(int32_t key);
        void moveRoundTrip();
        void clear();

        std::mt19937 random;
        Cache cache;
        Model model;
        int32_t capacity;
        int32_t keySpace;
        int32_t nextValue{ 0 };
        // a mismatch found inside an operation, reported by the next Matches
        bool operationFailed{ false };
    };

    template<typename Hasher>
    void RunModel(int32_t capacity, uint32_t seed);

    constexpr int32_t k_StepCount = 20000;
    constexpr std::array<int32_t, 4> k_Capacities{ 1, 2, 5, 64 };
}

TEST(LruCacheTest, MatchesRecencyModelWithWrappingCollisions)
{
    for (const int32_t capacity : k_Capacities)
    {
        RunModel<WrappingCollisionHash>(capacity, static_cast<uint32_t>(capacity));
    }
}

TEST(LruCacheTest, MatchesRecencyModelWithStdHash)
{
    for (const int32_t capacity : k_Capacities)
    {
        RunModel<std::hash<int32_t>>(capacity, static_cast<uint32_t>(capacity) + 100u);
    }
}

namespace
{
    std::size_t WrappingCollisionHash::operator()(int32_t key) const noexcept
    {
        constexpr std::array<std::size_t, 3> k_Hashes{ 144u, 0u, 1u };
        return k_Hashes[static_cast<std::size_t>(key % 3)];
    }

    const char* ToString(Operation operation)
    {
        switch (operation)
        {
        case Operation::Put: return "Put";
        case Operation::Find: return "Find";
        case Operation::Peek: return "Peek";
        case Operation::Erase: return "Erase";
        case Operation::MoveRoundTrip: return "MoveRoundTrip";
        case Operation::Clear: return "Clear";
        default: return "Invalid";
        }
    }

    template<typename Hasher>
    void RunModel(int32_t capacity, uint32_t seed)
    {
        LifetimeTracked::ResetCounters();

        {
            LruModelHarness<Hasher> harness(capacity, seed);

            for (int32_t step = 0; step < k_StepCount; ++step)
            {
                const Operation operation = harness.Step();
                ASSERT_TRUE(harness.Matches())
                    << "capacity " << capacity << ", seed " << seed << ", step " << step << ", after "
                    << ToString(operation);
            }
        }

        EXPECT_EQ(LifetimeTracked::Live, 0) << "capacity " << capacity;
        EXPECT_EQ(LifetimeTracked::Faults, 0) << "capacity " << capacity;
    }

    template<typename Hasher>
    LruModelHarness<Hasher>::LruModelHarness(int32_t _capacity, uint32_t seed) :
        random{ seed },
        cache{ _capacity },
        capacity{ _capacity },
        // more keys than slots, so that Put evicts often and lookups miss often
        keySpace{ _capacity * 2 + 3 }
    {
    }

    template<typename Hasher>
    Operation LruModelHarness<Hasher>::Step()
    {
        const Operation operation = randomOperation();
        apply(operation, randomIndex(keySpace));
        return operation;
    }

    template<typename Hasher>
    ::testing::AssertionResult LruModelHarness<Hasher>::Matches() const
    {
        if (operationFailed)
        {
            return ::testing::AssertionFailure() << "a Find or Erase result differed from the model";
        }

        if (cache.Size() != static_cast<int32_t>(model.size()))
        {
            return ::testing::AssertionFailure() << "size " << cache.Size() << ", expected " << model.size();
        }

        for (int32_t key = 0; key < keySpace; ++key)
        {
            const LifetimeTracked* cached = cache.Peek(key);
            const Model::const_iterator expected = modelFind(key);
            const bool expectedPresent = expected != model.end();

            if ((cached != nullptr) != expectedPresent)
            {
                return ::testing::AssertionFailure() << "key " << key << " present " << (cached != nullptr)
                                                     << ", expected " << expectedPresent;
            }

            if (cached != nullptr && cached->Value() != expected->second)
            {
                return ::testing::AssertionFailure() << "key " << key << " value " << cached->Value()
                                                     << ", expected " << expected->second;
            }
        }

        if (LifetimeTracked::Faults != 0 || LifetimeTracked::Live != static_cast<int64_t>(model.size()))
        {
            return ::testing::AssertionFailure() << LifetimeTracked::Faults << " lifetime faults, "
                                                 << LifetimeTracked::Live << " live values for " << model.size()
                                                 << " entries";
        }

        return ::testing::AssertionSuccess();
    }

    template<typename Hasher>
    int32_t LruModelHarness<Hasher>::randomIndex(int32_t upper_bound)
    {
        std::uniform_int_distribution<int32_t> distribution(0, upper_bound - 1);
        return distribution(random);
    }

    template<typename Hasher>
    typename LruModelHarness<Hasher>::Model::iterator LruModelHarness<Hasher>::modelFind(int32_t key)
    {
        return std::ranges::find(model, key, &Model::value_type::first);
    }

    template<typename Hasher>
    typename LruModelHarness<Hasher>::Model::const_iterator LruModelHarness<Hasher>::modelFind(int32_t key) const
    {
        return std::ranges::find(model, key, &Model::value_type::first);
    }

    template<typename Hasher>
    Operation LruModelHarness<Hasher>::randomOperation()
    {
        // Clear is rare, so that the cache spends most of the run full and evicting
        const int32_t roll = randomIndex(200);

        if (roll < 70)
        {
            return Operation::Put;
        }

        if (roll < 120)
        {
            return Operation::Find;
        }

        if (roll < 150)
        {
            return Operation::Peek;
        }

        if (roll < 190)
        {
            return Operation::Erase;
        }

        if (roll < 199)
        {
            return Operation::MoveRoundTrip;
        }

        return Operation::Clear;
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::apply(Operation operation, int32_t key)
    {
        switch (operation)
        {
        case Operation::Put:
            put(key);
            break;
        case Operation::Find:
            find(key);
            break;
        case Operation::Peek:
            peek(key);
            break;
        case Operation::Erase:
            erase(key);
            break;
        case Operation::MoveRoundTrip:
            moveRoundTrip();
            break;
        case Operation::Clear:
            clear();
            break;
        default:
            break;
        }
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::put(int32_t key)
    {
        const int32_t value = nextValue++;
        cache.Put(key, LifetimeTracked(value));
        const Model::iterator existing = modelFind(key);

        if (existing != model.end())
        {
            model.erase(existing);
        }
        else if (static_cast<int32_t>(model.size()) == capacity)
        {
            model.pop_back();
        }

        model.emplace_front(key, value);
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::find(int32_t key)
    {
        LifetimeTracked* cached = cache.Find(key);
        const Model::iterator expected = modelFind(key);

        if ((cached != nullptr) != (expected != model.end()))
        {
            operationFailed = true;
            return;
        }

        if (expected != model.end())
        {
            operationFailed = operationFailed || cached->Value() != expected->second;
            model.splice(model.begin(), model, expected);
        }
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::peek(int32_t key)
    {
        // the model does not move the key here, so a Peek that changed the recency order shows up as a wrong
        // eviction on a later Put
        static_cast<void>(cache.Peek(key));
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::erase(int32_t key)
    {
        const Model::iterator expected = modelFind(key);
        const bool erased = cache.Erase(key);

        if (erased != (expected != model.end()))
        {
            operationFailed = true;
            return;
        }

        if (erased)
        {
            model.erase(expected);
        }
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::moveRoundTrip()
    {
        Cache moved(std::move(cache));
        cache = std::move(moved);
    }

    template<typename Hasher>
    void LruModelHarness<Hasher>::clear()
    {
        cache.Clear();
        model.clear();
    }
}

}
