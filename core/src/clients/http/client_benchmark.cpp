#include <fmt/format.h>

#include <clients/http/retry_budgets.hpp>
#include <userver/clients/http/client.hpp>
#include <userver/engine/async.hpp>
#include <userver/engine/get_all.hpp>
#include <userver/engine/run_standalone.hpp>
#include <userver/engine/task/task_with_result.hpp>
#include <userver/utils/fixed_array.hpp>
#include <userver/utils/impl/userver_experiments.hpp>

#include <benchmark/benchmark.h>

using namespace std::chrono_literals;

USERVER_NAMESPACE_BEGIN

namespace clients::http {

namespace {

void CheckRetryBudget(clients::http::RetryBudgets& retry_budgets,
                      const std::string& destination) {
  retry_budgets.CanRetry(destination);
}

void UpdateRetryBudgetOnFail(clients::http::RetryBudgets& retry_budgets,
                             const std::string& destination) {
  retry_budgets.OnFailedRequest(destination);
}

}  // namespace

void HttpClientRetryBudgetsSingleHosts(benchmark::State& state) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  engine::RunStandalone(
      state.range(0),
      engine::TaskProcessorPoolsConfig{10000, 100000, 256 * 1024ULL, 8, "ev",
                                       false, false},
      [&] {
        static constexpr std::size_t kBatchSize = 10000;

        auto retry_budgets = clients::http::RetryBudgets();

        const std::string kDestination = "test_destination";
        UpdateRetryBudgetOnFail(
            retry_budgets,
            kDestination);  // it should fail once to be added to RetryBudgets
        for (auto _ : state) {
          auto tasks_update = utils::GenerateFixedArray(
              kBatchSize, [&retry_budgets, &kDestination](auto) {
                return engine::AsyncNoSpan(UpdateRetryBudgetOnFail,
                                           std::ref(retry_budgets),
                                           std::ref(kDestination));
              });
          auto tasks_check = utils::GenerateFixedArray(
              kBatchSize, [&retry_budgets, &kDestination](auto) {
                return engine::AsyncNoSpan(CheckRetryBudget,
                                           std::ref(retry_budgets),
                                           std::ref(kDestination));
              });
          engine::GetAll(tasks_update);
          engine::GetAll(tasks_check);
        }
      });
}

BENCHMARK(HttpClientRetryBudgetsSingleHosts)
    ->DenseRange(8, 40, 8)
    ->Unit(benchmark::kMicrosecond);

void HttpClientRetryBudgetsMultipleHosts(benchmark::State& state) {
  utils::impl::UserverExperimentsScope experiments;
  experiments.Set(utils::impl::kHttpClientRetryBudgetExperiment, true);

  engine::RunStandalone(
      state.range(0),
      engine::TaskProcessorPoolsConfig{10000, 100000, 256 * 1024ULL, 8, "ev",
                                       false, false},
      [&] {
        static constexpr std::size_t kBatchSize = 10000;
        static constexpr std::size_t kDestinationsSize = 512;

        auto retry_budgets = clients::http::RetryBudgets();
        auto destinations = utils::GenerateFixedArray(
            kDestinationsSize,
            [](auto index) { return fmt::format("destination_{}", index); });

        const std::string kDestination = "test_destination";
        UpdateRetryBudgetOnFail(retry_budgets, kDestination);
        for (auto _ : state) {
          auto tasks = utils::GenerateFixedArray(
              kBatchSize, [&retry_budgets, &destinations](auto index) {
                return engine::AsyncNoSpan(
                    UpdateRetryBudgetOnFail, std::ref(retry_budgets),
                    std::ref(destinations[index % destinations.size()]));
              });
          engine::GetAll(tasks);
        }
      });
}

BENCHMARK(HttpClientRetryBudgetsMultipleHosts)
    ->DenseRange(8, 40, 8)
    ->Unit(benchmark::kMicrosecond);

}  // namespace clients::http

USERVER_NAMESPACE_END
