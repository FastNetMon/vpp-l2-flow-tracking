/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026
 */

#ifndef __included_l2flow_h__
#define __included_l2flow_h__

#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/sfdp/sfdp.h>

#define L2FLOW_INVALID_TENANT_INDEX ((u16) ~0)

typedef struct
{
  /* vec: SFDP tenant index per rx sw_if_index, ~0 when disabled */
  u16 *tenant_idx_by_sw_if_index;
} l2flow_main_t;

extern l2flow_main_t l2flow_main;

clib_error_t *l2flow_enable_disable (u32 sw_if_index, u32 tenant_id,
				     u8 disable);

#endif /* __included_l2flow_h__ */
