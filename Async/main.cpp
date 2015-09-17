#include "Async/Task.h"

#include <atomic>
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
    int x = 0;
    Async::Task<void> f_void(Test::TestQueue1, [&x]() {
        x++;
    });
    
    f_void.get();
    assert(x == 1);
    
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
    int x = 0;
    Async::Task<void> f_void = Async::CreateTask(Test::TestQueue1, [&x]() {
        x++;
    });
    
    f_void.get();
    assert(x == 1);
    
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
    int x = 0;
    Async::Task<void> f_void = Async::CreateTask(Test::TestQueue1, [&x]() {
        x++;
    }).then([&x]() {
        x += 2;
    });
    
    f_void.get();
    assert(x == 3);
    
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

void testContinuationTasksDifferentTypes()
{
    Async::Task<double> f_dbl = Async::CreateTask(Test::TestQueue1, []() {
        return 444;
    }).then([](int x) {
        return 2.0*x + 1;
    });
    
    int y_dbl = f_dbl.get();
    assert(y_dbl == 889.0);
    
    Async::Task<int> f_int = Async::CreateTask(Test::TestQueue1, []() {
        return M_PI;
    }).then([](double x) {
        return (int)floor(x);
    });
    
    double y_int = f_int.get();
    assert(y_int == 3);
    
    Async::Task<size_t> f_size = Async::CreateTask(Test::TestQueue1, []() -> std::string {
        return "Hello World";
    }).then([](const std::string& s) {
        return s.size();
    });
    
    size_t y_size = f_size.get();
    assert(y_size == 11);
}

void testContinuationTasksAfterGet()
{
    int x = 0;
    Async::Task<void> f_void = Async::CreateTask(Test::TestQueue1, [&x]() {
        x++;
    });
    f_void.get();
    
    Async::Task<void> f_void2 = f_void.then([&x]() {
        x += 2;
    });
    
    f_void2.get();
    assert(x == 3);
    
    Async::Task<int> f_int = Async::CreateTask(Test::TestQueue1, []() {
        return 444;
    });
    f_int.get();
    
    Async::Task<int> f_int2 = f_int.then([](int x) {
        return 2*x + 1;
    });
    
    int y_int2 = f_int2.get();
    assert(y_int2 == 889);
    
    Async::Task<double> f_double = Async::CreateTask(Test::TestQueue1, []() {
        return M_PI;
    });
    f_double.get();

    Async::Task<double> f_double2 = f_double.then([](double x) {
        return 2*x + 1;
    });
    
    double y_double2 = f_double2.get();
    assert(fabs(y_double2 - (2*M_PI + 1)) < 1e-8);

    
    Async::Task<std::string> f_str = Async::CreateTask(Test::TestQueue1, []() -> std::string {
        return "Hello World";
    });
    f_str.get();
    
    Async::Task<std::string> f_str2 = f_str.then([](const std::string& s) {
        std::string rev = s;
        std::reverse(rev.begin(), rev.end());
        return rev;
    });
    
    std::string y_str2 = f_str2.get();
    assert(y_str2 == "dlroW olleH");
}

void testWhenAny()
{
    std::atomic_int count(0);
    
    Async::Task<void> t0 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        count++;
    });

    Async::Task<void> t1 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        count++;
    });

    Async::Task<void> t2 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        count++;
    });
    
    auto tasks = {t0, t1, t2};
    Async::Task<std::vector<Async::Task<void>>> anyTask = Async::WhenAny(Test::TestQueue2, begin(tasks), end(tasks));
    std::vector<Async::Task<void>> completed = anyTask.get();

    assert(completed.size() > 0);
    assert(count > 0);
    
    // make sure all tasks are completed before returning from this test
    t0.get();
    t1.get();
    t2.get();
}

void testWhenAll()
{
    std::atomic_int count(0);
    
    Async::Task<void> t0 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        count++;
    });
    
    Async::Task<void> t1 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        count++;
    });
    
    Async::Task<void> t2 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        count++;
    });
    
    auto tasks = {t0, t1, t2};
    Async::Task<std::vector<Async::Task<void>>> allTask = Async::WhenAll(Test::TestQueue2, begin(tasks), end(tasks));
    std::vector<Async::Task<void>> completed = allTask.get();
    
    assert(completed.size() == tasks.size());
    assert(count == tasks.size());
    
    // make sure all tasks are completed before returning from this test
    t0.get();
    t1.get();
    t2.get();
}

void testWhenAnyOperator()
{
    std::atomic_int count(0);
    
    Async::Task<void> t1 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        count++;
    });
    
    Async::Task<void> t2 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        count++;
    });
    
    Async::Task<std::vector<Async::Task<void>>> anyTask = t1 || t2;
    std::vector<Async::Task<void>> completed = anyTask.get();
    
    assert(completed.size() > 0);
    assert(count > 0);
    
    // make sure all tasks are completed before returning from this func
    t1.get();
    t2.get();
}

void testWhenAllOperator()
{
    std::atomic_int count(0);

    Async::Task<void> t1 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        count++;
    });
    
    Async::Task<void> t2 = Async::CreateTask(Test::TestQueue1, [&count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        count++;
    });
    
    Async::Task<std::vector<Async::Task<void>>> allTask = t1 && t2;
    std::vector<Async::Task<void>> completed = allTask.get();
    
    assert(completed.size() == 2);
    assert(count == 2);
    
    // make sure all tasks are completed before returning from this func
    t1.get();
    t2.get();
}

int main(int argc, const char* argv[])
{
    setupQueues();
    
    testBasicTasks();
    testCreateTask();
    testContinuationTasks();
    testContinuationTasksDifferentTypes();
    testContinuationTasksAfterGet();
    testWhenAny();
    testWhenAll();
    testWhenAnyOperator();
    testWhenAllOperator();
    
    return 0;
}
