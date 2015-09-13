#pragma once

#include "Async/Queue.h"
#include "Util/StateMachineT.h"

#include <cassert>
#include <future>

ASYNC_BEGIN

namespace Details
{
    class Schedulable
        : public std::enable_shared_from_this<Schedulable>
    {
    public:
        typedef std::shared_ptr<Schedulable> Ptr;
        
        virtual void schedule() = 0;
        virtual bool cancel() = 0;
    };
}

template <typename T>
class Task
    : public std::enable_shared_from_this<Task<T>>
{
public:
    template <typename F>
    Task(uint32_t queueId, const F& f)
    {
        m_work = std::make_shared<Work>(queueId, f);
        m_work->schedule();
    }
    
    bool cancel()
    {
        return m_work->cancel();
    }
    
    bool isCanceled() const
    {
        return m_work->isCanceled();
    }
        
    T get() const
    {
        return m_work->getFuture().get();
    }

    void wait() const
    {
        return m_work->getFuture().wait();
    }
    
    std::shared_future<T> getFuture() const
    {
        return m_work->getFuture();
    }
    
    template <typename Func>
    auto then(const Func& f) -> Task<decltype(f(*reinterpret_cast<T*>(0)))>
    {
        uint32_t queueId = m_work->getJobId() >> 32;
        return then(queueId, f);
    }
    
    template <typename Func>
    auto then(uint32_t queueId, const Func& f) -> Task<decltype(f(*reinterpret_cast<T*>(0)))>
    {
        typedef Task<decltype(f(*reinterpret_cast<T*>(0)))> NextTask;
        
        std::shared_future<T> futCopy = m_work->getFuture();
        auto g = [futCopy, f]()
        {
            T result = futCopy.get();
            return f(result);
        };
        
        typename NextTask::Work::Ptr work = std::make_shared<typename NextTask::Work>(queueId, g);
        m_work->addNextWork(work);
        return NextTask(work);
    }
    
    template <typename Func>
    auto then(const Func& f) -> Task<decltype(f())>
    {
        uint32_t queueId = m_work->jobId >> 32;
        return then(queueId, f);
    }
    
    template <typename Func>
    auto then(uint32_t queueId, const Func& f) -> Task<decltype(f())>
    {
        typedef Task<decltype(f())> NextTask;
        
        std::shared_future<T> futCopy = m_work->fut;
        auto g = [futCopy, f]()
        {
            futCopy.get();
            return f();
        };
        
        typename NextTask::Work::Ptr work = std::make_shared<typename NextTask::Work>(queueId, g);
        m_work->addNextWork(work);
        return NextTask(work);
    }
    
private:
    template <typename S>
    friend class Task;
    
    class Work
        : public Details::Schedulable
    {
    public:
        typedef std::shared_ptr<Work> Ptr;
        
        Work()
        : m_queueId(0)
        {
        }
        
        template <typename F>
        Work(uint32_t queueId0, const F& f)
        : m_queueId(queueId0)
        , m_stateMachine(State_Waiting)
        {
            m_future = m_promise.get_future().share();
            createWorkFunc(f);
            
            // Waiting --(Schedule)--> Scheduled
            // causes function to be enqueued
            auto enqueueFunc = [this](State, State, Transition) {
                // create a shared ptr to this and capture it in the lambda below
                // so that "this" is kept alive until after after the lambda is executed (or canceled)
                Work::Ptr sharedThis = std::dynamic_pointer_cast<Work>(shared_from_this());
                m_jobId = Async::enqueue(m_queueId, [sharedThis]() {
                    sharedThis->m_stateMachine.executeTransition(Transition_RunStart);
                    sharedThis->m_stateMachine.executeTransition(Transition_RunEnd);
                });
            };
            m_stateMachine.addTransition(State_Waiting, State_Scheduled, Transition_Schedule, enqueueFunc);
            
            // Scheduled --(RunStart)--> Running
            // causes work to be run
            auto runWork = [this](State, State, Transition) {
                m_func();
            };
            m_stateMachine.addTransition(State_Scheduled, State_Running, Transition_RunStart, runWork);
            
            // Running --(RunEnd)--> Completed
            // causes next work items to be scheduled
            auto scheduleNexts = [this](State, State, Transition) {
                scheduleNextWork();
            };
            m_stateMachine.addTransition(State_Running, State_Completed, Transition_RunEnd, scheduleNexts);
            
            // {Waiting, Scheduled} --(Cancel)--> Canceled
            // cancel work function from being executed
            auto cancelWork = [this](State, State, Transition) {
                Async::cancel(m_jobId);
                m_jobId = 0;
            };
            m_stateMachine.addTransition(State_Waiting, State_Canceled, Transition_Cancel, cancelWork);
            m_stateMachine.addTransition(State_Scheduled, State_Canceled, Transition_Cancel, cancelWork);
        }
        
        virtual void schedule() override
        {
            m_stateMachine.executeTransition(Transition_Schedule);
        }
        
        virtual bool cancel() override
        {
            State newState = m_stateMachine.executeTransition(Transition_Cancel);
            return (newState == State_Canceled);
        }
        
        bool isCanceled() const
        {
            State curState = m_stateMachine.getCurrentState();
            return (curState == State_Canceled);
        }
        
        uint64_t getJobId() const
        {
            return m_jobId;
        }
        
        std::shared_future<T>& getFuture()
        {
            return m_future;
        }
        
        bool addNextWork(Details::Schedulable::Ptr next)
        {
            bool added = false;
            
            std::lock_guard<std::mutex> lock(m_stateMachine.getMutex());
            State current = m_stateMachine.getCurrentState();
            switch (current)
            {
                case Work::State_Completed:
                {
                    next->schedule();
                    added = true;
                    break;
                }
                case Work::State_Canceled:
                    // do nothing, work was canceled
                    break;
                default:
                {
                    // all other states (waiting, scheduled, running)
                    // can allow the work the be queued up
                    m_nextWork.push_back(next);
                    added = true;
                    break;
                }
            }
            
            return added;
        }
        
    private:
        enum State
        {
            State_Waiting,
            State_Scheduled,
            State_Running,
            State_Completed,
            State_Canceled
        };
        
        enum Transition
        {
            Transition_Schedule,
            Transition_RunStart,
            Transition_RunEnd,
            Transition_Complete,
            Transition_Cancel
        };
        
        template <typename F>
        void createWorkFunc(const F& f)
        {
            m_func = [this, f]() {
                T val = f();
                m_promise.set_value(val);
            };
        }
        
        void scheduleNextWork()
        {
            std::vector<typename Details::Schedulable::Ptr> nextWorkCopy;
            {
                std::lock_guard<std::mutex> lock(m_stateMachine.getMutex());
                State current = m_stateMachine.getCurrentState();
                assert(current == Work::State_Completed);
                nextWorkCopy = m_nextWork;
                m_nextWork.clear();
            }
            
            for (auto next : nextWorkCopy)
                next->schedule();
        }
        
        uint32_t m_queueId = 0;
        std::function<void(void)> m_func;
        std::promise<T> m_promise;
        std::shared_future<T> m_future;
        uint64_t m_jobId = 0;
        std::vector<typename Details::Schedulable::Ptr> m_nextWork;
        Util::StateMachineT<State, Transition> m_stateMachine;
    };
    
    Task(typename Work::Ptr work)
        : m_work(work)
    {
    }
    
    typename Work::Ptr m_work;
};

template <>
template <typename F>
void Task<void>::Work::createWorkFunc(const F& f)
{
    m_func = [this, f]() {
        f();
        m_promise.set_value();
    };
}

template <typename Func>
auto CreateTask(const Func& f) -> Task<decltype(f())>
{
    return Task<decltype(f())>(f);
}

ASYNC_END

