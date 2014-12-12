#undef TRACE_SYSTEM
#define TRACE_SYSTEM cxl

#if !defined(_CXL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CXL_TRACE_H

#include <linux/tracepoint.h>

#include "cxl.h"

/* TODO: Dump out card & afu numbers as well */

#define DSISR_FLAGS __print_flags(__entry->dsisr, "|", \
	{CXL_PSL_DSISR_An_DS, "DS"}, \
	{CXL_PSL_DSISR_An_DM, "DM"}, \
	{CXL_PSL_DSISR_An_ST, "ST"}, \
	{CXL_PSL_DSISR_An_UR, "UR"}, \
	{CXL_PSL_DSISR_An_PE, "PE"}, \
	{CXL_PSL_DSISR_An_AE, "AE"}, \
	{CXL_PSL_DSISR_An_OC, "OC"}, \
	{CXL_PSL_DSISR_An_M, "M"}, \
	{CXL_PSL_DSISR_An_P, "P"}, \
	{CXL_PSL_DSISR_An_A, "A"}, \
	{CXL_PSL_DSISR_An_S, "S"}, \
	{CXL_PSL_DSISR_An_K, "K"})

TRACE_EVENT(cxl_afu_irq,
	TP_PROTO(int pe, int afu_irq, int virq, irq_hw_number_t hwirq),

	TP_ARGS(pe, afu_irq, virq, hwirq),

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(u16, afu_irq)
		__field(int, virq)
		__field(irq_hw_number_t, hwirq)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->afu_irq = afu_irq;
		__entry->virq = virq;
		__entry->hwirq = hwirq;
	),

	TP_printk("pe=%i afu_irq=%i virq=%i hwirq=0x%lx",
		__entry->pe,
		__entry->afu_irq,
		__entry->virq,
		__entry->hwirq)
);

TRACE_EVENT(cxl_psl_irq,
	TP_PROTO(int pe, int irq, u64 dsisr, u64 dar),

	TP_ARGS(pe, irq, dsisr, dar),

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(int, irq)
		__field(u64, dsisr)
		__field(u64, dar)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->irq = irq;
		__entry->dsisr = dsisr;
		__entry->dar = dar;
	),

	TP_printk("pe=%i irq=%i dsisr=%s dar=0x%.16llx",
		__entry->pe,
		__entry->irq,
		DSISR_FLAGS,
		__entry->dar)
);

TRACE_EVENT(cxl_psl_irq_ack,
	TP_PROTO(int pe, u64 tfc),

	TP_ARGS(pe, tfc),

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(u64, tfc)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->tfc = tfc;
	),

	TP_printk("pe=%i tfc=%s",
		__entry->pe,
		__print_flags(__entry->tfc, "|",
			{CXL_PSL_TFC_An_A, "A"},
			{CXL_PSL_TFC_An_C, "C"},
			{CXL_PSL_TFC_An_AE, "AE"},
			{CXL_PSL_TFC_An_R, "R"}
		)
	)
);

TRACE_EVENT(cxl_ste_miss,
	TP_PROTO(int pe, u64 dar),

	TP_ARGS(pe, dar),

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(u64, dar)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->dar = dar;
	),

	TP_printk("pe=%i dar=0x%.16llx",
		__entry->pe,
		__entry->dar)
);

TRACE_EVENT(cxl_ste_write,
	TP_PROTO(int pe, unsigned int idx, u64 e, u64 v),

	TP_ARGS(pe, idx, e, v),

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(unsigned int, idx)
		__field(u64, e)
		__field(u64, v)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->idx = idx;
		__entry->e = e;
		__entry->v = v;
	),

	TP_printk("pe=%i SSTE[%i] E=0x%.16llx V=0x%.16llx",
		__entry->pe,
		__entry->idx,
		__entry->e,
		__entry->v)
);

TRACE_EVENT(cxl_pte_miss,
	TP_PROTO(int pe, u64 dsisr, u64 dar),

	TP_ARGS(pe, dsisr, dar),

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(u64, dsisr)
		__field(u64, dar)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->dsisr = dsisr;
		__entry->dar = dar;
	),

	TP_printk("pe=%i dsisr=%s dar=0x%.16llx",
		__entry->pe,
		DSISR_FLAGS,
		__entry->dar)
);

#define CXL_PE_PROTO TP_PROTO(int pe)
#define CXL_PE_ARGS  TP_ARGS(pe)
DECLARE_EVENT_CLASS(cxl_pe,
	CXL_PE_PROTO,
	CXL_PE_ARGS,

	TP_STRUCT__entry(
		__field(u16, pe)
	),

	TP_fast_assign(
		__entry->pe = pe;
	),

	TP_printk("pe=%i",
		__entry->pe)
);

#define CXL_PE_RET_PROTO TP_PROTO(int pe, int rc)
#define CXL_PE_RET_ARGS  TP_ARGS(pe, rc)
DECLARE_EVENT_CLASS(cxl_pe_ret,
	CXL_PE_RET_PROTO,
	CXL_PE_RET_ARGS,

	TP_STRUCT__entry(
		__field(u16, pe)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->pe = pe;
		__entry->rc = rc;
	),

	TP_printk("pe=%i rc=%i",
		__entry->pe,
		__entry->rc)
);

DEFINE_EVENT(cxl_pe, cxl_slbia, CXL_PE_PROTO, CXL_PE_ARGS);
DEFINE_EVENT(cxl_pe, cxl_llcmd_add, CXL_PE_PROTO, CXL_PE_ARGS);
DEFINE_EVENT(cxl_pe, cxl_llcmd_term, CXL_PE_PROTO, CXL_PE_ARGS);
DEFINE_EVENT(cxl_pe, cxl_llcmd_rm, CXL_PE_PROTO, CXL_PE_ARGS);
DEFINE_EVENT(cxl_pe_ret, cxl_llcmd_add_done, CXL_PE_RET_PROTO, CXL_PE_RET_ARGS);
DEFINE_EVENT(cxl_pe_ret, cxl_llcmd_term_done, CXL_PE_RET_PROTO, CXL_PE_RET_ARGS);
DEFINE_EVENT(cxl_pe_ret, cxl_llcmd_rm_done, CXL_PE_RET_PROTO, CXL_PE_RET_ARGS);

#endif /* _CXL_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
