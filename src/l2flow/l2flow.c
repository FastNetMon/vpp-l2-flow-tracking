/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026
 */

#include <vnet/plugin/plugin.h>
#include <vpp/app/version.h>
#include <vnet/feature/feature.h>
#include <vnet/sfdp/common.h>
#include <vnet/sfdp/sfdp.h>
#include <l2flow/l2flow.h>

l2flow_main_t l2flow_main;

clib_error_t *
l2flow_enable_disable (u32 sw_if_index, u32 tenant_id, u8 disable)
{
  sfdp_main_t *sfdp = &sfdp_main;
  l2flow_main_t *lm = &l2flow_main;
  clib_bihash_kv_8_8_t kv = { .key = tenant_id, .value = 0 };

  if (!disable && clib_bihash_search_inline_8_8 (&sfdp->tenant_idx_by_id, &kv))
    return clib_error_return (0, "unknown SFDP tenant %u", tenant_id);

  vec_validate_init_empty (lm->tenant_idx_by_sw_if_index, sw_if_index,
			   L2FLOW_INVALID_TENANT_INDEX);

  if (vnet_feature_enable_disable ("device-input", "l2flow-input",
				   sw_if_index, !disable, 0, 0))
    return clib_error_return (0, "unable to %s l2flow-input on interface",
			      disable ? "disable" : "enable");

  lm->tenant_idx_by_sw_if_index[sw_if_index] =
    disable ? L2FLOW_INVALID_TENANT_INDEX : (u16) kv.value;
  return 0;
}

VLIB_PLUGIN_REGISTER () = {
  .version = VPP_BUILD_VER,
  .description = "L2 xconnect SFDP flow tracking",
};
