#include <clients/http/retry_budgets.hpp>
#include <curl-ev/error_code.hpp>

#include <string>

#include <userver/engine/wait_all_checked.hpp>
#include <userver/utest/utest.hpp>
#include <userver/utils/from_string.hpp>
#include <userver/utils/impl/userver_experiments.hpp>
#include <userver/utils/rand.hpp>

USERVER_NAMESPACE_BEGIN

namespace clients::http {

class HttpRetryBudgetsChecker {
 public:
  static size_t GetDestinationsMapSize(
      clients::http::RetryBudgets& retry_budgets) {
    return retry_budgets.destinations_map_.SizeApprox();
  }

  static auto& GetDestinationsMap(clients::http::RetryBudgets& retry_budgets) {
    return retry_budgets.destinations_map_;
  }

  static auto GetMapReductionCnt(clients::http::RetryBudgets& retry_budgets) {
    return retry_budgets.map_reduction_cnt_.Load();
  }
};

namespace {

utils::RetryBudgetSettings CreateRetryBudgetSettings(float max_tokens,
                                                     float token_ratio,
                                                     bool enabled) {
  return utils::RetryBudgetSettings{max_tokens, token_ratio, enabled};
}

}  // namespace

UTEST(HttpRetryBudgets, Disabled) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;
  constexpr size_t kRepetitions = 10000;
  const std::string destination = "test_host";

  auto retry_budgets = std::make_shared<clients::http::RetryBudgets>();
  retry_budgets->SetRetryBudgetSettings(
      CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, false));

  for (size_t i = 0; i < kRepetitions; ++i) {
    retry_budgets->OnFailedRequest(destination);
    // Nothing changes in the budget
    ASSERT_TRUE(retry_budgets->CanRetry(destination));
  }

  for (size_t i = 0; i < kRepetitions; ++i) {
    retry_budgets->OnSuccessfulRequest(destination);
    // Nothing changes in the budget
    ASSERT_TRUE(retry_budgets->CanRetry(destination));
  }
}

UTEST(HttpRetryBudgets, SingleHost) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;
  const std::string destination = "test_host";

  auto retry_budgets = std::make_shared<clients::http::RetryBudgets>();

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets->SetRetryBudgetSettings(settings);

  for (size_t i = 0; i < 4; ++i) {
    retry_budgets->OnFailedRequest(destination);
    ASSERT_TRUE(retry_budgets->CanRetry(destination));
  }

  ASSERT_EQ(1, HttpRetryBudgetsChecker::GetDestinationsMapSize(*retry_budgets));

  for (size_t i = 0; i < 2; ++i) {
    retry_budgets->OnFailedRequest(destination);
  }
  // less than a half of the budget
  ASSERT_FALSE(retry_budgets->CanRetry(destination));

  for (size_t i = 0; i < 11; ++i) {
    ASSERT_FALSE(retry_budgets->CanRetry(destination));
    retry_budgets->OnSuccessfulRequest(destination);
  }

  // more than a half of a budget
  ASSERT_TRUE(retry_budgets->CanRetry(destination));
}

UTEST(HttpRetryBudgets, MultipleHosts) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;

  auto retry_budgets = std::make_shared<clients::http::RetryBudgets>();

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets->SetRetryBudgetSettings(settings);

  // empty the budgets of 5 hosts
  for (size_t i = 0; i < 5; ++i) {
    const auto destination = std::to_string(i);

    for (size_t i = 0; i < 6; ++i) {
      retry_budgets->OnFailedRequest(destination);
    }

    ASSERT_FALSE(retry_budgets->CanRetry(destination));
  }

  ASSERT_EQ(5, HttpRetryBudgetsChecker::GetDestinationsMapSize(*retry_budgets));

  // failed requests to the 6th host
  for (size_t i = 0; i < 3; ++i) {
    const auto destination = std::to_string(5);
    retry_budgets->OnFailedRequest(destination);
    ASSERT_TRUE(retry_budgets->CanRetry(destination));
  }

  // refill the budget of the 2nd host
  for (size_t i = 0; i < 30; ++i) {
    retry_budgets->OnSuccessfulRequest(std::to_string(2));
  }
  ASSERT_TRUE(retry_budgets->CanRetry(std::to_string(2)));

  ASSERT_EQ(6, HttpRetryBudgetsChecker::GetDestinationsMapSize(*retry_budgets));

  for (size_t i = 0; i <= 5; ++i) {
    if (i == 2 || i == 5) {
      ASSERT_TRUE(retry_budgets->CanRetry(std::to_string(i)));
    } else {
      ASSERT_FALSE(retry_budgets->CanRetry(std::to_string(i)));
    }
  }
}

UTEST(HttpRetryBudgets, SettingsUpdate) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;
  const std::string destination = "test_host";

  auto retry_budgets = std::make_shared<clients::http::RetryBudgets>();

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets->SetRetryBudgetSettings(settings);

  // take more than a half of tokens from the budget
  for (size_t i = 0; i < kMaxTokens / 2 + 1; ++i) {
    retry_budgets->OnFailedRequest(destination);
  }
  ASSERT_FALSE(retry_budgets->CanRetry(destination));

  // update settings to turn off retry budget mechanism
  retry_budgets->SetRetryBudgetSettings(
      CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, false));

  // more than a half of a budget
  ASSERT_TRUE(retry_budgets->CanRetry(destination));
}

UTEST(HttpRetryBudgets, MapReduction) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;

  auto retry_budgets = std::make_shared<clients::http::RetryBudgets>();

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets->SetRetryBudgetSettings(settings);

  // add destinations
  for (size_t i = 1; i < 257; ++i) {
    const auto destination = std::to_string(i);

    for (size_t j = 0; j < i; ++j) {
      retry_budgets->OnFailedRequest(destination);
    }
  }
  ASSERT_EQ(256,
            HttpRetryBudgetsChecker::GetDestinationsMapSize(*retry_budgets));

  // add more
  retry_budgets->OnFailedRequest(std::to_string(257));
  retry_budgets->OnFailedRequest(std::to_string(258));

  // ensure that map was reduced
  auto& map = HttpRetryBudgetsChecker::GetDestinationsMap(*retry_budgets);
  ASSERT_EQ(130, map.SizeApprox());
  for (auto it = map.begin(); it != map.end(); ++it) {
    auto i = utils::FromString<uint64_t>(it->first);
    ASSERT_TRUE(128 <= i);
    ASSERT_TRUE(i <= 258);
  }
}

UTEST_MT(HttpRetryBudgets, MapReductionMultiThread, 4) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  const auto kMaxTokens = 10;
  const auto kTokenRatio = 0.1f;

  auto retry_budgets = std::make_shared<clients::http::RetryBudgets>(
      engine::current_task::GetTaskProcessor());

  auto settings = CreateRetryBudgetSettings(kMaxTokens, kTokenRatio, true);
  retry_budgets->SetRetryBudgetSettings(settings);

  std::vector<engine::TaskWithResult<void>> tasks;
  for (size_t i = 0; i < 800; ++i) {
    auto destination = std::to_string(i);

    for (size_t task = 0; task < 3; ++task) {
      tasks.emplace_back(engine::AsyncNoSpan([retry_budgets, destination] {
        retry_budgets->OnFailedRequest(destination);

        for (size_t i = 0; i < 10; ++i) {
          if (utils::RandRange(2) == 0) {
            retry_budgets->OnFailedRequest(destination);
          } else {
            retry_budgets->OnSuccessfulRequest(destination);
          }
        }

        ASSERT_TRUE(HttpRetryBudgetsChecker::GetDestinationsMapSize(
                        *retry_budgets) < 300);
      }));
    }
  }

  engine::WaitAllChecked(tasks);

  ASSERT_TRUE(HttpRetryBudgetsChecker::GetDestinationsMapSize(*retry_budgets) <
              300);

  ASSERT_TRUE(HttpRetryBudgetsChecker::GetMapReductionCnt(*retry_budgets) < 10);
}

}  // namespace clients::http

USERVER_NAMESPACE_END
