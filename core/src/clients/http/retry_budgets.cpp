#include "retry_budgets.hpp"
#include <algorithm>
#include <clients/http/retry_budgets.hpp>
#include <curl-ev/error_code.hpp>

#include <userver/utils/impl/userver_experiments.hpp>

USERVER_NAMESPACE_BEGIN

namespace clients::http {
namespace {
const size_t kRetryBudgetsMapSize = 256;
}

RetryBudgets::RetryBudgets(engine::TaskProcessor& main_task_processor)
    : main_task_processor_(&main_task_processor) {}

void RetryBudgets::SetRetryBudgetSettings(
    const utils::RetryBudgetSettings& settings) {
  settings_.Assign(settings);

  for (auto it = destinations_map_.begin(); it != destinations_map_.end();
       ++it) {
    it->second->budget.SetSettings(settings);
  }
}

void RetryBudgets::OnSuccessfulRequest(const std::string& destination) {
  if (!IsEnabled()) {
    return;
  }

  OnCompletedRequest(destination, [](RetryBudgets& retry_budgets,
                                     const std::string& destination) {
    // no meaning to add request budget for a destination, because the budget
    // will stay full
    if (auto ptr = retry_budgets.GetInfoByDestinationIfExists(destination)) {
      ptr->budget.AccountOk();
    }
  });
}

void RetryBudgets::OnFailedRequest(const std::string& destination) {
  if (!IsEnabled()) {
    return;
  }

  OnCompletedRequest(destination, [](RetryBudgets& retry_budgets,
                                     const std::string& destination) {
    auto destination_retry_budget =
        retry_budgets.GetInfoByDestination(destination);
    destination_retry_budget->budget.AccountFail();
    destination_retry_budget->failed_cnt.fetch_add(1,
                                                   std::memory_order_released);
  });
}

template <typename Func>
void RetryBudgets::OnCompletedRequest(const std::string& destination,
                                      Func&& func) {
  if (main_task_processor_) {
    engine::AsyncNoSpan(*main_task_processor_, [holder = shared_from_this(),
                                                func = std::forward<Func>(func),
                                                destination] {
      func(*holder, destination);
    }).Detach();
  } else {
    func(*this, destination);
  }
}

bool RetryBudgets::CanRetry(const std::string& destination) {
  if (!IsEnabled()) {
    return true;
  }

  // no meaning to add request budget for a destination, because the budget will
  // stay full
  if (auto ptr = GetInfoByDestinationIfExists(destination);
      ptr && !ptr->budget.CanRetry()) {
    return false;
  } else {
    return true;
  }
}

std::shared_ptr<RetryBudgets::DestinationInfo>
RetryBudgets::GetInfoByDestination(const std::string& destination) {
  auto ptr = destinations_map_.Get(destination);
  if (ptr) return ptr;

  auto settingsPtr = settings_.Read();

  auto map_copy_ptr = destinations_map_.StartWrite();
  ReduceMapIfNecessary(*map_copy_ptr);
  auto element_ptr =
      map_copy_ptr
          ->emplace(destination,
                    std::make_shared<DestinationInfo>(*settingsPtr))
          .first->second;
  map_copy_ptr.Commit();

  return element_ptr;
}

std::shared_ptr<RetryBudgets::DestinationInfo>
RetryBudgets::GetInfoByDestinationIfExists(const std::string& destination) {
  return destinations_map_.Get(destination);
}

bool RetryBudgets::IsEnabled() const {
  if (!utils::impl::kHttpClientRetryBudgetExperiment.IsEnabled()) {
    return false;
  }

  auto settingsPtr = settings_.Read();
  return settingsPtr->enabled;
}

void RetryBudgets::ReduceMapIfNecessary(DestinationsMap::RawMap& map) {
  using OrderElement = std::pair<uint64_t, DestinationsMap::RawMap::iterator>;

  if (map.size() <= kRetryBudgetsMapSize) {
    return;
  }

  std::vector<OrderElement> order;
  for (auto it = map.begin(); it != map.end(); ++it) {
    order.emplace_back(it->second->failed_cnt.load(std::memory_order_acquire),
                       it);
  }

  std::sort(order.begin(), order.end(),
            [](const OrderElement& a, const OrderElement& b) {
              return a.first < b.first;
            });

  // erase elements with low failed_cnt
  for (size_t i = 0; i < order.size() / 2; ++i) {
    map.erase(order[i].second);
  }

  // reset to zero all failed_cnt
  for (auto& [key, info] : map) {
    info->failed_cnt.store(0, std::memory_order_released);
  }

  ++map_reduction_cnt_;
}

}  // namespace clients::http

USERVER_NAMESPACE_END
