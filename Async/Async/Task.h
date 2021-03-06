#pragma once

#include "Async/Queue.h"
#include "Util/StateMachineT.h"

#include <algorithm>
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
        
        virtual uint32_t getQueueId() const = 0;
        virtual uint64_t getJobId() const = 0;
        virtual bool schedule() = 0;
        virtual bool cancel() = 0;
    };
}

template <typename T>
class Task
{
public:
    typedef std::function<void(Task<T>)> CompletionFunc;
    
    template <typename F>
    Task(uint32_t queueId, const F& f)
    {
        m_work = std::make_shared<Work>(queueId, f);
        m_work->schedule();
    }
    
    uint32_t getQueueId() const
    {
        return m_work->getQueueId();
    }
    
    uint64_t getJobId() const
    {
        return m_work->getJobId();
    }
    
    uint32_t addCompletionHandler(const CompletionFunc& handler)
    {
        Task<T> thisCopy = *this;
        return m_work->addCompletionHandler([thisCopy, handler]() {
            handler(thisCopy);
        });
    }
    
    bool removeCompletionHandler(uint32_t token)
    {
        return m_work->removeCompletionHandler(token);
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
        // if the task has been canceled, this should probably throw an exception
        return m_work->getFuture().get();
    }

    void wait() const
    {
        // if the task has been canceled, this should probably throw an exception
        return m_work->getFuture().wait();
    }
    
    std::shared_future<T> getFuture() const
    {
        // if the task has been canceled, this should _possibly_ throw an exception
        return m_work->getFuture();
    }
    
    template <typename Func>
    auto then(const Func& f) -> Task<decltype(f(*reinterpret_cast<T*>(0)))>
    {
        uint32_t queueId = m_work->getQueueId();
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
        uint32_t queueId = m_work->getQueueId();
        return then(queueId, f);
    }
    
    template <typename Func>
    auto then(uint32_t queueId, const Func& f) -> Task<decltype(f())>
    {
        typedef Task<decltype(f())> NextTask;
        
        std::shared_future<T> futCopy = m_work->getFuture();
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
        typedef std::function<void(void)> CompletionFunc;
        
        
        Work()
        : m_queueId(0)
        {
        }
        
        template <typename F>
        Work(uint32_t queueId0, const F& f)
            : m_queueId(queueId0)
            , m_stateMachine(State::Waiting)
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
                    sharedThis->m_stateMachine.executeTransition(Transition::RunStart);
                    sharedThis->m_stateMachine.executeTransition(Transition::RunEnd);
                });
            };
            m_stateMachine.addTransition(State::Waiting, State::Scheduled, Transition::Schedule, enqueueFunc);
            
            // Scheduled --(RunStart)--> Running
            // causes work to be run
            auto runWork = [this](State, State, Transition) {
                m_func();
            };
            m_stateMachine.addTransition(State::Scheduled, State::Running, Transition::RunStart, runWork);
            
            // Running --(RunEnd)--> Completed
            // causes next work items to be scheduled
            auto workCompleted = [this](State, State, Transition) {
                notifyCompletionHandlers();
                scheduleNextWork();
            };
            m_stateMachine.addTransition(State::Running, State::Completed, Transition::RunEnd, workCompleted);
            
            // {Waiting, Scheduled} --(Cancel)--> Canceled
            // cancel work function from being executed
            auto cancelWork = [this](State, State, Transition) {
                Async::cancel(m_jobId);
                m_jobId = 0;
            };
            m_stateMachine.addTransition(State::Waiting, State::Canceled, Transition::Cancel, cancelWork);
            m_stateMachine.addTransition(State::Scheduled, State::Canceled, Transition::Cancel, cancelWork);
        }
        
        uint32_t getQueueId() const override
        {
            return m_jobId >> 32;
        }
        
        uint64_t getJobId() const override
        {
            return m_jobId;
        }
        
        virtual bool schedule() override
        {
            State newState = m_stateMachine.executeTransition(Transition::Schedule);
            return (newState == State::Scheduled);
        }
        
        virtual bool cancel() override
        {
            State newState = m_stateMachine.executeTransition(Transition::Cancel);
            return (newState == State::Canceled);
        }
        
        bool isCanceled() const
        {
            State curState = m_stateMachine.getCurrentState();
            return (curState == State::Canceled);
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
                case Work::State::Completed:
                {
                    next->schedule();
                    added = true;
                    break;
                }
                case Work::State::Canceled:
                    // do nothing, work was canceled
                    // throw an exception here?
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
        
        uint32_t addCompletionHandler(const CompletionFunc& handler)
        {
            uint32_t token = 0;
            
            bool callNow = false;
            {
                std::lock_guard<std::mutex> lock(m_stateMachine.getMutex());
                State current = m_stateMachine.getCurrentState();
                switch (current) {
                    case State::Completed:
                        callNow = true;
                        break;
                    default:
                        token = m_nextCompletionHandlerToken++;
                        m_completionHandlers.insert({token, handler});
                        break;
                }
            }
            
            if (callNow)
                handler();
            
            return token;
        }
        
        bool removeCompletionHandler(uint32_t token)
        {
            std::lock_guard<std::mutex> lock(m_stateMachine.getMutex());
            auto it = m_completionHandlers.find(token);
            if (it == m_completionHandlers.end())
                return false;
            
            m_completionHandlers.erase(it);
            return true;
        }
        
    private:
        enum class State
        {
            Waiting,
            Scheduled,
            Running,
            Completed,
            Canceled
        };
        
        enum class Transition
        {
            Schedule,
            RunStart,
            RunEnd,
            Complete,
            Cancel
        };
        
        template <typename F>
        void createWorkFunc(const F& f)
        {
            m_func = [this, f]() {
                T val = f();
                m_promise.set_value(val);
            };
        }
        
        void notifyCompletionHandlers()
        {
            std::map<uint32_t, CompletionFunc> completionHandlersCopy;
            {
                std::lock_guard<std::mutex> lock(m_stateMachine.getMutex());
                State current = m_stateMachine.getCurrentState();
                assert(current == Work::State::Completed);
                completionHandlersCopy = m_completionHandlers;
                m_completionHandlers.clear();
            }
            
            for (auto it : completionHandlersCopy)
                it.second();
        }
        
        void scheduleNextWork()
        {
            std::vector<typename Details::Schedulable::Ptr> nextWorkCopy;
            {
                std::lock_guard<std::mutex> lock(m_stateMachine.getMutex());
                State current = m_stateMachine.getCurrentState();
                assert(current == Work::State::Completed);
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
        std::map<uint32_t, CompletionFunc> m_completionHandlers;
        uint32_t m_nextCompletionHandlerToken = 0;
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
auto CreateTask(uint32_t queueId, const Func& f) -> Task<decltype(f())>
{
    return Task<decltype(f())>(queueId, f);
}

template <typename Iter>
auto WhenAny(uint32_t queueId, Iter begin, Iter end) -> Task<std::vector<Task<decltype(begin->get())>>>
{
    using TaskType = Task<decltype(begin->get())>;
    using TaskVector = std::vector<TaskType>;
    
    // make a copy of the input
    TaskVector tasks;
    std::copy(begin, end, std::back_inserter(tasks));
    
    auto f = [tasks]() {
        std::mutex mutex;
        std::condition_variable cond;
        TaskVector completed;
        
        auto completionCallback = [&mutex, &cond, &completed](TaskType task) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                completed.push_back(task);
            }
            cond.notify_one();
        };
        
        std::vector<uint32_t> tokens;
        for (auto t : tasks)
        {
            uint32_t token = t.addCompletionHandler(completionCallback);
            tokens.push_back(token);
        }
        
        std::unique_lock<std::mutex> lock(mutex);
        while (completed.size() == 0)
            cond.wait(lock);

        size_t i = 0;
        for (auto t : tasks)
        {
            uint32_t token = tokens[i++];
            t.removeCompletionHandler(token);
        }
        
        return completed;
    };
    
    return CreateTask(queueId, f);
}

template <typename Iter>
auto WhenAll(uint32_t queueId, Iter begin, Iter end) -> Task<std::vector<Task<decltype(begin->get())>>>
{
    using TaskType = Task<decltype(begin->get())>;
    using TaskVector = std::vector<TaskType>;
    
    // make a copy of the input
    TaskVector tasks;
    std::copy(begin, end, std::back_inserter(tasks));

    auto f = [tasks]() {
        std::mutex mutex;
        std::condition_variable cond;
        TaskVector completed;
        
        auto completionCallback = [&mutex, &cond, &completed](TaskType task) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                completed.push_back(task);
            }
            cond.notify_one();
        };
        
        uint32_t total = 0;
        std::vector<uint32_t> tokens;
        for (auto t : tasks)
        {
            uint32_t token = t.addCompletionHandler(completionCallback);
            tokens.push_back(token);
            ++total;
        }
        
        std::unique_lock<std::mutex> lock(mutex);
        while (completed.size() < total)
            cond.wait(lock);
        
        size_t i = 0;
        for (auto t : tasks)
        {
            uint32_t token = tokens[i++];
            t.removeCompletionHandler(token);
        }

        return completed;
    };
    
    return CreateTask(queueId, f);
}

template <typename T>
Task<std::vector<Task<T>>> operator||(const Task<T>& a, const Task<T>& b)
{
    auto tasks = {a,b};
    return WhenAny(a.getQueueId(), begin(tasks), end(tasks));
}

template <typename T>
Task<std::vector<Task<T>>> operator&&(const Task<T>& a, const Task<T>& b)
{
    auto tasks = {a,b};
    return WhenAll(a.getQueueId(), begin(tasks), end(tasks));
}

ASYNC_END

