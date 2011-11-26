# Copyright (c) 2011, Christian Rorvik
# Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

vpm_set_default_versions(
  crunch.base trunk
  crunch.concurrency trunk)

vpm_depend(
  crunch.base
  crunch.concurrency)

vpm_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)