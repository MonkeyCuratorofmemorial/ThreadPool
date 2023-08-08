#include <iostream>
#include <functional>
#include <future>
#include <chrono>
#include <thread>
#include <unordered_map>
#include "threadpool.h"

using uLong = unsigned long long;

class MyTask : public Task {
public:
	MyTask(int begin, int end) : begin_(begin), end_(end){};
	//问题一：怎么设计run函数的返回值，可以表示任意的类型
	Any run() {//run方法最终在线程池分配的线程中去做事情啦
		std::cout << "begin threadFunc" << std::this_thread::get_id() << std::endl;
		/*std::this_thread::sleep_for(std::chrono::seconds(2));*/
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++) sum += i;
		std::cout << "end threadFunc" << std::this_thread::get_id() << std::endl;
		return sum;
	}

private:
	int begin_;
	int end_;
};

int sum1(int a, int b) {
	return a + b;
}

//template<typename T>
//void test() {
//	std::packaged_task<T(T, T)> task(sum1);
//	std::future<T> res = task.get_future();
//	std::thread t(std::move(task),10, 20);
//	t.detach();
//
//	std::cout << res.get() << std::endl;
//}

int main() {

	//问题：ThreadPool对象析构以后，把线程池相关的线程资源全部回收
	{
		ThreadPool pool;

		//自己设置线程池的工作模式
		pool.setMode(PoolMode::MODE_CACHED);
		//开始启动线程池
		pool.start(4);

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(10000001, 20000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(20000001, 30000000));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(30000001, 40000000));
		pool.submitTask(std::make_shared<MyTask>(40000001, 50000000));
		pool.submitTask(std::make_shared<MyTask>(50000001, 60000000));
		uLong sum1 = res1.get().cast_<uLong>();//get返回了一个Any类型，如何转回实质类型
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();
		uLong sum4 = res4.get().cast_<uLong>();

		//Master - Slave线程模型
		//Master线程用来分解任务，然后给各个Salve线程分配任务
		std::cout << (sum1 + sum2 + sum3 + sum4) << std::endl;
	}
	
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	getchar();
}