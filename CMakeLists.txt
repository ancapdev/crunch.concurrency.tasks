# Copyright (c) 2011-2013, Christian Rorvik
# Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

include(configure.cmake)

vpm_add_library(crunch_concurrency_tasks_lib
  include/crunch/concurrency/index_range.hpp
  include/crunch/concurrency/iterator_range.hpp
  include/crunch/concurrency/parallel_for.hpp
  include/crunch/concurrency/range.hpp
  include/crunch/concurrency/task.hpp
  include/crunch/concurrency/task_execution_context.hpp
  include/crunch/concurrency/task_scheduler.hpp
  include/crunch/concurrency/tasks_api.hpp
  include/crunch/concurrency/work_stealing_queue.hpp
  include/crunch/concurrency/work_stealing_scheduler.hpp
  include/crunch/concurrency/detail/scheduled_task.hpp
  include/crunch/concurrency/detail/scheduled_task_execution_context.hpp
  include/crunch/concurrency/detail/task_result.hpp
  source/scheduled_task.cpp
  source/task.cpp
  source/task_scheduler.cpp
  source/work_stealing_scheduler.cpp)

target_link_libraries(crunch_concurrency_tasks_lib
  crunch_concurrency_lib)

if(NOT CRUNCH_NO_TESTS)
  vpm_set_default_version(crunch.test master)
  vpm_depend(crunch.test)

  add_executable(crunch_concurrency_tasks_test
    test/parallel_for_tests.cpp
    test/task_scheduler_tests.cpp
    test/work_stealing_queue_tests.cpp)

  target_link_libraries(crunch_concurrency_tasks_test
    crunch_concurrency_tasks_lib
    crunch_test_lib)

  crunch_add_test(crunch_concurrency_tasks_test)
endif()

if(NOT CRUNCH_NO_BENCHMARKS)
  vpm_set_default_version(crunch.benchmarking master)
  vpm_depend(crunch.benchmarking)

  add_executable(crunch_concurrency_tasks_benchmark
    benchmark/work_stealing_queue_benchmarks.cpp)

  target_link_libraries(crunch_concurrency_tasks_benchmark
    crunch_benchmarking_lib
    crunch_concurrency_tasks_lib)

  crunch_add_benchmark(crunch_concurrency_tasks_benchmark)
endif()

add_subdirectory(samples/fib)