#pragma once
#include <boost/lockfree/queue.hpp>

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace utility::concurrency {
namespace details {
template <class Tuple>
auto make_index_sequence_for_tuple() {
  return std::make_index_sequence<std::tuple_size_v<Tuple>>{};
}

template <class Function, class ArgTuple, size_t... Indices>
struct invoke_result_impl {
  using type = std::invoke_result_t<Function,
                                    decltype(std::move(std::get<Indices>(
                                        std::declval<ArgTuple>())))...>;
};

template <class Function, class ArgTuple, size_t... Indices>
using invoke_result_impl_t =
    typename invoke_result_impl<Function, ArgTuple, Indices...>::type;

template <class Function, class ArgTuple, size_t... Indices>
invoke_result_impl_t<Function, ArgTuple, Indices...> get_invoke_result(
    std::index_sequence<Indices...> idcs);

template <class Function, class ArgTuple>
struct invoke_result {
  using type = decltype(get_invoke_result<Function, ArgTuple>(
      make_index_sequence_for_tuple<ArgTuple>()));
};

template <class Function, class ArgTuple>
using invoke_result_t = typename invoke_result<Function, ArgTuple>::type;

template <class... Types>
struct argument_tuple {
  using type = typename std::tuple<std::decay_t<Types>...>;
};

template <class... Types>
using argument_tuple_t = typename argument_tuple<Types...>::type;

template <template <class, class> class Task, class Function, class... Types>
struct async_task {
  using type = Task<std::decay_t<Function>, argument_tuple_t<Types...>>;
};

template <template <class, class> class Task, class Function, class... Types>
using async_task_t = typename async_task<Task, Function, Types...>::type;
}  // namespace details

namespace async {
class ITask {
 public:
  using result_t = bool;

 public:
  virtual ~ITask() = default;
  virtual result_t Process() noexcept = 0;

 protected:
  static result_t operation_successful() noexcept { return true; }
  template <class Exception>
  static result_t exception_thrown(const Exception& exc) noexcept {
    return false;
  }
  static result_t unknown_exception_thrown() noexcept { return false; }
};

using task_holder = std::unique_ptr<ITask>;

template <class Function, class ArgTuple>
class DetachedTask : public ITask {
 public:
  using MyBase = ITask;
  using result_t = MyBase::result_t;

 public:
  template <class Func, class Tuple>
  DetachedTask(Func&& func, Tuple&& args)
      : m_func(std::forward<Func>(func)), m_args(std::forward<Tuple>(args)) {}

  result_t Process() noexcept override {
    try {
      call_with_unpacked_args(m_func, m_args);
    } catch (const std::exception& exc) {
      return exception_thrown(exc);
    } catch (...) {
      return unknown_exception_thrown();
    }
    return operation_successful();
  }

 protected:
  static decltype(auto) call_with_unpacked_args(Function& func,
                                                ArgTuple& args) {
    return call_with_unpacked_args_impl(
        func, args, details::make_index_sequence_for_tuple<ArgTuple>());
  }

  template <size_t... Indices>
  static decltype(auto) call_with_unpacked_args_impl(
      Function& func,
      ArgTuple& args,
      std::index_sequence<Indices...>) {
    return std::invoke(func, std::move(std::get<Indices>(args))...);
  }

 protected:
  Function m_func;
  ArgTuple m_args;
};

template <class Function, class ArgTuple>
class PackagedTask : public DetachedTask<Function, ArgTuple> {
 private:
  using MyBase = DetachedTask<Function, ArgTuple>;
  using result_t = typename MyBase::result_t;
  using ret_t = details::invoke_result_t<Function, ArgTuple>;
  using future_t = std::future<ret_t>;

 public:
  template <class Func, class Tuple>
  PackagedTask(Func&& func, Tuple&& args)
      : MyBase(std::forward<Func>(func), std::forward<Tuple>(args)) {}

  future_t GetFuture() { return m_promise.get_future(); }

  result_t Process() noexcept override {
    try {
      m_promise.set_value(MyBase::call_with_unpacked_args(m_func, m_args))
    } catch (const std::exception& exc) {
      return exception_thrown(exc);
    } catch (...) {
      try {
        m_promise.set_exception(
            std::current_exception());  //Может бросить std::future_error
      } catch (const std::future_error& exc) {
        return exception_thrown(exc);
      }
      return unknown_exception_thrown();
    }
    return operation_successful();
  }

 protected:
  std::promise<ret_t> m_promise;
};

template <template <class, class> class Task, class Function, class... Types>
auto MakeTask(Function&& func, Types&&... args) {
  using task_t = details::async_task_t<Task, Function, Types...>;
  static_assert(std::is_base_of_v<ITask, task_t>,
                "Task must be derived from ITask");
  return task_t(
      std::forward<Function>(func),
      details::argument_tuple_t<Types...>{std::forward<Types>(args)...});
}

template <template <class, class> class Task, class Function, class... Types>
task_holder MakeTaskHolder(Function&& func, Types&&... args) {
  using task_t = details::async_task_t<Task, Function, Types...>;
  static_assert(std::is_base_of_v<ITask, task_t>,
                "Task must be derived from ITask");
  return std::make_unique<task_t>(
      std::forward<Function>(func),
      details::argument_tuple_t<Types...>{std::forward<Types>(args)...});
}
}  // namespace async

class ThreadController {
 public:
  ThreadController() = default;
  ThreadController(bool stopped_at_creation) : m_stop{stopped_at_creation} {}

  ~ThreadController() {
    if (!Stopped()) {
      Stop();
      NotifyAll();
    }
  }

  void Stop() noexcept { m_stop = true; }

  void Continue() noexcept { m_stop = false; }

  void Wait() {
    std::unique_lock thread_waid_lock(m_mtx);
    m_cv.wait(thread_waid_lock);
  }

  template <class Predicate>
  void Wait(Predicate pred) {
    std::unique_lock thread_waid_lock(m_mtx);
    m_cv.wait(thread_waid_lock, pred);
  }

  void NotifyOne() { m_cv.notify_one(); }

  void NotifyAll() { m_cv.notify_all(); }

  bool InProgress() const noexcept { return !Stopped(); }

  bool Stopped() const noexcept { return m_stop; }

 private:
  std::atomic<bool> m_stop{false};
  std::mutex m_mtx;
  std::condition_variable m_cv;
};

class ThreadPool {
 private:
  using lockfree_queue = boost::lockfree::queue<async::ITask*>;

 public:
  ThreadPool(size_t worker_count) : m_tasks(worker_count) {
    m_workers.reserve(worker_count);
    for (size_t idx = 0; idx < worker_count; ++idx) {
      m_workers.emplace_back(&ThreadPool::execute, this);
    }
  }
  ~ThreadPool() {
    m_controller.Stop();
    m_controller.NotifyAll();
    for (auto& worker : m_workers) {
      worker.join();
    }
  }

  template <class Function, class... Types>
  auto Schedule(Function&& func, Types&&... args) {
    static_assert(std::is_invocable_v<Function, Types...>,
                  "Impossible to invoke a callable with passed arguments");
    auto task_guard{
        async::MakeTaskHolder<async::PackagedTask, Function, Types...> >
        (std::forward<Function>(func), std::forward<Types>(args)...)};
    /**********************************************************************************************************
    Объект future необходимо получить до отправки задачи в очередь - в противном
    случае есть вероятность возникновения гонки данных: <Поток 1>: <Задача
    создана> -> <Задача загружена в очередь> -> [timestamp] -> <future получен>
    -> <Возврат из функции> <Поток 2>: <Ожидание> -> <Задача выполняется> ->
    <Задача выполнена> -> <Объект Task удалён> -> [timestamp] При выбросе
    исключения не произойдет блокировки потока с future, ссылающимся на
    shared_state: см. https://en.cppreference.com/w/cpp/thread/future/%7Efuture:
    ...these actions will not block for the shared state to become ready,
    except that it may block if all of the following are true:
    > the shared state was created by a call to std::async
    > the shared state is not yet ready
    > [future object] was the last reference to the shared state
    **********************************************************************************************************/
    auto future{task_guard->GetFuture()};
    if (!m_tasks.push(task_guard.get())) {
      throw runtime_error("Can't push task into queue");
    }
    m_controller.NotifyOne();
    task_guard.release();
    return future;
  }

  template <class Function, class... Types>
  void Enqueue(Function&& func, Types&&... args) {
    static_assert(std::is_invocable_v<Function, Types...>,
                  "Impossible to invoke a callable with passed arguments");
    auto task_guard{
        async::MakeTaskHolder<async::DetachedTask, Function, Types...>(
            std::forward<Function>(func), std::forward<Types>(args)...)};
    if (!m_tasks.push(task_guard.get())) {
      throw runtime_error("Can't push task into queue");
    }
    m_controller.NotifyOne();
    task_guard.release();
  }

  template <class Function, class... Types>
  void EnqueueMulti(const Function& func,
                    size_t task_count,
                    const Types&... args) {
    for (size_t task_idx = 0; task_idx < task_count; ++task_idx) {
      Enqueue(func, args...);
    }
  }

  size_t WorkerCount() const noexcept { return m_workers.size(); }

 private:
  void execute() {
    for (;;) {
      async::ITask* task{nullptr};
      if (m_tasks.pop(task)) {
        std::unique_ptr<async::ITask> task_guard(task);
        task_guard->Process();
      } else if (m_controller.Stopped()) {
        break;
      } else {
        m_controller.Wait();
      }
    }
  }

 private:
  std::vector<std::thread> m_workers;
  lockfree_queue m_tasks;
  ThreadController m_controller;
};
}  // namespace utility::concurrency
