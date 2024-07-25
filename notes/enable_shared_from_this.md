# std::enable_shared_from_this 用法分析

## std::shared_ptr 基础知识

`std::shared_ptr` 是一种智能指针，通过共享控制块的方式安全地管理对象的生命周期。

使用原始指针构造或初始化 `shared_ptr` 时，会创建一个新的控制块，因此对象的任何额外的 `shared_ptr` 实例必须通过复制已存在的指向该对象的 `shared_ptr` 来产生，并尽量通过 `std::make_shared` 或者 `std::allocate_shared` 创建：

``` cpp
void run() {
    auto p{new int(12)};
    std::shared_ptr<int> sp1{p};
    auto sp2{sp1}; // √

    std::share_ptr<int> sp3{p}; // ×，UB
}
```

有些情况下对象需要获得一个指向自己的 `shared_ptr`

``` cpp
struct Foo {
    std::shared_ptr<Foo> getThis() {
        return std::shared_ptr<Foo>(this);
    }
};
void run() {
    auto sp1 = std::make_shared<Foo>();
    auto sp2 = sp1->getThis(); // ×，UB，有不同的控制块
}
```

这就是 `std::enable_shared_from_this<T>` 发挥作用的地方。公开继承该类可以通过调用方法 `shared_from_this()` 获得指向自己的 `shared_ptr`

## 异步编程下需要保证对象的生命周期

在[协程库代码](https://github.com/bz-2021/cpp-coroutine-lib)中 Coroutine 类继承自 `std::enable_shared_from_this` 是为了确保对象的生命周期能够正确管理，其提供了 `shared_from_this()` 的成员函数，可以返回一个指向当前对象的 `std::shared_ptr`。这在对象需要在其成员函数中创建 `shared_ptr` 来延长自身的生命周期时非常有用。

``` cpp
void Coroutine::MainFunc() {
    // GetThis() 中调用了 shared_from_this() 使引用计数加一
    std::shared_ptr<Coroutine> curr = GetThis();
    assert(curr != nullptr);

	curr->m_cb(); // 协程的入口函数
	curr->m_cb = nullptr;
	curr->m_state = TERM;

	// 运行完毕 -> 让出执行权
	auto raw_ptr = curr.get();
	curr.reset(); // reset 使引用计数减一
	raw_ptr->yield(); 
}
```

在以上代码中，当 `GetThis()` 返回当前协程的 `shared_ptr` 时，引用计数增加了一，确保了当前协程对象在 `curr` 的生命周期内不会被销毁。

在协程执行完毕后，通过 `curr.reset()` 减少了引用计数。如果 `curr` 是唯一一个引用该对象的 `shared_ptr，那么在` `curr.reset()` 后对象将被销毁。但是在这之前，我们用 `raw_ptr` 保存了对象的原始指针。

因为 `raw_ptr` 是普通指针，它不会影响对象的引用计数。因此，在调用 `raw_ptr->yield()` 时，协程对象依然存在，不会被立即销毁。这确保了 `yield()` 操作可以安全地完成。

## enable_shared_from_this 的使用场景

- `shared_ptr` 根本认不得你传进来的指针变量是不是之前已经传过。

凡是需要共享使用类对象的地方，必须使用这个 `shared_ptr` 当作右值来构造产生或者拷贝产生（ `shared_ptr` 类中定义了赋值运算符函数和拷贝构造函数）另一个 `shared_ptr` ，从而达到共享使用的目的。

``` cpp
int main()
{
    Test* test = new Test();
    shared_ptr<Test> p(test);
    shared_ptr<Test> q(test);
    std::cout << "p.use_count(): " << p.use_count() << std::endl;
    std::cout << "q.use_count(): " << q.use_count() << std::endl;
    return 0;
}
/*
输出：
    p.use_count(): 1
    q.use_count(): 1
    Test Destructor.
    Test Destructor.
*/
```

那么如何在类对象内部中获得一个指向当前对象的 `shared_ptr` 对象？

可以通过以下代码实现：

``` cpp
class Test : public std::enable_shared_from_this<Test>
{
public:
    ~Test() { std::cout << "Test Destructor." << std::endl; }

    std::shared_ptr<Test> GetObject()
    {
        return shared_from_this();
    }
};
```