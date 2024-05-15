#pragma once

#include <memory>
#include <mutex>

#include <userver/cache/lru_map.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/utils/retry_budget.hpp>

USERVER_NAMESPACE_BEGIN

namespace curl {

class RetryBudgets final {
 public:
  RetryBudgets();
  void SetRetryBudgetSettings(const utils::RetryBudgetSettings& settings);

  void OnSuccessfulRequest(const std::string& destination);
  void OnFailedRequest(const std::string& destination);
  bool CanRetry(const std::string& destination);

 private:
  bool IsEnabled() const;

  utils::RetryBudget& GetRetryBudgetByDestination(
      const std::string& destination);

  cache::LruMap<std::string, utils::RetryBudget> destinations_map_;
  rcu::Variable<utils::RetryBudgetSettings> settings_;
};

}  // namespace curl

USERVER_NAMESPACE_END
