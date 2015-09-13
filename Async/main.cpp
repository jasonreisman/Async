#include "Async/Task.h"

#include <iostream>

namespace Test
{
    const uint32_t TestQueue1 = 444;
    const uint32_t TestQueue2 = 999;
    const uint32_t NumThreads = 1;
}

enum FooState
{
    FooState_A,
    FooState_B,
    FooState_C
};

enum FooTransition
{
    FooTransition_1,
    FooTransition_2,
};

void setupQueues()
{
    Async::ThreadPoolQueue::Ptr queue1 = std::make_shared<Async::ThreadPoolQueue>(Test::TestQueue1, Test::NumThreads);
    Async::registerQueue(queue1);

    Async::ThreadPoolQueue::Ptr queue2 = std::make_shared<Async::ThreadPoolQueue>(Test::TestQueue2, Test::NumThreads);
    Async::registerQueue(queue2);
}


int main(int argc, const char* argv[])
{
    auto noop = [](FooState, FooState, FooTransition) {};
    Util::StateMachineT<FooState, FooTransition> sm(FooState_A);
    sm.addTransition(FooState_A, FooState_B, FooTransition_1, noop);
    FooState next = sm.executeTransition(FooTransition_1);
    FooState next2 = sm.executeTransition(FooTransition_1);
    
    setupQueues();
    
    Async::Task<int> f_int(Test::TestQueue1, []() {
        std::cout << "Hello 444" << std::endl;
        return 444;
    });
    
    Async::Task<int> f_int2 = f_int.then([](int x){
        std::cout << "Hello " << (x+1) << std::endl;
        return x+1;
    });

    Async::Task<void> f_intThenVoid = f_int.then([](int x){
        std::cout << "Goodbye " << 2*x << std::endl;
    });
    
    int y_int = f_int.get();
    int y_int2 = f_int2.get();
    f_intThenVoid.wait();
    
    Async::Task<int> f_int3 = f_int.then([](int x){
        std::cout << "Hello " << (x+2) << std::endl;
        return x+2;
    });
    int y_int3 = f_int3.get();
    
    Async::Task<void> f_void(Test::TestQueue1, [](){
        std::cout << "Hello" << std::endl;
    });
    f_void.get();
    
    return 0;
}
