#pragma once
#include <atomic>
#include <memory>
#include <iterator>
#include <type_traits>

struct MCAS
{

    struct Helper;

    struct Row
    {
        const void* address;
        const void* expectedValue;
        const void* newValue;
        Helper* helperPtr{ nullptr };
    };

    struct Descriptor
    {

        std::unique_ptr<Row[]> rows;
        const size_t tail = 0x1;
    };

    struct Helper
    {
        Row* current{ nullptr };
    };

    struct Signal
    {

    };

    bool Invoke(Row* first, Row* last)
    {
        thread_local auto localCas = first;
        placeHelper(localCas++, last, false, 0);
        while (localCas != last)
        {
            if (last->helperPtr == nullptr)
            {
                placeHelper(localCas++, last, false, 0);
            }
            else
            {
                break;
            }
        }
    }

private:

    // examine other thread to stop it from being pre-empted
    void helpIfNeeded();
    void placeHelper(Row* cr, Row* lastRow, bool firstTime, int depth)
    {
        
    }

    Helper* allocateHelper(Row* row)
    {
        
    }

    static thread_local size_t threadID;
    static thread_local size_t checkID;
    const size_t nThreads;
    const size_t maxFailures;
    std::unique_ptr<Signal[]> opTable;
};
