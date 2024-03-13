# ##############################################################################
# apps/system/libuv/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

if(CONFIG_LIBUV)

  # ############################################################################
  # Config and Fetch libuv
  # ############################################################################
  set(LIBUV_DIR ${CMAKE_CURRENT_LIST_DIR}/libuv)
  set(LIBUV_VERSION v1.46.0)
  set(LIBUV_URL https://github.com/libuv/libuv/archive/refs/tags)

  if(NOT EXISTS ${LIBUV_DIR})
    FetchContent_Declare(
      libuv_fetch
      URL ${LIBUV_URL}/${LIBUV_VERSION}.zip SOURCE_DIR ${LIBUV_DIR} BINARY_DIR
          ${CMAKE_BINARY_DIR}/apps/system/libuv/libuv
      PATCH_COMMAND patch -s -N -p1 -d ${LIBUV_DIR} <
                    ${CMAKE_CURRENT_LIST_DIR}/0001-libuv-port-for-nuttx.patch
      DOWNLOAD_NO_PROGRESS true
      TIMEOUT 30)

    FetchContent_GetProperties(libuv_fetch)

    if(NOT libuv_fetch_POPULATED)
      FetchContent_Populate(libuv_fetch)
    endif()
  endif()

  nuttx_add_library(uv STATIC)

  # ############################################################################
  # Flags and Sources
  # ############################################################################

  set(INCDIR ${LIBUV_DIR}/src ${LIBUV_DIR}/src/unix ${LIBUV_DIR}/src/test)
  set(CFLAGS -Wno-shadow -DDEF_THREADPOOL_SIZE=${CONFIG_LIBUV_THREADPOOL_SIZE}
             -DDEF_THREADPOOL_STACKSIZE=${CONFIG_LIBUV_THREAD_STACKSIZE})

  if(NOT DEFINED GCCVER)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version
                    OUTPUT_VARIABLE GCC_VERSION_OUTPUT)
    string(REGEX MATCH "\\+\\+.* ([0-9]+)\\.[0-9]+" GCC_VERSION_REGEX
                 "${GCC_VERSION_OUTPUT}")
    set(GCCVER ${CMAKE_MATCH_1})
  endif()

  if(GCCVER EQUAL 12)
    list(APPEND CFLAGS -Wno-dangling-pointer)
  endif()

  set(CSRCS
      ${LIBUV_DIR}/src/unix/core.c
      ${LIBUV_DIR}/src/unix/poll.c
      ${LIBUV_DIR}/src/unix/loop.c
      ${LIBUV_DIR}/src/unix/thread.c
      ${LIBUV_DIR}/src/thread-common.c
      ${LIBUV_DIR}/src/unix/posix-hrtime.c
      ${LIBUV_DIR}/src/unix/posix-poll.c
      ${LIBUV_DIR}/src/uv-data-getter-setters.c
      ${LIBUV_DIR}/src/version.c
      ${LIBUV_DIR}/src/unix/no-fsevents.c
      ${LIBUV_DIR}/src/uv-common.c
      ${LIBUV_DIR}/src/unix/random-devurandom.c
      ${LIBUV_DIR}/src/random.c
      ${LIBUV_DIR}/src/unix/nuttx.c
      ${LIBUV_DIR}/src/unix/tty.c
      ${LIBUV_DIR}/src/unix/loop-watcher.c
      ${LIBUV_DIR}/src/unix/signal.c
      ${LIBUV_DIR}/src/unix/stream.c
      ${LIBUV_DIR}/src/threadpool.c
      ${LIBUV_DIR}/src/unix/async.c
      ${LIBUV_DIR}/src/unix/pipe.c
      ${LIBUV_DIR}/src/unix/fs.c
      ${LIBUV_DIR}/src/fs-poll.c
      ${LIBUV_DIR}/src/timer.c
      ${LIBUV_DIR}/src/unix/sysinfo-loadavg.c
      ${LIBUV_DIR}/src/unix/sysinfo-memory.c)

  if(NOT CONFIG_LIBUV_UTILS_TEST)
    list(APPEND CSRCS ${LIBUV_DIR}/src/idna.c ${LIBUV_DIR}/src/strscpy.c
         ${LIBUV_DIR}/src/strtok.c)
  endif()

  if(CONFIG_LIBC_EXECFUNCS)
    list(APPEND CSRCS ${LIBUV_DIR}/src/unix/process-spawn.c)
  endif()

  if(CONFIG_LIBC_DLFCN)
    list(APPEND CSRCS ${LIBUV_DIR}/src/unix/dl.c)
  endif()

  if(CONFIG_LIBC_NETDB)
    list(APPEND CSRCS ${LIBUV_DIR}/src/unix/getaddrinfo.c
         ${LIBUV_DIR}/src/unix/getnameinfo.c)
  endif()

  if(CONFIG_NET)
    list(APPEND CSRCS ${LIBUV_DIR}/src/unix/inet.c)
  endif()

  if(CONFIG_NET_TCP)
    list(APPEND CSRCS tcp.c)
  endif()

  if(CONFIG_NET_UDP)
    list(APPEND CSRCS udp.c)
  endif()

  if(CONFIG_LIBUV_UTILS_TEST OR CONFIG_LIBUV_UTILS_BENCHMARK)
    set(PRIORITY ${CONFIG_LIBUV_UTILS_PRIORITY})
    set(STACKSIZE ${CONFIG_LIBUV_UTILS_STACKSIZE})
  endif()

  if(CONFIG_LIBUV_UTILS_TEST)
    file(GLOB TEST_CSRCS "libuv/test/test-*.c")
    list(INSERT TEST_CSRCS 0 ${LIBUV_DIR}/test/run-tests.c)
    list(APPEND TEST_CSRCS ${LIBUV_DIR}/test/runner.c
         ${LIBUV_DIR}/test/runner-unix.c ${LIBUV_DIR}/test/echo-server.c)

    nuttx_add_application(
      NAME
      uv_run_tests
      PRIORITY
      ${CONFIG_LIBUV_UTILS_PRIORITY}
      STACKSIZE
      ${CONFIG_LIBUV_UTILS_STACKSIZE}
      COMPILE_FLAGS
      -Wno-implicit-function-declaration
      -Wno-shadow
      SRCS
      ${TEST_CSRCS}
      DEPENDS
      uv)
  endif()

  if(CONFIG_LIBUV_UTILS_BENCHMARK)
    list(
      APPEND
      BENCHMARK_CSRCS
      ${LIBUV_DIR}/test/run-benchmarks.c
      ${LIBUV_DIR}/test/runner.c
      ${LIBUV_DIR}/test/runner-unix.c
      ${LIBUV_DIR}/test/benchmark-async-pummel.c
      ${LIBUV_DIR}/test/benchmark-async.c
      ${LIBUV_DIR}/test/benchmark-fs-stat.c
      ${LIBUV_DIR}/test/benchmark-getaddrinfo.c
      ${LIBUV_DIR}/test/benchmark-loop-count.c
      ${LIBUV_DIR}/test/benchmark-million-async.c
      ${LIBUV_DIR}/test/benchmark-million-timers.c
      ${LIBUV_DIR}/test/benchmark-multi-accept.c
      ${LIBUV_DIR}/test/benchmark-ping-pongs.c
      ${LIBUV_DIR}/test/benchmark-ping-udp.c
      ${LIBUV_DIR}/test/benchmark-pound.c
      ${LIBUV_DIR}/test/benchmark-pump.c
      ${LIBUV_DIR}/test/benchmark-sizes.c
      ${LIBUV_DIR}/test/benchmark-spawn.c
      ${LIBUV_DIR}/test/benchmark-tcp-write-batch.c
      ${LIBUV_DIR}/test/benchmark-thread.c
      ${LIBUV_DIR}/test/benchmark-udp-pummel.c
      ${LIBUV_DIR}/test/blackhole-server.c
      ${LIBUV_DIR}/test/echo-server.c)

    nuttx_add_application(
      NAME
      uv_run_benchmarks
      PRIORITY
      ${CONFIG_LIBUV_UTILS_PRIORITY}
      STACKSIZE
      ${CONFIG_LIBUV_UTILS_STACKSIZE}
      COMPILE_FLAGS
      -Wno-implicit-function-declaration
      -Wno-shadow
      -Wno-undef
      SRCS
      ${BENCHMARK_CSRCS}
      DEPENDS
      uv)
  endif()

  # ############################################################################
  # Library Configuration
  # ############################################################################

  target_sources(uv PRIVATE ${CSRCS})
  target_include_directories(uv PRIVATE ${INCDIR})
  target_compile_options(uv PRIVATE ${CFLAGS})

  target_include_directories(uv PUBLIC ${LIBUV_DIR}/include)
endif()
