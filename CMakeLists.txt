# Copyright (c) 2011, Christian Rorvik
# Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

include(configure.cmake)

vpm_depend(crunch_test)

vpm_add_library(crunch_concurrency_tasks_lib
  include/crunch/concurrency/task.hpp
  include/crunch/concurrency/task_scheduler.hpp
  source/task.cpp
  source/task_scheduler.cpp)

target_link_libraries(crunch_concurrency_tasks_lib
  crunch_concurrency_lib)

add_executable(crunch_concurrency_tasks_test
  test/task_scheduler_tests.cpp)

target_link_libraries(crunch_concurrency_tasks_test
  crunch_concurrency_tasks_lib
  crunch_test_lib)

crunch_add_test(crunch_concurrency_tasks_test)
