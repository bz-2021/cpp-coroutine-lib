#include "coroutine.h"
#include <vector>

using namespace coroutine;

class Scheduler {
public:
    void schedule(std::shared_ptr<Coroutine> task) {
        m_tasks.push_back(task);
    }
    void run() {
        std::cout << " number of tasks: " << m_tasks.size() << std::endl;
        
        std::shared_ptr<Coroutine> task;
        auto it = m_tasks.begin();
        while (it != m_tasks.end()) {
            task = *it;
            task->resume();
            it++;
        }
        m_tasks.clear();
    }

private:
    std::vector<std::shared_ptr<Coroutine>> m_tasks;
};

void test_task(int i) {
    std::cout << " task id: " << i << " is running "<< std::endl;
}

int main() {
    // 初始化主线程
    Coroutine::GetThis();

    Scheduler sc;

    for (auto i = 0; i < 20; i++) {
        std::shared_ptr<Coroutine> task = std::make_shared<Coroutine>(std::bind(test_task, i), 0, false);
        sc.schedule(task);
    }

    sc.run();

    return 0;
}