#include "Async/Task.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace Test
{
    const uint32_t TestQueue1 = 444;
    const uint32_t TestQueue2 = 999;
    const uint32_t NumThreads = 4;
}

void setupQueues()
{
    Async::ThreadPoolQueue::Ptr queue1 = std::make_shared<Async::ThreadPoolQueue>(Test::TestQueue1, Test::NumThreads);
    Async::registerQueue(queue1);

    Async::ThreadPoolQueue::Ptr queue2 = std::make_shared<Async::ThreadPoolQueue>(Test::TestQueue2, Test::NumThreads);
    Async::registerQueue(queue2);
}

void testBasicTasks()
{
    Async::Task<int> f_int(Test::TestQueue1, []() {
        return 444;
    });
    
    int y_int = f_int.get();
    assert(y_int == 444);
    
    Async::Task<double> f_double(Test::TestQueue1, []() {
        return M_PI;
    });
    
    double y_double = f_double.get();
    assert(fabs(y_double - M_PI) < 1e-8);
    
    Async::Task<std::string> f_str(Test::TestQueue1, []() {
        return "Hello World";
    });
    
    std::string y_str = f_str.get();
    assert(y_str == "Hello World");
}

void testCreateTask()
{
    Async::Task<int> f_int = Async::CreateTask(Test::TestQueue1, []() {
        return 444;
    });
    
    int y_int = f_int.get();
    assert(y_int == 444);
    
    Async::Task<double> f_double = Async::CreateTask(Test::TestQueue1, []() {
        return M_PI;
    });
    
    double y_double = f_double.get();
    assert(fabs(y_double - M_PI) < 1e-8);
    
    Async::Task<std::string> f_str = Async::CreateTask(Test::TestQueue1, []() -> std::string {
        return "Hello World";
    });
    
    std::string y_str = f_str.get();
    assert(y_str == "Hello World");
}

void testContinuationTasks()
{
    Async::Task<int> f_int = Async::CreateTask(Test::TestQueue1, []() {
        return 444;
    }).then([](int x) {
        return 2*x + 1;
    });
    
    int y_int = f_int.get();
    assert(y_int == 889);
    
    Async::Task<double> f_double = Async::CreateTask(Test::TestQueue1, []() {
        return M_PI;
    }).then([](double x) {
        return 2*x + 1;
    });
    
    double y_double = f_double.get();
    assert(fabs(y_double - (2*M_PI + 1)) < 1e-8);
    
    Async::Task<std::string> f_str = Async::CreateTask(Test::TestQueue1, []() -> std::string {
        return "Hello World";
    }).then([](const std::string& s) {
        std::string rev = s;
        std::reverse(rev.begin(), rev.end());
        return rev;
    });
    
    std::string y_str = f_str.get();
    assert(y_str == "dlroW olleH");
}


int main(int argc, const char* argv[])
{
    setupQueues();
    
    testBasicTasks();
    testCreateTask();
    testContinuationTasks();
    
    return 0;
}
