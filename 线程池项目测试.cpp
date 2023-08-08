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
	//����һ����ô���run�����ķ���ֵ�����Ա�ʾ���������
	Any run() {//run�����������̳߳ط�����߳���ȥ��������
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

	//���⣺ThreadPool���������Ժ󣬰��̳߳���ص��߳���Դȫ������
	{
		ThreadPool pool;

		//�Լ������̳߳صĹ���ģʽ
		pool.setMode(PoolMode::MODE_CACHED);
		//��ʼ�����̳߳�
		pool.start(4);

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(10000001, 20000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(20000001, 30000000));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(30000001, 40000000));
		pool.submitTask(std::make_shared<MyTask>(40000001, 50000000));
		pool.submitTask(std::make_shared<MyTask>(50000001, 60000000));
		uLong sum1 = res1.get().cast_<uLong>();//get������һ��Any���ͣ����ת��ʵ������
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();
		uLong sum4 = res4.get().cast_<uLong>();

		//Master - Slave�߳�ģ��
		//Master�߳������ֽ�����Ȼ�������Salve�̷߳�������
		std::cout << (sum1 + sum2 + sum3 + sum4) << std::endl;
	}
	
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	getchar();
}