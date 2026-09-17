# SPDX-License-Identifier: Apache-2.0

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

add_vpp_plugin(l2flow
  SOURCES
  l2flow.c
  node.c
  cli.c

  MULTIARCH_SOURCES
  node.c
)
