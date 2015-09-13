#pragma once

#include "Async/Base.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

ASYNC_BEGIN

typedef std::function<void(void)> VoidFunc;

class Queue
    : public std::enable_shared_from_this<Queue>
{
public:
    typedef std::shared_ptr<Queue> Ptr;
    typedef std::weak_ptr<Queue> WeakPtr;
    
    Queue();
    Queue(uint32_t queueId);
    virtual ~Queue();
    
    uint32_t getId();
    
    template <typename F>
    uint64_t enqueue(const F& func)
    {
        uint64_t jobId = m_queueId;
        jobId <<= 32;
        jobId += m_nextJobNumber++;
        
        {
            std::unique_lock<std::mutex> lock(m_jobsMutex);
            
            Job job;
            job.id = jobId;
            job.func = [=]() {
                func();
            };
            m_jobs.push_back(job);
        }
        
        newJobAdded();
        return jobId;
    }
    
    bool cancel(uint64_t jobId);
    bool empty();
    bool runNext();
    
private:
    virtual void newJobAdded();
    
    class Job
    {
    public:
        uint64_t id = 0;
        VoidFunc func;
    };
    
    typedef std::deque<Job> Jobs;
    
    uint32_t m_queueId;
    std::mutex m_jobsMutex;
    uint32_t m_nextJobNumber = 1;
    Jobs m_jobs;
};

void registerQueue(Queue::Ptr q);
bool unregisterQueue(uint32_t queueId);

template <typename F>
uint64_t enqueue(uint32_t queueId, const F& func)
{
    VoidFunc vf = [func]() {
        func();
    };
    return enqueue(queueId, vf);
}

template <>
uint64_t enqueue<VoidFunc>(uint32_t queueId, const VoidFunc& func);

bool cancel(uint64_t jobId);

class ThreadPoolQueue
    : public Queue
{
public:
    typedef std::shared_ptr<ThreadPoolQueue> Ptr;
    typedef std::weak_ptr<ThreadPoolQueue> WeakPtr;
    
    ThreadPoolQueue(uint32_t numThreads);
    ThreadPoolQueue(uint32_t queueId, uint32_t numThreads);
    ~ThreadPoolQueue();
    
    void stop();
    
private:
    void init(uint32_t numThreads);
    void run();
    virtual void newJobAdded() override;
    
    bool m_running = true;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::vector<std::thread> m_threads;
};


ASYNC_END