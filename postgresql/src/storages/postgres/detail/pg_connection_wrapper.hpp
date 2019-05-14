#pragma once

#include <libpq-fe.h>
#include <chrono>

#include <engine/async.hpp>
#include <engine/io/socket.hpp>
#include <engine/task/task.hpp>
#include <logging/log_extra.hpp>
#include <storages/postgres/detail/connection.hpp>
#include <storages/postgres/detail/result_wrapper.hpp>
#include <tracing/span.hpp>
#include <utils/size_guard.hpp>

namespace storages {
namespace postgres {
namespace detail {

class PGConnectionWrapper {
 public:
  using Deadline = engine::Deadline;
  using Duration = Deadline::TimePoint::clock::duration;
  using ResultHandle = detail::ResultWrapper::ResultHandle;
  using SizeGuard = ::utils::SizeGuard<std::shared_ptr<std::atomic<size_t>>>;

 public:
  PGConnectionWrapper(engine::TaskProcessor& tp, uint32_t id,
                      SizeGuard&& size_guard);
  ~PGConnectionWrapper();

  PGConnectionWrapper(const PGConnectionWrapper&) = delete;
  PGConnectionWrapper& operator=(const PGConnectionWrapper&) = delete;

  ConnectionState GetConnectionState() const;

  /// @brief Asynchronously connect PG instance.
  /// Start asynchronous connection and wait for it's completion (suspending
  /// current couroutine)
  /// @param conninfo Connection string
  /// @param
  void AsyncConnect(const std::string& conninfo, Deadline deadline, ScopeTime&);

  /// @brief Close the connection on a background task processor.
  [[nodiscard]] engine::Task Close();

  /// @brief Cancel current operation on a background task processor.
  [[nodiscard]] engine::Task Cancel();

  /// @brief Wrapper for PQsendQuery
  void SendQuery(const std::string& statement, ScopeTime&);

  /// @brief Wrapper for PQsendQueryParams
  void SendQuery(const std::string& statement, const QueryParameters& params,
                 ScopeTime&,
                 io::DataFormat reply_format = io::DataFormat::kTextDataFormat);

  /// @brief Wrapper for PQsendPrepare
  void SendPrepare(const std::string& name, const std::string& statement,
                   const QueryParameters& params, ScopeTime&);

  /// @brief Wrapper for PQsendDescribePrepared
  void SendDescribePrepared(const std::string& name, ScopeTime&);

  /// @brief Wrapper for PQsendQueryPrepared
  void SendPreparedQuery(
      const std::string& name, const QueryParameters& params, ScopeTime&,
      io::DataFormat reply_format = io::DataFormat::kTextDataFormat);

  /// @brief Wait for query result
  /// Will return result or throw an exception
  ResultSet WaitResult(const UserTypes&, Deadline deadline, ScopeTime&);

  /// Consume all input discarding all result sets
  void DiscardInput(Deadline deadline);

  /// @brief Get extra log information
  /// Used for internal needs
  const logging::LogExtra& GetLogExtra() const;

  void LogNotice(PGresult const*);

  TimeoutDuration GetIdleDuration() const;

 private:
  PGTransactionStatusType GetTransactionStatus() const;

  void StartAsyncConnect(const std::string& conninfo);

  /// @param deadline
  /// @throws ConnectionTimeoutError if was awakened by the deadline
  void WaitConnectionFinish(Deadline deadline);

  /// @param deadline
  /// @return true if wait was successful, false if was awakened by the deadline
  [[nodiscard]] bool WaitSocketWriteable(Deadline deadline);

  /// @param deadline
  /// @return true if wait was successful, false if was awakened by the deadline
  [[nodiscard]] bool WaitSocketReadable(Deadline deadline);

  void Flush(Deadline deadline);
  void ConsumeInput(Deadline deadline);

  ResultSet MakeResult(const UserTypes&, ResultHandle&& handle);

  template <typename ExceptionType>
  void CheckError(const std::string& cmd, int pg_dispatch_result);

  template <typename ExceptionType>
  [[noreturn]] void CloseWithError(ExceptionType&& ex);

  void UpdateLastUse();

 private:
  engine::TaskProcessor& bg_task_processor_;

  PGconn* conn_ = nullptr;
  engine::io::Socket socket_;
  logging::LogExtra log_extra_;
  SizeGuard size_guard_;
  std::chrono::steady_clock::time_point last_use_;
};

}  // namespace detail
}  // namespace postgres
}  // namespace storages
