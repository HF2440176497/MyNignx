// 自定义互斥量类
// 对于 C++11 新标准，提供有多种模板可替代我们自定义的 CLock 类

#ifndef C_LOCK_H
#define C_LOCK_H

#include <pthread.h>

// CLock 对已有的互斥量加锁
class CLock {

public:
	CLock(pthread_mutex_t *pMutex) {
		m_pMutex = pMutex;
		pthread_mutex_lock(m_pMutex); //加锁互斥量
	}
	~CLock() {
		pthread_mutex_unlock(m_pMutex); //解锁互斥量
	}
private:
	pthread_mutex_t *m_pMutex;

};

#endif