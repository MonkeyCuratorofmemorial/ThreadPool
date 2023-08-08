#include "threadpool.h"
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>


const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;

//�̳߳ع���
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
	//�ȴ��̳߳��������е��̷߳���  ������״̬ ��������ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool{return threads_.size() == 0; });
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState())
		return;
	poolMode_ = mode;
};

//����task������е���ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
};

//�����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}
};

//���̳߳��ύ����(����Ҫ������)
//�û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�̵߳�ͨ�� �ȴ���������п���
	//�û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
	/*while (taskQue_.size() == taskQueMaxThreshHold_) {
		notFull_.wait(lock);
	}*/
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; })) {
		//��ʾnotFull_�ȴ�1s�ӣ�������Ȼû������
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp, false);//Task
	}
	
	//���п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;

	//��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ
	notEmpty_.notify_all();

	//��Ϊ�·�������������п϶���Ϊ�գ���notEmpty_(��������)�Ͻ���֪ͨ���Ͽ�����߳�ִ������
	//cachedģʽ��������ȽϽ��� ����:С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��߳�
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_) {
		//�����µ��̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int ThreadId = ptr->getId();
		threads_.emplace(ThreadId, std::move(ptr));
		threads_[ThreadId]->start();//�����߳�
		//�޸��̸߳�����ر���
		curThreadSize_++;
		idleThreadSize_++;
	}

	//���������Result����
	return Result(sp);
};

//�����̳߳�
void ThreadPool::start(int initThreadSize) {
	//�����̳߳ص�start
	isPoolRunning_ = true;
	
	//��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++) {
		//����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		/*threads_.emplace_back(std::move(ptr));*/
	}

	//���������߳�
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++; //��¼��ʼ�����̵߳�����
	}
};

//�����̺߳��� �̳߳ص������̴߳��̶߳���������������
void ThreadPool::threadFunc(int threadid) {//�̺߳������أ���Ӧ���߳�Ҳ�ͽ�����
	/*std::cout << "begin threadFunc" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc" << std::this_thread::get_id() << std::endl;*/
	auto lastTime = std::chrono::high_resolution_clock().now();

	for (;;) {
		std::shared_ptr<Task> task;
		{	
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() <<"���Ի�ȡ���񡣡���"<< std::endl;

			//cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����̻߳��յ�
			//�������յ�(����initThreadSize_�������߳̽��л���)
			//��ǰʱ�� - ��һ���߳�ִ��ʱ�� > 60s
			
			//ÿһ���ӷ���һ�ε��������û������ ��ô����ʱ���أ� ��������������أ�
			//������������˫���жϣ�isPoolRunning_��־�̳߳��Ƿ���������
			while (taskQue_.size() == 0) {
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << "exit!!" << std::endl;
					exitCond_.notify_all();
					return;
				}
				if (poolMode_ == PoolMode::MODE_CACHED) {
					//����������ʱ����
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
							//��ʼ���յ�ǰ�߳�
							//��¼�߳���������ؼ�¼��ֵ�޸�
							//���̶߳�����߳��б�������ɾ��
							//threadid =>thread���� =��ɾ��
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid:" << std::this_thread::get_id() << "exit!!��" << std::endl;
							return;
						}
					}
				}
				else {
					//�����������δ��ʱ�򣬵ȴ��������notEmpty�������㣬�ȴ��ύ����
					notEmpty_.wait(lock);
				}
			}

			//�̳߳�Ҫ������������ ����Դ
			if (!isPoolRunning_) {
				/*threads_.erase(threadid);
				std::cout << "threadid:" << std::this_thread::get_id() << "exit!!��" << std::endl;
				exitCond_.notify_all();
				return;*/
				break;
			}

			//��ʼ�����߳�
			idleThreadSize_--;
			std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�������" << std::endl;
			
			//�����������ȡһ���������
			//����BUG1:�ھֲ��������в�������autoȥ�����ⲿ�������ڲ��൱�����¶��壬��������������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			//����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
			if (taskQue_.size()>0) {
				notEmpty_.notify_all();
			}
			//ִ����һ�����񣬵ý���֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}//��Ӧ�ð����ͷŵ�

		//��ǰ�̸߳���ִ���������
		if (task != nullptr) {
			/*task->run();*/
			task->exec();
		}	
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//�����߳�ִ���������ʱ��
	}
};

bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

/////////�̷߳���ʵ��
int Thread::generateId_ = 0;

//�̹߳���
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++) {};
//�߳�����
Thread::~Thread() {

};
//�����߳�
void Thread::start() {
	//����һ���߳���ִ��һ���̺߳���

	//�̶߳����ھֲ��������У�����������ʱ�������Զ�����
	//����ʹ�÷����߳�ʹ�̺߳����ܼ���ʹ�ã������Զ�����
	std::thread t(func_,threadId_);
	t.detach();//���÷����߳�
};

int Thread::getId() const {
	return threadId_;
};
///////////////  Task����ʵ��
Task::Task()
	:result_(nullptr) { };

void Task::exec() {
	//ִ������
	if (result_ != nullptr) {
		result_->setVal(run());
	}
};

void Task::setResult(Result* res) {
	result_ = res;
};


///////////////   Result������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:isValid_(isValid), task_(task)
{
	task->setResult(this);
};

Any Result::get() {
	if (!isValid_) {
		return " ";
	}
	sem_.wait(); //task�������û��ִ���꣬����������û��Ľ���
	return std::move(any_);
};

void Result::setVal(Any any) {
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();//�Ѿ���ȡ������ķ���ֵ�������ź�����Դ

};