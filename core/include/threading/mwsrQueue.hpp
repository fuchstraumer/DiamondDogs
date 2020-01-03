#pragma once
#ifndef CORE_THREADING_MWSR_QUEUE_HPP
#define CORE_THREADING_MWSR_QUEUE_HPP
#include "casReactor.hpp"
#include <cstdint>
#include <cassert>
#include <utility>
#include <mutex>
#include <condition_variable>

// Article detailing this: https://accu.org/index.php/journals/2467
// Code sourced from: https://github.com/ITHare/mtprimitives/blob/master/src/mwsr.h

namespace detail
{

    constexpr inline size_t mwsrQueueSize = 64u;

    constexpr inline bool maskGetBit(uint64_t mask, uint64_t position) noexcept
    {
        return (mask & (uint64_t(1u) << position)) != 0;
    }

    constexpr inline uint64_t maskSetBit(uint64_t mask, uint64_t position) noexcept
    {
        return mask | (uint64_t(1u) << position);
    }

    struct EntranceReactorData
    {
    private:
        friend class EntranceReactorHandle;
        friend class CasReactorHandle<EntranceReactorData>;
        alignas(cas_data128_t) cas_data128_t data;
        // "stores" fields by allowing access to them as parts of a bitfield
        // firstIDToWrite is a 64 bit unsigned int
        //      - this represents the first ID in the queue which is available for writing
        // lastIDToWrite is a SIGNED 32 bit offset from firstIDToWrite
        // lockedThreadCount is a 32 bit unsigned int
        //      - number of writers locked because the queue is full
    public:

        EntranceReactorData() {}

        EntranceReactorData(uint64_t firstToWrite, uint64_t lastToWrite)
        {
            memset(this, 0, sizeof(EntranceReactorData));
            setIDsToWrite(firstToWrite, lastToWrite);
        }

        cas_data128_t Data() const noexcept
        {
            return data;
        }

    private:

        uint64_t getFirstIDToWrite() const noexcept
        {
            return data.low;
        }

        uint64_t getLastIDToWrite() const noexcept
        {
            return data.low + int32_t(data.high & 0xFFFFFFFFLL);
        }

        uint32_t getLockedThreadCount() const noexcept
        {
            return data.high >> 32u;
        }

        void setLockedThreadCount(uint32_t value) noexcept
        {
            data.high = (data.high & 0xFFFFFFFFLL) | (uint64_t(value) << 32u);
        }

        void setFirstIDToWrite(uint64_t value)
        {
            const uint32_t lockedCount = getLockedThreadCount();
            const uint64_t last = getLastIDToWrite();
            data.low = value;
            const int64_t offset = last - value;
            assert((int32_t)offset == offset);
            const uint64_t oldHigh = data.high;
            data.high = (data.high & 0xFFFFFFFF00000000LL) | uint32_t(int32_t(offset));
            assert(getFirstIDToWrite() == value);
            assert(getLastIDToWrite() == last);
            assert(lockedCount == getLockedThreadCount());
        }

        void setLastIDToWrite(uint64_t value)
        {
            uint32_t lockedCount = getLockedThreadCount();
            uint64_t first = getFirstIDToWrite();
            const int64_t offset = value - data.low;
            assert((int32_t)offset == offset);
            data.high = (data.high & 0xFFFFFFFF00000000LL) | uint32_t(int32_t(offset));
            assert(getFirstIDToWrite() == first);
            assert(getLastIDToWrite() == value);
            assert(getLockedThreadCount() == lockedCount);
        }

        void setIDsToWrite(uint64_t first, uint64_t last)
        {
            uint32_t lockedCount = getLockedThreadCount();
            data.low = first;
            const int64_t offset = last - first;
            assert(int32_t(offset) == offset);
            data.high = (data.high & 0xFFFFFFFF00000000LL) | uint32_t(int32_t(offset));
            assert(getFirstIDToWrite() == first);
            assert(getLastIDToWrite() == last);
            assert(lockedCount == getLockedThreadCount());
        }

    };

    class EntranceReactorHandle : public CasReactorHandle<EntranceReactorData>
    {
    public:

        EntranceReactorHandle(atomic128& cas_data) : CasReactorHandle<EntranceReactorData>(cas_data)
        {

        }

        // result.first contains the allocated ID, and result.second indicates whether caller should lock for a while
        std::pair<uint64_t, bool> allocateNextID()
        {
            std::pair<uint64_t, bool> result{ false, 0u };

            auto reactFunction = [](EntranceReactorData& data, bool& earlyExit)->std::pair<uint64_t, bool>
            {
                const uint64_t firstToWrite = data.getFirstIDToWrite();
                uint64_t newIDToWrite = firstToWrite + 1u;
                data.setFirstIDToWrite(newIDToWrite);

                bool willLock = false;
                if (newIDToWrite >= data.getLastIDToWrite())
                {
                    willLock = true;
                    const uint32_t lockedCount = data.getLockedThreadCount();
                    data.setLockedThreadCount(lockedCount + 1u);
                }

                return { firstToWrite, willLock };
            };

            React(result, reactFunction);
            
            return result;
        }

        // indicates that a thread has unlocked (was able to add to queue)
        void unlock()
        {
            int dummyResult{ 0 };
            
            auto reactFunction = [](EntranceReactorData& data, bool& earlyExit)->int
            {
                const uint32_t lockedCount = data.getLockedThreadCount();
                uint32_t newLockedCount = lockedCount - 1u;
                assert(newLockedCount < lockedCount);
                data.setLockedThreadCount(newLockedCount);
                return 0;
            };

            React(dummyResult, reactFunction);
        }

        // this tells the reactor that we've read everything up to lastIDToWrite, so more writes are now allowed.
        // the return value returns whether or not we should unlock a writer or two
        bool moveLastToWrite(uint64_t newLastIDToWrite)
        {
            bool result = false;

            auto reactFunction = [](EntranceReactorData& data, uint64_t newLastID, bool& earlyExit)->bool
            {
                const uint64_t lastIDToWrite = data.getLastIDToWrite();
                assert(lastIDToWrite <= newLastID);
                const uint32_t lockedCount = data.getLockedThreadCount();
                data.setLastIDToWrite(newLastID);
                return lockedCount > 0u;
            };

            React(result, reactFunction);

            return result;
        }

    };

    class ExitReactorHandle;

    class ExitReactorData
    {
    private:
        alignas(cas_data128_t) cas_data128_t data;
        friend class ExitReactorHandle;
        friend class CasReactorHandle<ExitReactorData>;
        // just like EntranceReactorData, stores stuff by just masking through to underlying bitfield
        // firstIDToRead is an unsigned 63 bit int
        // readerIsLocked is a single bit, trailing firstIDToRead
        // completedWritesMask is a 64 bit unsigned int
    public:

        static_assert(mwsrQueueSize <= 64u, "QueueSize cannot exceed number of bits in mask!");
        constexpr static uint64_t EntranceFirstToWrite = 0u;
        constexpr static uint64_t EntranceLastToWrite = mwsrQueueSize;

        ExitReactorData()
        {
            memset(this, 0, sizeof(ExitReactorData));
        }

    private:

        uint64_t getFirstIDToRead() const noexcept
        {
            constexpr static uint64_t mask = 0x7FFFFFFFLL;
            return data.high & mask;
        }

        uint64_t getCompletedWritesMask() const noexcept
        {
            return data.low;
        }

        bool getReaderIsLocked() const noexcept
        {
            return (data.high & 0x80000000LL) != 0;
        }

        void setFirstIDToRead(uint64_t value)
        {
            assert((value & 0x80000000LL) != 0);
            data.high = (data.high & 0x80000000LL) | value;
        }

        void setCompletedWritesMask(uint64_t value) noexcept
        {
            data.low = value;
        }

        void setReaderIsLocked() noexcept
        {
            data.high |= 0x80000000LL;
        }

        void setReaderIsUnlocked() noexcept
        {
            data.high &= ~0x80000000LL;
        }
    };

    class ExitReactorHandle : public CasReactorHandle<ExitReactorData>
    {
    public:
        ExitReactorHandle(atomic128& atomic) : CasReactorHandle<ExitReactorData>(atomic) {}

        bool writeCompleted(uint64_t _id)
        {
            auto reactFunction = [](ExitReactorData& data, uint64_t id, bool& earlyExit)->bool
            {
                const uint64_t firstToRead = data.getFirstIDToRead();
                assert(id >= firstToRead);
                // make sure we haven't wrapped around our queue, because then somethings big broke
                assert(id < firstToRead + mwsrQueueSize);
                const uint64_t mask = data.getCompletedWritesMask();
                // make sure write hasn't already been marked as complete
                assert(!maskGetBit(mask, id - firstToRead));
                // IDs are all relative, so doing this gets us the offset into mask where "id" is
                uint64_t newMask = maskSetBit(mask, id - firstToRead);
                data.setCompletedWritesMask(newMask);

                bool result = false;
                // reader only locks if no writes have completed: if one has, update that state
                if (data.getReaderIsLocked())
                {
                    data.setReaderIsUnlocked();
                    result = true;
                }

                return result;
            };

            bool result = false;
            /// result is true if we've unlocked the reader (from locked), false if it wasn't even locked in the first place
            React(result, reactFunction);
            return result;
        }

        std::pair<size_t, uint64_t> startRead()
        {
            auto reactFunction = [](ExitReactorData& data, bool& earlyExit)->std::pair<size_t, uint64_t>
            {
                // we better not have started reading while we're supposed to be locked
                assert(!data.getReaderIsLocked());
                uint64_t mask = data.getCompletedWritesMask();

                if (maskGetBit(mask, 0u))
                {
                    // we'll exit without modifying the state
                    earlyExit = true;
                    uint64_t n = 1u;
                    for(; n < mwsrQueueSize; ++n)
                    {
                        if (!maskGetBit(mask, n))
                        {
                            break;
                        }
                    }
                    // returns number of completed writes, and then the first ID to read from
                    return std::pair<size_t, uint64_t>{ size_t(n), data.getFirstIDToRead() };
                }
                else
                {
                    // not a single write has completed. reader has to lock and wait for that.
                    data.setReaderIsLocked();
                }

                return { 0u, 0u };
            };

            std::pair<size_t, uint64_t> result{ 0u, 0u };
            React(result, reactFunction);
            return result;
        }

        uint64_t readCompleted(size_t _size, uint64_t _id)
        {
            auto reactFunction = [](ExitReactorData& data, uint64_t size, uint64_t id, bool& earlyExit)->uint64_t
            {
                const uint64_t mask = data.getCompletedWritesMask();
                assert(!maskGetBit(mask, 0));

                const uint64_t previousFirstIDToRead = data.getFirstIDToRead();
                // update first ID to read based on how many we say have completed
                uint64_t newFirstIDToRead = previousFirstIDToRead + size;
                // checks for overflow, but probably not necessary to have these particular checks tbh...
                assert(newFirstIDToRead > previousFirstIDToRead);
                data.setFirstIDToRead(newFirstIDToRead);

                uint64_t newMask = mask >> size;
                data.setCompletedWritesMask(newMask);
                uint64_t newLastIDToWrite = newFirstIDToRead + mwsrQueueSize;
                return newLastIDToWrite;
            };

            uint64_t result{ 0u };
            React(result, reactFunction, _size, _id);
            return result;
        }

    };

    class LockedSingleThread
    {
    private:
        int64_t lockCount{ 0u };
        std::mutex mutex;
        std::condition_variable cv;
    public:

        void lockAndWait()
        {
            std::unique_lock lock(mutex);
            assert(lockCount == -1 || lockCount == 0);
            ++lockCount;
            while (lockCount > 0)
            {
                // Thread will sleep while lock count is set
                cv.wait(lock);
            }
        }

        void unlock()
        {
            std::unique_lock lock(mutex);
            --lockCount;
            lock.unlock();
            // lockCount lowered, wake thread that was locked and waiting
            cv.notify_one();
        }
    };

    struct LockedThreadsListLockItem
    {
        // ID of item we're waiting on
        uint64_t itemID{ std::numeric_limits<uint64_t>::max() };
        std::mutex mutex;
        std::condition_variable cv;
        LockedThreadsListLockItem* next{ nullptr };
    };

    // Templatization required, so that it works with more than one type of queue item
    // (as each template initilization will be a new type, causing new static members)
    template<typename QueueItem>
    class LockedThreadsList
    {
    private:
        uint64_t unlockUpTo{ 0u };
        std::mutex mutex;
        LockedThreadsListLockItem* first{ nullptr };
        static thread_local LockedThreadsListLockItem lockedThreadsListTLS_data;
    public:

        void lockAndWait(uint64_t itemId)
        {
            std::unique_lock lock(mutex);
            lockedThreadsListTLS_data.itemID = itemId;
            insertSorted(&lockedThreadsListTLS_data);
            while (itemId >= unlockUpTo)
            {
                lockedThreadsListTLS_data.cv.wait(lock);
            }
            removeFromList(&lockedThreadsListTLS_data);
        }

        void unlockAllUpTo(uint64_t id)
        {
            std::unique_lock lock(mutex);
            assert(id >= unlockUpTo);
            unlockUpTo = id;

            for (auto iter = first; iter != nullptr; iter = iter->next)
            {
                if (iter->itemID < unlockUpTo)
                {
                    iter->cv.notify_one();
                }
            }
        }

    private:
        
        void insertSorted(LockedThreadsListLockItem* item)
        {
            LockedThreadsListLockItem* previous{ nullptr };
            for (auto iter = first; iter != nullptr; previous = iter, iter = iter->next)
            {
                // already inserted, not good
                assert(iter != item);
                // duplicate item ID, also not good
                assert(iter->itemID != item->itemID);
                if (item->itemID < iter->itemID)
                {
                    insertBetweenEntries(item, previous, iter);
                    return;
                }
            }
            insertBetweenEntries(item, previous, nullptr);
        }

        void insertBetweenEntries(LockedThreadsListLockItem* iter, LockedThreadsListLockItem* prev, LockedThreadsListLockItem* toInsert)
        {
            if (prev == nullptr)
            {
                assert(toInsert == first);
                iter->next = first;
                first = iter;
            }
            else
            {
                assert(toInsert = prev->next);
                iter->next = toInsert;
                prev->next = iter;
            }
        }

        void removeFromList(LockedThreadsListLockItem* toRemove)
        {
            // oh, no...
            assert(first != nullptr);
            LockedThreadsListLockItem* prev = nullptr;
            for (auto iter = first; ; iter = iter->next)
            {
                if (iter == toRemove)
                {
                    if (prev == nullptr)
                    {
                        first = iter->next;
                    }
                    else
                    {
                        prev->next = iter->next;
                    }
                    return;
                }
                prev = iter;
            }
        }

    };
}

template<typename T>
class mwsrQueue
{
private:
    QueueItem items[detail::mwsrQueueSize];
    atomic128 entranceData;
    atomic128 exitData;
    detail::LockedThreadsList<T> lockedWriters;
    detail::LockedSingleThread lockedReader;

    QueueItem readCache[detail::mwsrQueueSize - 1u];
    size_t readCacheBegin{ 0u };
    size_t readCacheEnd{ 0u };

    constexpr static size_t getQueueIndex(uint64_t id)
    {
        return id % detail::mwsrQueueSize;
    }

public:
    static_assert(std::is_default_constructible_v<QueueItem>, "QueueItem used in mwsrQueue must be default-constructible!");
    static_assert(std::is_move_assignable_v<QueueItem>, "QueueItem must be move-assignable!");

    mwsrQueue() : entranceData({ detail::ExitReactorData::EntranceFirstToWrite, detail::ExitReactorData::EntranceLastToWrite }) {}
    mwsrQueue(const mwsrQueue&) = delete;
    mwsrQueue& operator=(const mwsrQueue&) = delete;

    bool empty() const noexcept
    {
        return readCacheBegin != readCacheEnd;
    }

    void push(QueueItem&& item)
    {
        detail::EntranceReactorHandle entrance(entranceData);
        auto[ newId, willLock ] = entrance.allocateNextID();
        if (willLock)
        {
            lockedWriters.lockAndWait(newId);
            entrance.unlock();
        }

        size_t idx = getQueueIndex(newId);
        items[idx] = std::move(item);

        detail::ExitReactorHandle exit(exitData);
        bool unlock = exit.writeCompleted(newId);
        if (unlock)
        {
            lockedReader.unlock();
        }
    }

    QueueItem pop()
    {
        if (readCacheBegin < readCacheEnd)
        {
            return std::move(readCache[readCacheBegin++]);
        }

        assert(readCacheBegin == readCacheEnd);

        while (true)
        {
            detail::ExitReactorHandle exit(exitData);

            auto[ numRead, firstId ] = exit.startRead();
            assert(numRead <= detail::mwsrQueueSize);

            if (!numRead)
            {
                lockedReader.lockAndWait();
                continue;
            }

            size_t queueIndex = getQueueIndex(firstId);
            QueueItem resultItem = std::move(items[queueIndex]);
            assert(readCacheBegin == readCacheEnd);
            readCacheBegin = 0u;
            readCacheEnd = 0u;

            for (size_t i = 1; i < numRead; ++i)
            {
                readCache[readCacheEnd++] = std::move(items[getQueueIndex(firstId + i)]);
            }
            assert(readCacheEnd < detail::mwsrQueueSize - 1u);

            const uint64_t newLastWrite = exit.readCompleted(size, firstId);

            detail::EntranceReactorHandle entrance(entranceData);
            const bool shouldUnlock = entrance.moveLastToWrite(newLastWrite);
            if (shouldUnlock)
            {
                lockedWriters.unlockUpTo(firstId + numRead - 1u + detail::mwsrQueueSize);
            }

            return resultItem;
        }
    }

};

#endif //!CORE_THREADING_MWSR_QUEUE_HPP
