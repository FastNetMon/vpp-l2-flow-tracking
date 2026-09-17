/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026
 */

#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <l2flow/l2flow.h>

static clib_error_t *
l2flow_enable_disable_command_fn (vlib_main_t *vm, unformat_input_t *input,
				  vlib_cli_command_t *cmd)
{
  unformat_input_t line_input_, *line_input = &line_input_;
  vnet_main_t *vnm = vnet_get_main ();
  clib_error_t *err = 0;
  u32 sw_if_index = ~0;
  u32 tenant_id = ~0;
  u8 disable = 0;

  if (!unformat_user (input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "%U", unformat_vnet_sw_interface, vnm,
		    &sw_if_index))
	;
      else if (unformat (line_input, "tenant %u", &tenant_id))
	;
      else if (unformat (line_input, "disable"))
	disable = 1;
      else
	{
	  err = unformat_parse_error (line_input);
	  goto done;
	}
    }

  if (sw_if_index == ~0)
    {
      err = clib_error_return (0, "missing interface");
      goto done;
    }
  if (!disable && tenant_id == ~0)
    {
      err = clib_error_return (0, "missing tenant id");
      goto done;
    }

  err = l2flow_enable_disable (sw_if_index, tenant_id, disable);
done:
  unformat_free (line_input);
  return err;
}

VLIB_CLI_COMMAND (l2flow_enable_disable_command, static) = {
  .path = "set l2flow interface",
  .short_help = "set l2flow interface <interface> tenant <tenant-id> [disable]",
  .function = l2flow_enable_disable_command_fn,
};

static clib_error_t *
l2flow_show_command_fn (vlib_main_t *vm, unformat_input_t *input,
			vlib_cli_command_t *cmd)
{
  l2flow_main_t *lm = &l2flow_main;
  vnet_main_t *vnm = vnet_get_main ();
  u32 sw_if_index;

  vec_foreach_index (sw_if_index, lm->tenant_idx_by_sw_if_index)
    {
      u16 tenant_idx = lm->tenant_idx_by_sw_if_index[sw_if_index];
      if (tenant_idx == L2FLOW_INVALID_TENANT_INDEX)
	continue;
      vlib_cli_output (vm, "%U: tenant index %u", format_vnet_sw_if_index_name,
		       vnm, sw_if_index, tenant_idx);
    }
  return 0;
}

VLIB_CLI_COMMAND (l2flow_show_command, static) = {
  .path = "show l2flow interfaces",
  .short_help = "show l2flow interfaces",
  .function = l2flow_show_command_fn,
};
