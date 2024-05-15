#include <curl-ev/error_code.hpp>
#include <curl-ev/retry_budgets.hpp>

#include <userver/utils/impl/userver_experiments.hpp>

USERVER_NAMESPACE_BEGIN

namespace curl {
namespace {
const size_t kRetryBudgetsLruSize = 128;
}

RetryBudgets::RetryBudgets() : destinations_map_(kRetryBudgetsLruSize) {}

void RetryBudgets::SetRetryBudgetSettings(
    const utils::RetryBudgetSettings& settings) {
  settings_.Assign(settings);
}

void RetryBudgets::OnSuccessfulRequest(const std::string& destination) {
  if (!IsEnabled()) {
    return;
  }

  GetRetryBudgetByDestination(destination).AccountOk();
}

void RetryBudgets::OnFailedRequest(const std::string& destination) {
  if (!IsEnabled()) {
    return;
  }

  GetRetryBudgetByDestination(destination).AccountFail();
}

bool RetryBudgets::CanRetry(const std::string& destination) {
  if (!IsEnabled()) {
    return true;
  }

  return GetRetryBudgetByDestination(destination).CanRetry();
}

utils::RetryBudget& RetryBudgets::GetRetryBudgetByDestination(
    const std::string& destination) {
  auto settingsPtr = settings_.Read();
  return *destinations_map_.Emplace(destination, *settingsPtr);
}

bool RetryBudgets::IsEnabled() const {
  if (!utils::impl::kHttpClientRetryBudgetExperiment.IsEnabled()) {
    return false;
  }

  auto settingsPtr = settings_.Read();
  return settingsPtr->enabled;
}

}  // namespace curl

USERVER_NAMESPACE_END
