// 自定义互斥量类
// 对于 C++11 新标准，提供有多种模板可替代我们自定义的 CLock 类

// 05.25 改造 
// 以“对象管理资源”的角度看，CLock 是一个 RAII 对象类
// RAII 对象构造，复制时，对于所管理的资源采用的策略是设置引用计数 
// 

#ifndef C_LOCK_H
#define C_LOCK_H

#include <pthread.h>
#include <memory>

class CLock {
public:
	explicit CLock(pthread_mutex_t* p_mutex): mutex_Ptr(p_mutex, 
											[](pthread_mutex_t* p){ pthread_mutex_unlock(p); }) {
		pthread_mutex_lock(mutex_Ptr.get());
	}
	CLock(const CLock& temp_lock) {
		mutex_Ptr = nullptr;
		mutex_Ptr = temp_lock.mutex_Ptr;
	}
private:
	std::shared_ptr<pthread_mutex_t> mutex_Ptr;  // mutex_Ptr 的析构函数，会在引用计数为 0 时，自动调用删除器，因此自己可以不用实现析构函数
};

// 删除器定义了引用计数为 0 时的行为
// shared_ptr 定义有拷贝赋值运算符，引用计数加 1 

#endif