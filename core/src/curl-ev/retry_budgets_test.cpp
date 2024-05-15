#include <curl-ev/error_code.hpp>
#include <curl-ev/retry_budgets.hpp>

#include <userver/utest/utest.hpp>
#include <userver/utils/impl/userver_experiments.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

utils::RetryBudgetSettings CreateRetryBudgetSettings(float max_tokens,
                                                     float token_ratio,
                                                     bool enabled) {
  return utils::RetryBudgetSettings{max_tokens, token_ratio, enabled};
}

}  // namespace

UTEST(CurlRetryBudgets, Disabled) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;
  constexpr size_t kRepetitions = 10000;
  const std::string destination = "test_host";

  curl::RetryBudgets retry_budgets;
  retry_budgets.SetRetryBudgetSettings(
      CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, false));

  for (size_t i = 0; i < kRepetitions; ++i) {
    retry_budgets.OnFailedRequest(destination);
    // Nothing changes in the budget
    ASSERT_TRUE(retry_budgets.CanRetry(destination));
  }

  for (size_t i = 0; i < kRepetitions; ++i) {
    retry_budgets.OnSuccessfulRequest(destination);
    // Nothing changes in the budget
    ASSERT_TRUE(retry_budgets.CanRetry(destination));
  }
}

UTEST(CurlRetryBudgets, SingleHost) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;
  const std::string destination = "test_host";

  curl::RetryBudgets retry_budgets;

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets.SetRetryBudgetSettings(settings);

  for (size_t i = 0; i < 4; ++i) {
    retry_budgets.OnFailedRequest(destination);
    ASSERT_TRUE(retry_budgets.CanRetry(destination));
  }

  for (size_t i = 0; i < 2; ++i) {
    retry_budgets.OnFailedRequest(destination);
  }
  // less than a half of the budget
  ASSERT_FALSE(retry_budgets.CanRetry(destination));

  for (size_t i = 0; i < 11; ++i) {
    ASSERT_FALSE(retry_budgets.CanRetry(destination));
    retry_budgets.OnSuccessfulRequest(destination);
  }

  // more than a half of a budget
  ASSERT_TRUE(retry_budgets.CanRetry(destination));
}

UTEST(CurlRetryBudgets, MultipleHosts) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;

  curl::RetryBudgets retry_budgets;

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets.SetRetryBudgetSettings(settings);

  // empty the budgets of 5 hosts
  for (size_t i = 0; i < 5; ++i) {
    const auto destination = std::to_string(i);

    for (size_t i = 0; i < 6; ++i) {
      retry_budgets.OnFailedRequest(destination);
    }

    ASSERT_FALSE(retry_budgets.CanRetry(destination));
  }

  // failed requests to the 6th host
  for (size_t i = 0; i < 3; ++i) {
    const auto destination = std::to_string(5);
    retry_budgets.OnFailedRequest(destination);
    ASSERT_TRUE(retry_budgets.CanRetry(destination));
  }

  // refill the budget of the 2nd host
  for (size_t i = 0; i < 30; ++i) {
    retry_budgets.OnSuccessfulRequest(std::to_string(2));
  }
  ASSERT_TRUE(retry_budgets.CanRetry(std::to_string(2)));

  for (size_t i = 0; i <= 5; ++i) {
    if (i == 2 || i == 5) {
      ASSERT_TRUE(retry_budgets.CanRetry(std::to_string(i)));
    } else {
      ASSERT_FALSE(retry_budgets.CanRetry(std::to_string(i)));
    }
  }
}

UTEST(CurlRetryBudgets, SettingsUpdate) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;
  const std::string destination = "test_host";

  curl::RetryBudgets retryBudgets;

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retryBudgets.SetRetryBudgetSettings(settings);

  // take more than a half of tokens from the budget
  for (size_t i = 0; i < kMaxTokens / 2 + 1; ++i) {
    retryBudgets.OnFailedRequest(destination);
  }
  ASSERT_FALSE(retryBudgets.CanRetry(destination));

  // update settings to turn off retry budget mechanism
  retryBudgets.SetRetryBudgetSettings(
      CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, false));

  // more than a half of a budget
  ASSERT_TRUE(retryBudgets.CanRetry(destination));
}

USERVER_NAMESPACE_END
