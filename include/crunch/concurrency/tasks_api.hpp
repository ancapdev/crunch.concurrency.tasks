// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASKS_API_HPP
#define CRUNCH_CONCURRENCY_TASKS_API_HPP

#include "crunch/base/platform.hpp"

#if defined (CRUNCH_PLATFORM_WIN32) && defined (VPM_SHARED_LIB_BUILD)
#   if defined (crunch_concurrency_tasks_lib_EXPORTS)
#       define CRUNCH_CONCURRENCY_TASKS_API __declspec(dllexport)
#   else
#       define CRUNCH_CONCURRENCY_TASKS_API __declspec(dllimport)
#   endif
#else
#   define CRUNCH_CONCURRENCY_TASKS_API
#endif

#endif
