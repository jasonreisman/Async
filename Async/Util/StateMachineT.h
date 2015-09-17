#pragma once

#include "Util/Base.h"

#include <functional>
#include <map>
#include <mutex>

UTIL_BEGIN

template <typename State, typename Transition>
class StateMachineT
{
public:
    typedef std::function<void(State, State, Transition)> SideEffect;
    
    StateMachineT(const State& initial)
        : m_current(initial)
    {
    }
    
    State getCurrentState() const
    {
        return m_current;
    }
    
    bool addTransition(const State& from, const State& to, const Transition& trans, const SideEffect& effect, bool synchronous=false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        TransitionKey key(from, trans);
        if (m_transitions.find(key) != m_transitions.end())
            return false;
        
        TransitionData data = {from, to, trans, effect, synchronous};
        m_transitions.insert({key, data});
        return true;
    }
    
    State executeTransition(const Transition& trans)
    {
        SideEffect effect;
        State oldState = m_current;
        State newState = m_current;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            TransitionKey key(m_current, trans);
            auto it = m_transitions.find(key);
            if (it == m_transitions.end())
                return m_current;
            
            // transition to new state
            const TransitionData& data = it->second;
            oldState = data.from;
            newState = data.to;
            m_current = data.to;
            
            // determine if the side effect should be run inside or outside of the lock
            if (data.synchronous)
                data.effect(oldState, newState, trans);
            else
                effect = data.effect;
        }
        
        // execute the side effect if not synchronous
        if (effect)
            effect(oldState, newState, trans);
        
        return newState;
    }
    
    std::mutex& getMutex()
    {
        return m_mutex;
    }
    
private:
    
    struct TransitionData
    {
        State from;
        State to;
        Transition trans;
        SideEffect effect;
        bool synchronous;
    };
    
    typedef std::pair<State, Transition> TransitionKey;
    typedef std::map<TransitionKey, TransitionData> TransitionMap;
    
    State m_current;
    TransitionMap m_transitions;
    std::mutex m_mutex;
};

UTIL_END
