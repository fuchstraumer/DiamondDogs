#include <gtest/gtest.h>

#include "LifetimeTracked.hpp"
#include "containers/SmallVector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

namespace containers
{

namespace
{
    enum class Operation : uint8_t
    {
        Invalid = 0,
        PushBack,
        PushBackOwnElement,
        EmplaceOwnElement,
        InsertCopiesOfOwnElement,
        InsertForwardRange,
        InsertSinglePassRange,
        EraseRange,
        PopBack,
        Resize,
        ResizeWithOwnElement,
        AssignOwnElement,
        ShrinkToFit,
        Reserve,
        Swap,
        MoveAssign,
        CopyAssign,
        MoveConstruct,
        EraseIf,
        Clear,
        Count,
    };

    [[nodiscard]] const char* ToString(Operation operation);
    [[nodiscard]] int32_t ValueOf(int32_t element);
    [[nodiscard]] int32_t ValueOf(const LifetimeTracked& element);

    /** Runs random operations on two SmallVectors, and mirrors each one on two std::vector<int32_t>. Two vectors let
     *  swap, move and copy cross between an inline vector and a heap vector. The "own element" operations pass an
     *  element of the vector into the same vector, which is where a reallocation or a shift can read a dead element.
     *  The model copies such a value first, so that it does not depend on std::vector's aliasing rules. */
    template<typename Element, std::size_t N>
    class ModelHarness
    {
    public:
        explicit ModelHarness(uint32_t seed);
        [[nodiscard]] Operation Step();
        [[nodiscard]] ::testing::AssertionResult Matches() const;

    private:
        using Vector = foundation::SmallVector<Element, N>;

        [[nodiscard]] int32_t randomIndex(int32_t upper_bound);
        [[nodiscard]] std::vector<int32_t> freshValues(int32_t value_count);
        void apply(Operation operation);
        void pushBack();
        void pushBackOwnElement();
        void emplaceOwnElement();
        void insertCopiesOfOwnElement();
        void insertForwardRange();
        void insertSinglePassRange();
        void eraseRange();
        void popBack();
        void resize();
        void resizeWithOwnElement();
        void assignOwnElement();
        void shrinkToFit();
        void reserve();
        void swap();
        void moveAssign();
        void copyAssign();
        void moveConstruct();
        void eraseIf();
        void clear();

        std::mt19937 random;
        std::array<Vector, 2> vectors;
        std::array<std::vector<int32_t>, 2> models;
        int32_t target{ 0 };
        int32_t nextValue{ 0 };
    };

    template<typename Element, std::size_t N>
    void RunModel(uint32_t seed);

    // 256, because mimalloc returns 64-byte aligned blocks even from plain operator new, so a 64-byte type passes
    // with the aligned path removed. the array fills the alignment exactly, so that MSVC does not warn about padding
    struct alignas(256) OverAligned
    {
        std::array<int32_t, 64> Values{};
    };

    constexpr int32_t k_StepCount = 20000;
    // the operations keep the size near this on average; the cap stops a long run of inserts from growing it
    constexpr std::size_t k_SizeCap = 96;
}

TEST(SmallVectorTest, MatchesStdVectorAndBalancesEveryLifetime)
{
    RunModel<LifetimeTracked, 4>(1u);
    RunModel<LifetimeTracked, 1>(2u);
    RunModel<LifetimeTracked, 0>(3u);
}

TEST(SmallVectorTest, MatchesStdVectorOnTheMemmovePath)
{
    // int32_t is trivially copyable, so every relocation goes through memmove instead of move-and-destroy
    RunModel<int32_t, 4>(4u);
    RunModel<int32_t, 0>(5u);
}

TEST(SmallVectorTest, OverAlignedElementsStayAlignedOnTheHeap)
{
    foundation::SmallVector<OverAligned, 2> vector;

    // odd capacities, so that the heap block sizes are not powers of two
    for (int32_t reserveCount = 3; reserveCount < 64; reserveCount += 2)
    {
        vector.reserve(static_cast<std::size_t>(reserveCount));
        vector.emplace_back();
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(vector.data());
        ASSERT_EQ(address % alignof(OverAligned), 0u) << "capacity " << vector.capacity();
    }
}

namespace
{
    const char* ToString(Operation operation)
    {
        switch (operation)
        {
        case Operation::PushBack: return "PushBack";
        case Operation::PushBackOwnElement: return "PushBackOwnElement";
        case Operation::EmplaceOwnElement: return "EmplaceOwnElement";
        case Operation::InsertCopiesOfOwnElement: return "InsertCopiesOfOwnElement";
        case Operation::InsertForwardRange: return "InsertForwardRange";
        case Operation::InsertSinglePassRange: return "InsertSinglePassRange";
        case Operation::EraseRange: return "EraseRange";
        case Operation::PopBack: return "PopBack";
        case Operation::Resize: return "Resize";
        case Operation::ResizeWithOwnElement: return "ResizeWithOwnElement";
        case Operation::AssignOwnElement: return "AssignOwnElement";
        case Operation::ShrinkToFit: return "ShrinkToFit";
        case Operation::Reserve: return "Reserve";
        case Operation::Swap: return "Swap";
        case Operation::MoveAssign: return "MoveAssign";
        case Operation::CopyAssign: return "CopyAssign";
        case Operation::MoveConstruct: return "MoveConstruct";
        case Operation::EraseIf: return "EraseIf";
        case Operation::Clear: return "Clear";
        default: return "Invalid";
        }
    }

    int32_t ValueOf(int32_t element)
    {
        return element;
    }

    int32_t ValueOf(const LifetimeTracked& element)
    {
        return element.Value();
    }

    template<typename Element, std::size_t N>
    void RunModel(uint32_t seed)
    {
        LifetimeTracked::ResetCounters();

        {
            ModelHarness<Element, N> harness(seed);

            for (int32_t step = 0; step < k_StepCount; ++step)
            {
                const Operation operation = harness.Step();
                ASSERT_TRUE(harness.Matches())
                    << "N " << N << ", seed " << seed << ", step " << step << ", after " << ToString(operation);
            }
        }

        // the harness is gone, so every element it made must be gone too
        EXPECT_EQ(LifetimeTracked::Live, 0) << "N " << N << ", seed " << seed;
        EXPECT_EQ(LifetimeTracked::Faults, 0) << "N " << N << ", seed " << seed;
    }

    template<typename Element, std::size_t N>
    ModelHarness<Element, N>::ModelHarness(uint32_t seed) :
        random{ seed }
    {
    }

    template<typename Element, std::size_t N>
    Operation ModelHarness<Element, N>::Step()
    {
        target = randomIndex(2);
        const int32_t operationCount = static_cast<int32_t>(Operation::Count) - 1;
        Operation operation = static_cast<Operation>(randomIndex(operationCount) + 1);

        if (models[target].size() > k_SizeCap)
        {
            operation = Operation::EraseRange;
        }

        apply(operation);
        return operation;
    }

    template<typename Element, std::size_t N>
    ::testing::AssertionResult ModelHarness<Element, N>::Matches() const
    {
        std::size_t totalSize = 0;

        for (int32_t side = 0; side < 2; ++side)
        {
            const Vector& vector = vectors[side];
            const std::vector<int32_t>& model = models[side];
            totalSize += vector.size();

            if (vector.size() != model.size() || vector.size() > vector.capacity())
            {
                return ::testing::AssertionFailure() << "vector " << side << ": size " << vector.size()
                                                     << ", capacity " << vector.capacity() << ", expected size "
                                                     << model.size();
            }

            if (vector.is_inline() != (vector.capacity() == N))
            {
                return ::testing::AssertionFailure() << "vector " << side << ": is_inline " << vector.is_inline()
                                                     << " with capacity " << vector.capacity();
            }

            for (std::size_t index = 0; index < model.size(); ++index)
            {
                const int32_t actual = ValueOf(vector[index]);

                if (actual != model[index])
                {
                    return ::testing::AssertionFailure() << "vector " << side << "[" << index << "] is " << actual
                                                         << ", expected " << model[index];
                }
            }
        }

        if constexpr (std::same_as<Element, LifetimeTracked>)
        {
            if (LifetimeTracked::Faults != 0 || LifetimeTracked::Live != static_cast<int64_t>(totalSize))
            {
                return ::testing::AssertionFailure() << LifetimeTracked::Faults << " lifetime faults, "
                                                     << LifetimeTracked::Live << " live elements for "
                                                     << totalSize << " stored";
            }
        }

        return ::testing::AssertionSuccess();
    }

    template<typename Element, std::size_t N>
    int32_t ModelHarness<Element, N>::randomIndex(int32_t upper_bound)
    {
        std::uniform_int_distribution<int32_t> distribution(0, upper_bound - 1);
        return distribution(random);
    }

    template<typename Element, std::size_t N>
    std::vector<int32_t> ModelHarness<Element, N>::freshValues(int32_t value_count)
    {
        std::vector<int32_t> values(static_cast<std::size_t>(value_count));

        for (int32_t& value : values)
        {
            value = nextValue++;
        }

        return values;
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::apply(Operation operation)
    {
        switch (operation)
        {
        case Operation::PushBack:
            pushBack();
            break;
        case Operation::PushBackOwnElement:
            pushBackOwnElement();
            break;
        case Operation::EmplaceOwnElement:
            emplaceOwnElement();
            break;
        case Operation::InsertCopiesOfOwnElement:
            insertCopiesOfOwnElement();
            break;
        case Operation::InsertForwardRange:
            insertForwardRange();
            break;
        case Operation::InsertSinglePassRange:
            insertSinglePassRange();
            break;
        case Operation::EraseRange:
            eraseRange();
            break;
        case Operation::PopBack:
            popBack();
            break;
        case Operation::Resize:
            resize();
            break;
        case Operation::ResizeWithOwnElement:
            resizeWithOwnElement();
            break;
        case Operation::AssignOwnElement:
            assignOwnElement();
            break;
        case Operation::ShrinkToFit:
            shrinkToFit();
            break;
        case Operation::Reserve:
            reserve();
            break;
        case Operation::Swap:
            swap();
            break;
        case Operation::MoveAssign:
            moveAssign();
            break;
        case Operation::CopyAssign:
            copyAssign();
            break;
        case Operation::MoveConstruct:
            moveConstruct();
            break;
        case Operation::EraseIf:
            eraseIf();
            break;
        case Operation::Clear:
            clear();
            break;
        default:
            break;
        }
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::pushBack()
    {
        vectors[target].push_back(Element(nextValue));
        models[target].push_back(nextValue);
        ++nextValue;
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::pushBackOwnElement()
    {
        Vector& vector = vectors[target];
        std::vector<int32_t>& model = models[target];

        if (model.empty())
        {
            return;
        }

        const int32_t source = randomIndex(static_cast<int32_t>(model.size()));
        const int32_t sourceValue = model[source];
        vector.push_back(vector[source]);
        model.push_back(sourceValue);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::emplaceOwnElement()
    {
        Vector& vector = vectors[target];
        std::vector<int32_t>& model = models[target];

        if (model.empty())
        {
            return;
        }

        const int32_t source = randomIndex(static_cast<int32_t>(model.size()));
        const int32_t position = randomIndex(static_cast<int32_t>(model.size()) + 1);
        const int32_t sourceValue = model[source];
        vector.emplace(vector.begin() + position, vector[source]);
        model.insert(model.begin() + position, sourceValue);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::insertCopiesOfOwnElement()
    {
        Vector& vector = vectors[target];
        std::vector<int32_t>& model = models[target];

        if (model.empty())
        {
            return;
        }

        const int32_t source = randomIndex(static_cast<int32_t>(model.size()));
        const int32_t position = randomIndex(static_cast<int32_t>(model.size()) + 1);
        const std::size_t copyCount = static_cast<std::size_t>(randomIndex(6));
        const int32_t sourceValue = model[source];
        vector.insert(vector.begin() + position, copyCount, vector[source]);
        model.insert(model.begin() + position, copyCount, sourceValue);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::insertForwardRange()
    {
        const int32_t position = randomIndex(static_cast<int32_t>(models[target].size()) + 1);
        const std::vector<int32_t> values = freshValues(randomIndex(7));
        vectors[target].insert_range(vectors[target].begin() + position, values);
        models[target].insert(models[target].begin() + position, values.begin(), values.end());
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::insertSinglePassRange()
    {
        // an istream view has no size up front, so this takes the append-then-rotate path
        const int32_t position = randomIndex(static_cast<int32_t>(models[target].size()) + 1);
        const std::vector<int32_t> values = freshValues(randomIndex(7));
        std::string text;

        for (const int32_t value : values)
        {
            text += std::to_string(value) + " ";
        }

        std::istringstream stream(text);
        vectors[target].insert_range(vectors[target].begin() + position, std::views::istream<int32_t>(stream));
        models[target].insert(models[target].begin() + position, values.begin(), values.end());
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::eraseRange()
    {
        const int32_t size = static_cast<int32_t>(models[target].size());
        const int32_t first = randomIndex(size + 1);
        const int32_t last = first + randomIndex(size - first + 1);
        vectors[target].erase(vectors[target].begin() + first, vectors[target].begin() + last);
        models[target].erase(models[target].begin() + first, models[target].begin() + last);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::popBack()
    {
        if (models[target].empty())
        {
            return;
        }

        vectors[target].pop_back();
        models[target].pop_back();
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::resize()
    {
        const std::size_t newSize = static_cast<std::size_t>(randomIndex(static_cast<int32_t>(models[target].size()) + 9));
        vectors[target].resize(newSize);
        models[target].resize(newSize);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::resizeWithOwnElement()
    {
        Vector& vector = vectors[target];
        std::vector<int32_t>& model = models[target];

        if (model.empty())
        {
            return;
        }

        const int32_t source = randomIndex(static_cast<int32_t>(model.size()));
        // past the capacity two times in three, so that the fill value lives in the buffer that reallocate frees
        const std::size_t newSize = vector.capacity() + static_cast<std::size_t>(randomIndex(3));
        const int32_t sourceValue = model[source];
        vector.resize(newSize, vector[source]);
        model.resize(newSize, sourceValue);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::assignOwnElement()
    {
        Vector& vector = vectors[target];
        std::vector<int32_t>& model = models[target];

        if (model.empty())
        {
            return;
        }

        const int32_t source = randomIndex(static_cast<int32_t>(model.size()));
        const std::size_t newSize = static_cast<std::size_t>(randomIndex(static_cast<int32_t>(vector.capacity()) + 3));
        const int32_t sourceValue = model[source];
        vector.assign(newSize, vector[source]);
        model.assign(newSize, sourceValue);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::shrinkToFit()
    {
        vectors[target].shrink_to_fit();
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::reserve()
    {
        vectors[target].reserve(models[target].size() + static_cast<std::size_t>(randomIndex(20)));
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::swap()
    {
        if (randomIndex(4) == 0)
        {
            vectors[target].swap(vectors[target]);
            return;
        }

        vectors[0].swap(vectors[1]);
        models[0].swap(models[1]);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::moveAssign()
    {
        const int32_t source = 1 - target;

        if (randomIndex(8) == 0)
        {
            // self-move must leave the contents alone. the reference hides the self-move from the compiler warning
            Vector& self = vectors[target];
            vectors[target] = std::move(self);
            return;
        }

        // a moved-from SmallVector is empty, and the model holds us to that
        vectors[target] = std::move(vectors[source]);
        models[target] = models[source];
        models[source].clear();
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::copyAssign()
    {
        const int32_t source = randomIndex(2);
        vectors[target] = vectors[source];
        models[target] = models[source];
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::moveConstruct()
    {
        const int32_t destination = 1 - target;
        Vector moved(std::move(vectors[target]));
        vectors[destination] = std::move(moved);
        models[destination] = models[target];
        models[target].clear();
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::eraseIf()
    {
        if (models[target].empty())
        {
            return;
        }

        const int32_t erasedValue = models[target][randomIndex(static_cast<int32_t>(models[target].size()))];
        const auto matchesErasedValue = [erasedValue](const Element& element) { return ValueOf(element) == erasedValue; };
        erase_if(vectors[target], matchesErasedValue);
        std::erase(models[target], erasedValue);
    }

    template<typename Element, std::size_t N>
    void ModelHarness<Element, N>::clear()
    {
        vectors[target].clear();
        models[target].clear();
    }
}

}
