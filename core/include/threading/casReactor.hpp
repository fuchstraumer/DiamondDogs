#pragma once
#ifndef CORE_THREADING_CAS_REACTOR_HPP
#define CORE_THREADING_CAS_REACTOR_HPP
#include "atomic128.hpp"

/*
    Based on https://github.com/ITHare/mtprimitives
    Article: http://ithare.com/cas-reactor-for-non-blocking-multithreaded-primitives/
*/

template<typename ReactorData>
class CasReactorHandle
{
protected:
    atomic128* casBlock;
    ReactorData lastRead;

    static_assert((sizeof(ReactorData) <= 16),
        "ReactorData must be no larger than 16 bytes / 128 bits, in order for it to be CAS reactor compatible!");

    CasReactorHandle(atomic128& cas_block) : casBlock(&cas_block)
    {
        lastRead = casBlock->load();
    }

    template<typename ReturnType, typename Function>
    void React(ReturnType& out, Function&& fn)
    {
        while (true)
        {
            ReactorData new_data = lastRead;
            bool earlyExit = false;
            out = f(new_data, earlyExit);
            // early exit is used to indicate that we didn't end up mutating state, so no need
            // to do the compare-exchange
            if (earlyExit)
            {
                return;
            }

            // if compare-exchange fails, we retry the reactor function using the update data from whoever succeeded
            bool cmpxchgOk = casBlock->compare_exchange_weak(lastRead.data, new_data.data));
            
            if (cmpxchgOk)
            {
                lastRead = new_data;
                return;
            }
        }
    }

    template<typename ReturnType, typename Function>
    void React(ReturnType& out, Function&& fn, uint64_t param)
    {
        while (true)
        {
            ReactorData new_data = lastRead;
            bool earlyExit = false;
            out = f(new_data, param, earlyExit);
            if (earlyExit)
            {
                return;
            }

            bool cmpxchgOk = casBlock->compare_exchange_weak(lastRead.data, new_data.data);
            
            if (cmpxchgOk)
            {
                lastRead = new_data;
                return;
            }
        }
    }

    template<typename ReturnType, typename Function>
    void React(ReturnType& out, Function&& fn, uint64_t p0, uint64_t p1)
    {
        while (true)
        {
            ReactorData new_data = lastRead;
            bool earlyExit = false;
            out = f(new_data, p0, p1, earlyExit);
            if (earlyExit)
            {
                return;
            }

            bool cmpxchgOk = casBlock->compare_exchange_weak(lastRead.data, new_data.data);
            
            if (cmpxchgOk)
            {
                lastRead = new_data;
                return;
            }
        }
    }

};

#endif //!CORE_THREADING_CAS_REACTOR_HPP