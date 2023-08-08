#include "threadpool.h"
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>


const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;

//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	,idleThreadSize_(0)
	,curThreadSize_(0)
	,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	,threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
	,isPoolRunning_(false){}

ThreadPool::~ThreadPool(){
	isPoolRunning_ = false;
	//等待线程池里面所有的线程返回  有两种状态 ：阻塞或执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool{return threads_.size() == 0; });
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState())
		return;
	poolMode_ = mode;
};

//设置task任务队列的阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
};

//设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}
};

//给线程池提交任务(很重要！！！)
//用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程的通信 等待任务队列有空余
	//用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	/*while (taskQue_.size() == taskQueMaxThreshHold_) {
		notFull_.wait(lock);
	}*/
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; })) {
		//表示notFull_等待1s钟，条件依然没有满足
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp, false);//Task
	}
	
	//若有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	//因为新放了任务，任务队列肯定不空了，在notEmpty_上进行通知
	notEmpty_.notify_all();

	//因为新放了任务，任务队列肯定不为空，在notEmpty_(条件变量)上进行通知，赶快分配线程执行任务
	//cached模式，任务处理比较紧急 场景:小而快的任务 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_) {
		//创建新的线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int ThreadId = ptr->getId();
		threads_.emplace(ThreadId, std::move(ptr));
		threads_[ThreadId]->start();//启动线程
		//修改线程个数相关变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	//返回任务的Result对象
	return Result(sp);
};

//开启线程池
void ThreadPool::start(int initThreadSize) {
	//设置线程池的start
	isPoolRunning_ = true;
	
	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++) {
		//创建thread线程对象的时候，把线程函数给到thread线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		/*threads_.emplace_back(std::move(ptr));*/
	}

	//启动所有线程
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++; //记录初始空闲线程的数量
	}
};

//定义线程函数 线程池的所有线程从线程队列里面消费任务
void ThreadPool::threadFunc(int threadid) {//线程函数返回，相应的线程也就结束啦
	/*std::cout << "begin threadFunc" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc" << std::this_thread::get_id() << std::endl;*/
	auto lastTime = std::chrono::high_resolution_clock().now();

	for (;;) {
		std::shared_ptr<Task> task;
		{	
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() <<"尝试获取任务。。。"<< std::endl;

			//cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，应该把多余的线程回收掉
			//结束回收掉(超过initThreadSize_数量的线程进行回收)
			//当前时间 - 上一次线程执行时间 > 60s
			
			//每一秒钟返回一次当任务队列没有任务， 怎么区别超时返回？ 还是有任务待返回？
			//锁加条件变量双重判断，isPoolRunning_标志线程池是否正常运行
			while (taskQue_.size() == 0) {
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << "exit!!" << std::endl;
					exitCond_.notify_all();
					return;
				}
				if (poolMode_ == PoolMode::MODE_CACHED) {
					//条件变量超时返回
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
							//开始回收当前线程
							//记录线程数量的相关记录的值修改
							//把线程对象从线程列表容器中删除
							//threadid =>thread对象 =》删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid:" << std::this_thread::get_id() << "exit!!！" << std::endl;
							return;
						}
					}
				}
				else {
					//如果条件变量未超时则，等待任务队列notEmpty条件满足，等待提交任务
					notEmpty_.wait(lock);
				}
			}

			//线程池要结束，回收线 程资源
			if (!isPoolRunning_) {
				/*threads_.erase(threadid);
				std::cout << "threadid:" << std::this_thread::get_id() << "exit!!！" << std::endl;
				exitCond_.notify_all();
				return;*/
				break;
			}

			//开始消费线程
			idleThreadSize_--;
			std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功。。。" << std::endl;
			
			//从任务队列中取一个任务出来
			//出现BUG1:在局部作用域中不能再用auto去定义外部变量，内部相当于重新定义，出作用域无意义
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			//若依然有剩余任务，继续通知其他的线程执行任务
			if (taskQue_.size()>0) {
				notEmpty_.notify_all();
			}
			//执行完一个任务，得进行通知可以继续提交生产任务
			notFull_.notify_all();
		}//就应该把锁释放掉

		//当前线程负责执行这个任务
		if (task != nullptr) {
			/*task->run();*/
			task->exec();
		}	
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//更新线程执行完任务的时间
	}
};

bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

/////////线程方法实现
int Thread::generateId_ = 0;

//线程构造
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++) {};
//线程析构
Thread::~Thread() {

};
//开启线程
void Thread::start() {
	//创建一个线程来执行一个线程函数

	//线程对象在局部作用域中，当出作用域时，对象自动回收
	//所以使用分离线程使线程函数能继续使用，不被自动回收
	std::thread t(func_,threadId_);
	t.detach();//设置分离线程
};

int Thread::getId() const {
	return threadId_;
};
///////////////  Task方法实现
Task::Task()
	:result_(nullptr) { };

void Task::exec() {
	//执行任务
	if (result_ != nullptr) {
		result_->setVal(run());
	}
};

void Task::setResult(Result* res) {
	result_ = res;
};


///////////////   Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:isValid_(isValid), task_(task)
{
	task->setResult(this);
};

Any Result::get() {
	if (!isValid_) {
		return " ";
	}
	sem_.wait(); //task任务如果没有执行完，这里会阻塞用户的进程
	return std::move(any_);
};

void Result::setVal(Any any) {
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();//已经获取的任务的返回值，增加信号量资源

};