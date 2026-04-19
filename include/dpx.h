#ifndef DPX_H
#define DPX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Draft DPX public header.
 *
 * This is intentionally a design-stage API sketch, not a stable ABI.
 *
 * The core lifetime rule is:
 * - backend selection happens before domain creation
 * - a domain stays on one backend pair for its whole lifetime
 * - in-flight work is not migrated across backends
 */

#define DPX_BACKEND_NAME_MAX 32

typedef struct dpx_domain dpx_domain;
typedef struct dpx_channel dpx_channel;

typedef uint64_t dpx_buffer_ref;

typedef enum dpx_scheduler_type {
	DPX_SCHED_GPU_FIRMWARE = 0,
	DPX_SCHED_HARDWARE_EVENT,
	DPX_SCHED_SOFTWARE_EVENT,
} dpx_scheduler_type;

typedef enum dpx_transfer_type {
	DPX_XFER_GPU_DMA = 0,
	DPX_XFER_DSA,
	DPX_XFER_IOAT,
	DPX_XFER_CPU_MEMCPY,
} dpx_transfer_type;

typedef enum dpx_sched_mode {
	DPX_SCHED_MODE_ATOMIC = 0,
	DPX_SCHED_MODE_ORDERED,
	DPX_SCHED_MODE_PARALLEL,
	DPX_SCHED_MODE_DIRECTED,
} dpx_sched_mode;

typedef enum dpx_order_contract {
	DPX_ORDER_TOTAL = 0,
	DPX_ORDER_PER_FLOW,
	DPX_ORDER_NONE,
} dpx_order_contract;

typedef enum dpx_priority_class {
	DPX_PRIORITY_BACKGROUND = 0,
	DPX_PRIORITY_NORMAL,
	DPX_PRIORITY_INTERACTIVE,
	DPX_PRIORITY_SYSTEM,
} dpx_priority_class;

typedef enum dpx_opcode {
	DPX_OP_COPY = 0,
	DPX_OP_FILL,
	DPX_OP_COMPARE,
	DPX_OP_CRC,
	DPX_OP_MARKER,
} dpx_opcode;

typedef enum dpx_completion_mode {
	DPX_COMPLETION_FENCE = 0,
	DPX_COMPLETION_POLLING,
	DPX_COMPLETION_CALLBACK,
	DPX_COMPLETION_SOFTWARE_SIGNAL,
} dpx_completion_mode;

typedef enum dpx_error_status {
	DPX_ERROR_STATUS_SUCCESS = 0,
	DPX_ERROR_STATUS_RETRYABLE,
	DPX_ERROR_STATUS_FATAL,
	DPX_ERROR_STATUS_UNSUPPORTED,
} dpx_error_status;

typedef enum dpx_error_scope {
	DPX_ERROR_SCOPE_ITEM = 0,
	DPX_ERROR_SCOPE_CHANNEL,
	DPX_ERROR_SCOPE_TRANSFER_ENGINE,
	DPX_ERROR_SCOPE_DOMAIN,
	DPX_ERROR_SCOPE_DEVICE,
} dpx_error_scope;

typedef struct dpx_backend_desc {
	char name[DPX_BACKEND_NAME_MAX];
	dpx_scheduler_type scheduler_type;
	dpx_transfer_type transfer_type;
	uint32_t flags;
	uint32_t reserved;
} dpx_backend_desc;

typedef struct dpx_capability_set {
	dpx_scheduler_type scheduler_type;
	dpx_transfer_type transfer_type;
	uint32_t max_channels;
	uint32_t max_flow_id;
	uint32_t scheduling_modes;
	uint32_t transfer_ops;
	dpx_completion_mode completion_mode;
	uint8_t supports_preemption;
	uint8_t supports_grouping;
	uint8_t supports_memory_registration;
	uint8_t reserved0;
	uint64_t typical_completion_latency_ns;
	dpx_error_scope worst_case_failure_scope;
	uint32_t reserved1;
} dpx_capability_set;

typedef struct dpx_domain_config {
	dpx_backend_desc backend;
	uint32_t flags;
	uint32_t capacity_hint;
	uint32_t reserved[2];
} dpx_domain_config;

typedef struct dpx_channel_config {
	dpx_sched_mode sched_mode;
	dpx_order_contract order;
	dpx_priority_class priority;
	uint32_t depth_hint;
	uint32_t flow_hint;
	uint32_t flags;
	uint32_t reserved[2];
} dpx_channel_config;

typedef struct dpx_buffer_desc {
	void *addr;
	uint64_t len;
	uint32_t flags;
	uint32_t reserved;
} dpx_buffer_desc;

typedef struct dpx_work_item {
	dpx_opcode opcode;
	uint32_t flags;
	uint32_t flow_id;
	uint32_t reserved0;
	dpx_buffer_ref src;
	dpx_buffer_ref dst;
	uint64_t src_offset;
	uint64_t dst_offset;
	uint64_t len;
	uint64_t cookie;
	dpx_channel *success_ch;
	dpx_channel *failure_ch;
	void *user_ptr;
} dpx_work_item;

typedef struct dpx_depth_info {
	uint32_t queued;
	uint32_t in_flight;
	uint32_t capacity;
	uint32_t reserved;
} dpx_depth_info;

typedef struct dpx_completion {
	uint64_t cookie;
	dpx_channel *channel;
	dpx_completion_mode mode;
	int32_t backend_status;
	uint32_t flags;
	void *user_ptr;
} dpx_completion;

typedef struct dpx_completion_batch {
	dpx_completion *items;
	uint32_t count;
	uint32_t capacity;
	uint32_t reserved[2];
} dpx_completion_batch;

typedef struct dpx_error {
	uint64_t cookie;
	dpx_channel *channel;
	dpx_error_status status;
	dpx_error_scope scope;
	int32_t backend_status;
	uint32_t flags;
	void *user_ptr;
} dpx_error;

typedef struct dpx_error_batch {
	dpx_error *items;
	uint32_t count;
	uint32_t capacity;
	uint32_t reserved[2];
} dpx_error_batch;

/*
 * Returns available backend pairs.
 *
 * If backends is NULL, count is filled with the number of available entries.
 * If backends is non-NULL, up to *count entries are written.
 */
int dpx_backend_detect(uint32_t flags, dpx_backend_desc *backends, size_t *count);

int dpx_backend_query_caps(const dpx_backend_desc *backend,
    dpx_capability_set *caps);

dpx_domain *dpx_domain_create(const dpx_domain_config *cfg);
void dpx_domain_destroy(dpx_domain *dom);

int dpx_domain_query_capabilities(dpx_domain *dom, dpx_capability_set *caps);

dpx_channel *dpx_channel_create(dpx_domain *dom,
    const dpx_channel_config *cfg);
void dpx_channel_destroy(dpx_channel *ch);
int dpx_channel_query_depth(dpx_channel *ch, dpx_depth_info *out);

int dpx_buffer_register(dpx_domain *dom, const dpx_buffer_desc *desc,
    dpx_buffer_ref *out);
int dpx_buffer_unregister(dpx_domain *dom, dpx_buffer_ref ref);

/*
 * Submit up to count work items.
 *
 * submitted receives the number of items accepted by the backend.
 * Implementations may accept zero, some, or all items depending on current
 * backpressure and queue depth.
 */
int dpx_channel_submit(dpx_channel *ch, const dpx_work_item *items,
    uint32_t count, uint32_t *submitted);

int dpx_completion_poll(dpx_domain *dom, uint64_t timeout_ns,
    dpx_completion_batch *out);
int dpx_error_poll(dpx_domain *dom, uint64_t timeout_ns,
    dpx_error_batch *out);

/* Capability-gated extensions. */
int dpx_channel_set_preemption_policy(dpx_channel *ch, uint32_t policy);
int dpx_channel_set_quantum(dpx_channel *ch, uint64_t quantum_ns);
int dpx_channel_set_group(dpx_channel *ch, uint32_t group_id);

#ifdef __cplusplus
}
#endif

#endif
