#pragma once
#include "thread_pool.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/assert.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace web::http {
using request = boost::beast::http::request<boost::beast::http::string_body>;
using response = boost::beast::http::response<boost::beast::http::string_body>;
using result_code_t = unsigned int;
using field = boost::beast::http::field;
using method = boost::beast::http::verb;

class Session;
using session_holder = std::shared_ptr<Session>;

enum class ResultCodeCategory {
    Informational = 1,
    Success,
    Reedirection,
    ClientError,
    ServerError
};

class Session {
 public:
  static constexpr std::chrono::seconds TIME_OF_CONNECTION_ATTEMPTS{10};

 public:
  using error_code = boost::beast::error_code;
  enum class Status { Success, Fail, InProgress };

 public:
  Session(request request, boost::asio::io_context& io);

  Status GetSessionStatus() const noexcept;  //Неблокирующий вызов
  const request& GetRequest() const noexcept;

  error_code GetError() const;               //Блокирующий вызов

  void Wait() const;  //Операции, вызывающие Wait(), не обрабатывают внутри себя
                      //выброшенные ей исключения
  const response& GetResponse() const;
  std::optional<response> ExtractResponse();
  std::optional<result_code_t> GetResultCode() const;
  std::optional<ResultCodeCategory> GetResultCodeCategory() const;

  static void RunAsync(session_holder session);

 private:
  void throw_if_failed() const;

  static void on_resolve(session_holder session,
                         boost::beast::error_code error,
                         boost::asio::ip::tcp::resolver::results_type results);
  static void on_connect(
      session_holder session,
      boost::beast::error_code error,
      boost::asio::ip::tcp::resolver::results_type::endpoint_type endpoint);
  static void on_write(session_holder session,
                       boost::beast::error_code error,
                       size_t bytes_transferred);
  static void end_session(session_holder session,
                          boost::beast::error_code,
                          size_t bytes_transferred);

 private:
  request m_request;
  response m_response;
  boost::asio::ip::tcp::resolver m_resolver;
  boost::asio::basic_waitable_timer<std::chrono::steady_clock> m_timer;
  boost::beast::tcp_stream m_tcp_stream;
  boost::beast::flat_buffer m_dynamic_buffer;
  error_code m_error;

  mutable utility::concurrency::ThreadController
      m_controller;  //Управление ожиданием
};

class Client {
 private:
  static constexpr unsigned int BASIC_THREAD_COUNT{3},
      THREAD_COUNT_MULTIPLIER{1};

 public:
  Client();
  session_holder SendRequest(request req);

 private:
  session_holder start_async_session(request&& req);
  void initialize_io_runner();

 private:
  std::unique_ptr<boost::asio::io_context> m_io_context;
  utility::concurrency::ThreadPool m_workers;
  utility::concurrency::ThreadController m_controller;
};
}  // namespace web::http
