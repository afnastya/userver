#pragma once

#include <memory>
#include <mutex>

#include <userver/rcu/rcu_map.hpp>
#include <userver/utils/retry_budget.hpp>

USERVER_NAMESPACE_BEGIN

namespace clients::http {

class HttpRetryBudgetsChecker;

class RetryBudgets final : public std::enable_shared_from_this<RetryBudgets> {
 public:
  RetryBudgets() = default;
  RetryBudgets(engine::TaskProcessor& main_task_processor);
  void SetRetryBudgetSettings(const utils::RetryBudgetSettings& settings);

  bool CanRetry(const std::string& destination);
  void OnSuccessfulRequest(const std::string& destination);
  void OnFailedRequest(const std::string& destination);

  friend class HttpRetryBudgetsChecker;

 private:
  struct DestinationInfo {
    explicit DestinationInfo(const utils::RetryBudgetSettings& settings)
        : budget(settings) {}

    utils::RetryBudget budget;
    std::atomic<uint64_t> failed_cnt{0};
  };
  using DestinationsMap = rcu::RcuMap<std::string, DestinationInfo>;

  bool IsEnabled() const;

  std::shared_ptr<DestinationInfo> GetInfoByDestination(
      const std::string& destination);

  std::shared_ptr<DestinationInfo> GetInfoByDestinationIfExists(
      const std::string& destination);

  template <typename Func>
  void OnCompletedRequest(const std::string& destination, Func&& func);

  void ReduceMapIfNecessary(DestinationsMap::RawMap& map);

  DestinationsMap destinations_map_;
  rcu::Variable<utils::RetryBudgetSettings> settings_;

  engine::TaskProcessor* main_task_processor_{nullptr};

  utils::statistics::RateCounter map_reduction_cnt_;
};

}  // namespace clients::http

USERVER_NAMESPACE_END
