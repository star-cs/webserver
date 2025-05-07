#ifndef __SYLAR_SINGLETON_H__
#define __SYLAR_SINGLETON_H__

#include <memory>

namespace sylar{

template<class T, class X = void, int N = 0>
class Singleton{
    public:
        static T* GetInstance(){
            static T v;
            return &v;
        }
};

template<class T, class X = void, int N = 0>
class ThreadLocalSingleton{
    public:
        static T* GetInstance(){
            static thread_local T v;
            return &v;
        }
};

template<class T, class X = void, int N = 0>
class SingletonPtr{
    public: 
        static std::shared_ptr<T> GetInstance(){
            static std::shared_ptr<T> v(std::make_shared<T>);
            return v;
        }
};

template<class T, class X = void, int N = 0>
class SingletonHungry {
public:
    static T* GetInstance() {
        return instance;
    }
    
private:
    SingletonHungry() = default;
    static T* instance;
};

// // 类外初始化（在头文件中需要加上模板定义）
template<class T, class X, int N>
T* SingletonHungry<T, X, N>::instance = new T();



}

#endif