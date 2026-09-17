/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026
 */

#include <vlib/vlib.h>
#include <vnet/feature/feature.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/sfdp/common.h>
#include <vnet/sfdp/sfdp.h>
#include <vnet/sfdp/service.h>
#include <l2flow/l2flow.h>

/*
 * l2flow-input: sits on the device-input feature arc, ahead of
 * ethernet-input. Untagged IPv4/IPv6 packets are associated with the
 * configured SFDP tenant and steered into the SFDP lookup nodes for flow
 * tracking. Everything else continues down the arc untouched, so the
 * l2 xconnect path keeps working for non-IP traffic.
 */

#define foreach_l2flow_input_error _ (STEERED, "steered into SFDP")

typedef enum
{
#define _(sym, str) L2FLOW_INPUT_ERROR_##sym,
  foreach_l2flow_input_error
#undef _
    L2FLOW_INPUT_N_ERROR,
} l2flow_input_error_t;

static char *l2flow_input_error_strings[] = {
#define _(sym, string) string,
  foreach_l2flow_input_error
#undef _
};

typedef enum
{
  L2FLOW_INPUT_NEXT_LOOKUP_IP4,
  L2FLOW_INPUT_NEXT_LOOKUP_IP6,
  L2FLOW_INPUT_N_NEXT,
} l2flow_input_next_t;

typedef struct
{
  u32 sw_if_index;
  u16 tenant_idx;
  u16 next_index;
} l2flow_input_trace_t;

static u8 *
format_l2flow_input_trace (u8 *s, va_list *args)
{
  vlib_main_t __clib_unused *vm = va_arg (*args, vlib_main_t *);
  vlib_node_t __clib_unused *node = va_arg (*args, vlib_node_t *);
  l2flow_input_trace_t *t = va_arg (*args, l2flow_input_trace_t *);

  s = format (s, "l2flow-input: sw_if_index %u tenant_idx %u next %u",
	      t->sw_if_index, t->tenant_idx, t->next_index);
  return s;
}

VLIB_NODE_FN (l2flow_input_node)
(vlib_main_t *vm, vlib_node_runtime_t *node, vlib_frame_t *frame)
{
  sfdp_main_t *sfdp = &sfdp_main;
  l2flow_main_t *lm = &l2flow_main;
  vlib_combined_counter_main_t *cm =
    &sfdp->tenant_data_ctr[SFDP_TENANT_DATA_COUNTER_INCOMING];
  u32 thread_index = vlib_get_thread_index ();
  vlib_buffer_t *bufs[VLIB_FRAME_SIZE], **b = bufs;
  u16 nexts[VLIB_FRAME_SIZE], *next = nexts;
  u32 *from = vlib_frame_vector_args (frame);
  u32 n_left = frame->n_vectors;
  u32 n_steered = 0;

  vlib_get_buffers (vm, from, bufs, n_left);

  while (n_left > 0)
    {
      u32 sw_if_index = vnet_buffer (b[0])->sw_if_index[VLIB_RX];
      u16 tenant_idx = sw_if_index < vec_len (lm->tenant_idx_by_sw_if_index) ?
			 lm->tenant_idx_by_sw_if_index[sw_if_index] :
			 L2FLOW_INVALID_TENANT_INDEX;
      ethernet_header_t *e = vlib_buffer_get_current (b[0]);
      u16 ethertype = clib_net_to_host_u16 (e->type);
      u8 is_ip4 = ethertype == ETHERNET_TYPE_IP4;
      u8 is_ip6 = ethertype == ETHERNET_TYPE_IP6;

      if ((is_ip4 || is_ip6) && tenant_idx != L2FLOW_INVALID_TENANT_INDEX &&
	  vlib_buffer_has_space (b[0], sizeof (ethernet_header_t) +
					 sizeof (ip4_header_t)))
	{
	  sfdp_tenant_t *tenant = sfdp_tenant_at_index (sfdp, tenant_idx);

	  /* sfdp lookup expects current_data at the IP header */
	  vlib_buffer_advance (b[0], sizeof (ethernet_header_t));
	  b[0]->flow_id = tenant->context_id;
	  sfdp_buffer (b[0])->tenant_index = tenant_idx;
	  next[0] = is_ip4 ? L2FLOW_INPUT_NEXT_LOOKUP_IP4 :
			     L2FLOW_INPUT_NEXT_LOOKUP_IP6;
	  vlib_increment_combined_counter (
	    cm, thread_index, tenant_idx, 1,
	    vlib_buffer_length_in_chain (vm, b[0]));
	  n_steered++;
	}
      else
	vnet_feature_next_u16 (next, b[0]);

      if (PREDICT_FALSE (b[0]->flags & VLIB_BUFFER_IS_TRACED))
	{
	  l2flow_input_trace_t *t =
	    vlib_add_trace (vm, node, b[0], sizeof (*t));
	  t->sw_if_index = sw_if_index;
	  t->tenant_idx = tenant_idx;
	  t->next_index = next[0];
	}

      b++;
      next++;
      n_left--;
    }

  vlib_node_increment_counter (vm, node->node_index,
			       L2FLOW_INPUT_ERROR_STEERED, n_steered);
  vlib_buffer_enqueue_to_next (vm, node, from, nexts, frame->n_vectors);
  return frame->n_vectors;
}

VLIB_REGISTER_NODE (l2flow_input_node) = {
  .name = "l2flow-input",
  .vector_size = sizeof (u32),
  .format_trace = format_l2flow_input_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  .n_errors = L2FLOW_INPUT_N_ERROR,
  .error_strings = l2flow_input_error_strings,
  .n_next_nodes = L2FLOW_INPUT_N_NEXT,
  .next_nodes = {
    [L2FLOW_INPUT_NEXT_LOOKUP_IP4] = "sfdp-lookup-ip4",
    [L2FLOW_INPUT_NEXT_LOOKUP_IP6] = "sfdp-lookup-ip6",
  },
};

VNET_FEATURE_INIT (l2flow_input_feat, static) = {
  .arc_name = "device-input",
  .node_name = "l2flow-input",
  .runs_before = VNET_FEATURES ("ethernet-input"),
};

/*
 * l2flow-output: terminal SFDP service. Rewinds the buffer to the
 * ethernet header and hands the packet back to ethernet-input, which
 * dispatches it into the regular L2 path (l2-input -> xconnect output).
 */

#define foreach_l2flow_output_error _ (TRACKED, "flow tracked")

typedef enum
{
#define _(sym, str) L2FLOW_OUTPUT_ERROR_##sym,
  foreach_l2flow_output_error
#undef _
    L2FLOW_OUTPUT_N_ERROR,
} l2flow_output_error_t;

static char *l2flow_output_error_strings[] = {
#define _(sym, string) string,
  foreach_l2flow_output_error
#undef _
};

#define foreach_l2flow_output_next _ (ETHERNET_INPUT, "ethernet-input")

typedef enum
{
#define _(n, x) L2FLOW_OUTPUT_NEXT_##n,
  foreach_l2flow_output_next
#undef _
    L2FLOW_OUTPUT_N_NEXT
} l2flow_output_next_t;

typedef struct
{
  u32 flow_id;
} l2flow_output_trace_t;

static u8 *
format_l2flow_output_trace (u8 *s, va_list *args)
{
  vlib_main_t __clib_unused *vm = va_arg (*args, vlib_main_t *);
  vlib_node_t __clib_unused *node = va_arg (*args, vlib_node_t *);
  l2flow_output_trace_t *t = va_arg (*args, l2flow_output_trace_t *);

  s = format (s, "l2flow-output: flow-id %u (session %u, %s)", t->flow_id,
	      t->flow_id >> 1, t->flow_id & 0x1 ? "reverse" : "forward");
  return s;
}

VLIB_NODE_FN (l2flow_output_node)
(vlib_main_t *vm, vlib_node_runtime_t *node, vlib_frame_t *frame)
{
  vlib_buffer_t *bufs[VLIB_FRAME_SIZE], **b = bufs;
  u16 next_indices[VLIB_FRAME_SIZE], *to_next = next_indices;
  u32 *from = vlib_frame_vector_args (frame);
  u32 n_left = frame->n_vectors;

  vlib_get_buffers (vm, from, bufs, n_left);

  while (n_left > 0)
    {
      /* back to the ethernet header for the l2 path */
      vlib_buffer_advance (b[0], -(word) sizeof (ethernet_header_t));
      to_next[0] = L2FLOW_OUTPUT_NEXT_ETHERNET_INPUT;

      b++;
      to_next++;
      n_left--;
    }

  if (PREDICT_FALSE (node->flags & VLIB_NODE_FLAG_TRACE))
    {
      n_left = frame->n_vectors;
      b = bufs;
      for (u32 i = 0; i < n_left; i++)
	{
	  if (b[0]->flags & VLIB_BUFFER_IS_TRACED)
	    {
	      l2flow_output_trace_t *t =
		vlib_add_trace (vm, node, b[0], sizeof (*t));
	      t->flow_id = b[0]->flow_id;
	      b++;
	    }
	  else
	    break;
	}
    }

  vlib_node_increment_counter (vm, node->node_index,
			       L2FLOW_OUTPUT_ERROR_TRACKED, frame->n_vectors);
  vlib_buffer_enqueue_to_next (vm, node, from, next_indices,
			       frame->n_vectors);
  return frame->n_vectors;
}

VLIB_REGISTER_NODE (l2flow_output_node) = {
  .name = "l2flow-output",
  .vector_size = sizeof (u32),
  .format_trace = format_l2flow_output_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  .n_errors = L2FLOW_OUTPUT_N_ERROR,
  .error_strings = l2flow_output_error_strings,
  .n_next_nodes = L2FLOW_OUTPUT_N_NEXT,
  .next_nodes = {
#define _(n, x) [L2FLOW_OUTPUT_NEXT_##n] = x,
    foreach_l2flow_output_next
#undef _
  },
};

SFDP_SERVICE_DEFINE (l2flow_output) = {
  .node_name = "l2flow-output",
  .runs_before = SFDP_SERVICES (0),
  .runs_after = SFDP_SERVICES ("sfdp-drop", "sfdp-l4-lifecycle",
			       "sfdp-tcp-check"),
  .is_terminal = 1,
};
