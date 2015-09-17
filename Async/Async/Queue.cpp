#include "Async/Queue.h"

#include <cassert>
#include <map>

ASYNC_BEGIN

namespace
{
    std::mutex s_queueIdMutex;
    uint32_t s_nextQueueId;
    
    typedef std::map<uint32_t, Queue::Ptr> QueueMap;
    std::mutex s_queuesMutex;
    QueueMap s_queues;
}

//////////////////////////////////////////////////////
//////////////////////////////////////////////////////
////////// Queue
//////////////////////////////////////////////////////
//////////////////////////////////////////////////////

Queue::Queue()
{
    std::lock_guard<std::mutex> lock(s_queueIdMutex);
    m_queueId = s_nextQueueId;
    s_nextQueueId++;
}

Queue::Queue(uint32_t queueId)
    : m_queueId(queueId)
{
}

Queue::~Queue()
{
}

uint32_t
Queue::getId()
{
    return m_queueId;
}

bool
Queue::cancel(uint64_t jobId)
{
    std::lock_guard<std::mutex> lock(m_jobsMutex);
    
    uint32_t queueId = jobId >> 32;
    if (queueId != m_queueId)
        return false;
    
    Jobs::iterator it = std::find_if(m_jobs.begin(), m_jobs.end(), [jobId](const Job& j) {
        return j.id == jobId;
    });
    
    if (it == m_jobs.end())
        return false;
    
    m_jobs.erase(it);
    return true;
}

bool
Queue::empty()
{
    std::lock_guard<std::mutex> lock(m_jobsMutex);
    return emptyUnprotected();
}

bool
Queue::emptyUnprotected()
{
    return m_jobs.empty();
}

bool
Queue::runNext()
{
    Job j;
    {
        std::lock_guard<std::mutex> lock(m_jobsMutex);
        if (m_jobs.empty())
            return false;
        
        j = m_jobs.front();
        assert(j.id != 0);
        m_jobs.pop_front();
    }
    
    j.func();
    return true;
}

std::mutex&
Queue::getJobsMutex()
{
    return m_jobsMutex;
}

void
Queue::newJobAdded()
{
}

//////////////////////////////////////////////////////
//////////////////////////////////////////////////////
////////// Aysnc Queue functions
//////////////////////////////////////////////////////
//////////////////////////////////////////////////////

void registerQueue(Queue::Ptr q)
{
    std::lock_guard<std::mutex> lock(s_queuesMutex);
    s_queues.insert(std::make_pair(q->getId(), q));
}

bool unregisterQueue(uint32_t queueId)
{
    std::lock_guard<std::mutex> lock(s_queuesMutex);
    
    QueueMap::iterator it = s_queues.find(queueId);
    if (it == s_queues.end())
        return false;
    
    s_queues.erase(it);
    return true;
}

template <>
uint64_t enqueue<VoidFunc>(uint32_t queueId, const VoidFunc& func)
{
    Queue::Ptr q;
    {
        std::lock_guard<std::mutex> lock(s_queuesMutex);
        QueueMap::iterator it = s_queues.find(queueId);
        if (it == s_queues.end())
            return 0;
        
        q = it->second;
    }
    
    assert(q);
    if (!q)
        return 0;
    
    return q->enqueue(func);
}

bool cancel(uint64_t jobId)
{
    uint32_t queueId = jobId >> 32;
    
    Queue::Ptr q;
    {
        std::lock_guard<std::mutex> lock(s_queuesMutex);
        QueueMap::iterator it = s_queues.find(queueId);
        if (it == s_queues.end())
            return false;
        
        q = it->second;
    }
    
    assert(q);
    if (!q)
        return false;
    
    return q->cancel(jobId);
}

//////////////////////////////////////////////////////
//////////////////////////////////////////////////////
////////// ThreadPoolQueue
//////////////////////////////////////////////////////
//////////////////////////////////////////////////////

ThreadPoolQueue::ThreadPoolQueue(uint32_t numThreads)
{
    init(numThreads);
}

ThreadPoolQueue::ThreadPoolQueue(uint32_t queueId, uint32_t numThreads)
    : Queue(queueId)
{
    init(numThreads);
}

ThreadPoolQueue::~ThreadPoolQueue()
{
    stop();
}

void
ThreadPoolQueue::stop()
{
    {
        std::lock_guard<std::mutex> lock(getJobsMutex());
        if (!m_running)
            return;
        
        m_running = false;
        m_cond.notify_all();
    }
    
    for (auto& thread : m_threads)
    {
        thread.join();
    }
    m_threads.clear();
}

void
ThreadPoolQueue::init(uint32_t numThreads)
{
    for (uint32_t i=0; i<numThreads; i++)
    {
        std::thread worker(&ThreadPoolQueue::run, this);
        m_threads.push_back(std::move(worker));
    }
}

void
ThreadPoolQueue::run()
{
    while (m_running)
    {
        {
            // wait until there's work to do
            std::unique_lock<std::mutex> lock(getJobsMutex());
            while (m_running && emptyUnprotected())
                m_cond.wait(lock);
        }
    
        // execute any work on the queue
        while (m_running && runNext()) {}
    }
}

void
ThreadPoolQueue::newJobAdded()
{
    m_cond.notify_one();
}


ASYNC_END