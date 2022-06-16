#include <iostream>
#include <string>
#include <utility>
#include <thread>
#include <chrono>
#include <cassert>
#include <deque>
#include <compare>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <gc/gc.h>

template<class... Args>
void writeln(Args&&... args) {
    ((std::cout << std::forward<Args>(args)), ...) << std::endl;
}

struct FurtherestNested
{
    gc::vector<gc::graph_ptr<int>> stuff;

    FurtherestNested()
    {
        stuff.reserve(3);
        stuff.push_back(gc::graph::make<int>(1));
        stuff.push_back(gc::graph::make<int>(2));
        stuff.push_back(gc::graph::make<int>(3));
    }
};

struct FurtherNested
{
    gc::vector<FurtherestNested> stuff;
    
    FurtherNested()
    {
        stuff.reserve(3);
        stuff.emplace_back(FurtherestNested());
        stuff.emplace_back(FurtherestNested());
        stuff.emplace_back(FurtherestNested());
    }
};

struct Nested
{
    std::string value;

    gc::vector<gc::graph_ptr<FurtherNested>> stuff;

    Nested(const std::string& value)
        : value(value)
    {
        stuff.reserve(3);
        stuff.push_back(gc::graph::make<FurtherNested>());
        stuff.push_back(gc::graph::make<FurtherNested>());
        stuff.push_back(gc::graph::make<FurtherNested>());
    }
};

class Test : public gc::graph_object<Test>
{
public:
    int x = 1;
    int y = 2;
    gc::vector<gc::graph_ptr<Nested>> objects;

    gc::graph_ptr<Test> otherTest;
    gc::function<void()> func;
    
    Test()
    {
        objects.reserve(3);
        objects.push_back(gc::graph::make<Nested>("one"));
        objects.push_back(gc::graph::make<Nested>("two"));
        objects.push_back(gc::graph::make<Nested>("three"));

        gc::graph_ptr<std::string> num1 = gc::graph::make<std::string>("one");
        gc::graph_ptr<std::string> num2 = gc::graph::make<std::string>("two");
        gc::graph_ptr<std::string> num3 = gc::graph::make<std::string>("three");
        func = [=]{
            std::cout << *num1 << std::endl;
            std::cout << *num2 << std::endl;
            std::cout << *num3 << std::endl;
        };
    }

    void Fun() const
    {
        gc::graph_ptr<const Test> gp = self(this);
        printf("%d, %d\n", gp->x, gp->y);
    }

    void Fun2() {
        func();
    }
};

std::atomic<bool> run = true;

void DoTest(int id)
{
    while (run)
    {
        for(int i = 0; i != 50; ++i)
        {
            gc::graph_ptr<Test> testA = gc::graph::make<Test>();
            gc::graph_ptr<Test> testB = gc::graph::make<Test>();

            // intentionally create cycle
            testA->otherTest = testB;
            testB->otherTest = testA;

            for (int j = 0; j != 1000; ++j)
            {
                gc::graph_ptr<Test> test2 = testA;
                assert(test2);

                gc::graph_ptr<Test> test3 = std::move(test2);
                assert(test3);

                test2.reset();
                
                gc::graph_ptr<Test> test4;
                test4 = test3;
                assert(test4);

                test3.reset();

                gc::graph_ptr<Test> test5 = test4;
                assert(test5);

                gc::weak_graph_ptr<Test> wk = test5;

                auto test6 = wk.lock();
                assert(test6);
            }

            testA.reset();
            testB.reset();
        
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

int main(int argc, char* argv[])
{
    std::vector<std::thread> tests;
    int numThreads = 4;

    tests.reserve(numThreads);

    printf("Starting Test\n");
    
    for (int i = 0; i != numThreads; ++i) {
        tests.emplace_back(std::thread(&DoTest, i));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (int i = 0; i != 10; ++i)
    {
        (void)gc::graph::collect();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    run = false;

    for (int i = 0; i != numThreads; ++i) {
        tests[i].join();
    }

    (void)gc::graph::collect();
    
    printf("Test Completed (%d allocations leaked)\n", gc::graph::allocated_objects());

    return 0;
}
