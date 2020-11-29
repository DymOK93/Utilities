#include "http_client.h"

#include <algorithm>
#include <functional>
#include <numeric>

using namespace std;

namespace web::http {
Session::Session(request request, boost::asio::io_context& io)
    : m_request{move(request)}, m_resolver(io), m_timer(io), m_tcp_stream(io) {}

Session::Status Session::GetSessionStatus() const noexcept {
  if (m_controller.InProgress()) {
    return Status::InProgress;
  }
  return m_error ? Status::Fail : Status::Success;
}

Session::error_code Session::GetError() const {
  Wait();
  return m_error;
}

const request& Session::GetRequest() const noexcept {
  return m_request;
}

void Session::Wait() const {
  if (!m_controller.Stopped()) {
    m_controller.Wait();
  }
}

const response& Session::GetResponse() const {
  Wait();
  throw_if_failed();
  return m_response;
}

optional<response> Session::ExtractResponse() {
  if (GetError()) {  //Блокирующая операция
    return nullopt;
  }
  return move(m_response);
}

optional<result_code_t> Session::GetResultCode() const {
  if (GetError()) {  //Блокирующая операция
    return nullopt;
  }
  return m_response.result_int();
}

optional<ResultCodeCategory> Session::GetResultCodeCategory() const {
  if (GetError()) {  //Блокирующая операция
    return nullopt;
  }
  auto front_digit{m_response.result_int() / 100};

  if (front_digit < static_cast<underlying_type_t<ResultCodeCategory>>(
                        ResultCodeCategory::Informational) ||
      front_digit > static_cast<underlying_type_t<ResultCodeCategory>>(
                        ResultCodeCategory::ClientError)) {
    throw out_of_range(
        "http result code is invalid");  //Во избежание UB при некорректном
                                         //поведении сервера
  }
  return static_cast<ResultCodeCategory>(front_digit);
}

void Session::RunAsync(session_holder session) {
  auto& request{session->m_request};
  boost::asio::ip::tcp::resolver::query query(
      static_cast<string>(
          request[boost::beast::http::field::host]),  // boost::string_view
                                                      // isn't null-terminated!
      static_cast<string>(request[boost::beast::http::field::protocol]));
  session->m_resolver.async_resolve(
      move(query), bind(&Session::on_resolve, move(session), placeholders::_1,
                        placeholders::_2));
}

void Session::throw_if_failed() const {
  if (m_error) {
    throw runtime_error("HTTP session failed");
  }
}

void Session::on_resolve(session_holder session,
                         boost::beast::error_code error,
                         boost::asio::ip::tcp::resolver::results_type results) {
  if (error) {
    end_session(move(session), error, 0);
  } else {
    auto& tcp_stream{session->m_tcp_stream};

    tcp_stream.expires_after(TIME_OF_CONNECTION_ATTEMPTS);

    session->m_timer.expires_from_now(
        TIME_OF_CONNECTION_ATTEMPTS);  //Для разблокировки слушателей в случае
                                       //невозможности соединения
    session->m_timer.async_wait(
        bind(&Session::end_session, session, placeholders::_1, 0));

    tcp_stream.async_connect(results, bind(&Session::on_connect, move(session),
                                           placeholders::_1, placeholders::_2));
  }
}

void Session::on_connect(
    session_holder session,
    boost::beast::error_code error,
    boost::asio::ip::tcp::resolver::results_type::endpoint_type endpoint) {
  if (error) {
    end_session(move(session), error, 0);
  } else {
    boost::beast::http::async_write(
        session->m_tcp_stream, session->m_request,
        bind(&Session::on_write, session, placeholders::_1,
             placeholders::_2)  //Перемещать session нельзя: порядок вычисления
                                //аргументов функции не определён!
    );
  }
}

void Session::on_write(session_holder session,
                       boost::beast::error_code error,
                       size_t bytes_transferred) {
  if (session->m_error = error; !error) {
    boost::beast::http::async_read(
        session->m_tcp_stream, session->m_dynamic_buffer, session->m_response,
        bind(&Session::end_session, session, placeholders::_1,
             placeholders::_2));
  }
}

void Session::end_session(session_holder session,
                          boost::beast::error_code error,
                          size_t bytes_transferred) {
  session->m_error = error;

  // session->m_timer.cancel();              //TODO: перечитать справку к
  // таймеру

  auto& controller{session->m_controller};
  controller.Stop();
  controller.NotifyAll();

  session->m_tcp_stream.socket().shutdown(
      boost::asio::ip::tcp::socket::shutdown_both, error);
}

Client::Client()
    : m_io_context{make_unique<boost::asio::io_context>()},
      m_workers(max(BASIC_THREAD_COUNT, thread::hardware_concurrency()) *
                THREAD_COUNT_MULTIPLIER) {
  initialize_io_runner();
}

session_holder Client::SendRequest(request req) {
  return start_async_session(move(req));
}

session_holder Client::start_async_session(request&& req) {
  auto session{make_shared<Session>(move(req), *m_io_context)};
  Session::RunAsync(session);
  return session;
}

void Client::initialize_io_runner() {
  m_io_context->post([&controller = m_controller]() {  //Удержание вызова run()
    controller.Wait();
  });
  m_workers.EnqueueMulti([io = m_io_context.get()]() { io->run(); },
                         BASIC_THREAD_COUNT * THREAD_COUNT_MULTIPLIER);
}
}  // namespace web::http
