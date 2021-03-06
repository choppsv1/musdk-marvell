/******************************************************************************
 *	Copyright (C) 2016 Marvell International Ltd.
 *
 *  If you received this File from Marvell, you may opt to use, redistribute
 *  and/or modify this File under the following licensing terms.
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 *
 *	* Neither the name of Marvell nor the names of its contributors may be
 *	  used to endorse or promote products derived from this software
 *	  without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

/**
 * @file pp2_bm.c
 *
 * Buffer management and manipulation routines
 */
#include "std_internal.h"
#include "pp2_types.h"

#include "pp2.h"
#include "pp2_bm.h"
#include "pp2_port.h"

#if PP2_BM_BUF_DEBUG
/* Debug and helpers */
static inline void pp2_bm_print_reg(uintptr_t cpu_slot,
				    unsigned int reg_addr, const char *reg_name)
{
	pr_debug("  %-32s: 0x%X = 0x%08X\n", reg_name, reg_addr,
		pp2_reg_read(cpu_slot, reg_addr));
}

static void pp2_bm_pool_print_regs(uintptr_t cpu_slot, uint32_t pool)
{
	pr_debug("[BM pool registers: cpu_slot=0X%lx pool=%u]\n", cpu_slot, pool);

	pp2_bm_print_reg(cpu_slot, MVPP2_BM_POOL_BASE_ADDR_REG(pool),
			 "MVPP2_BM_POOL_BASE_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_POOL_SIZE_REG(pool),
			 "MVPP2_BM_POOL_SIZE_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_POOL_READ_PTR_REG(pool),
			 "MVPP2_BM_POOL_READ_PTR_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_POOL_PTRS_NUM_REG(pool),
			 "MVPP2_BM_POOL_PTRS_NUM_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_BPPI_READ_PTR_REG(pool),
			 "MVPP2_BM_BPPI_READ_PTR_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_BPPI_PTRS_NUM_REG(pool),
			 "MVPP2_BM_BPPI_PTRS_NUM_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool),
			 "MVPP2_BM_POOL_CTRL_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_INTR_CAUSE_REG(pool),
			 "MVPP2_BM_INTR_CAUSE_REG");
	pp2_bm_print_reg(cpu_slot, MVPP2_BM_INTR_MASK_REG(pool),
			 "MVPP2_BM_INTR_MASK_REG");
}
#endif
/** End debug helpers */

/* Set BM pool buffer size in HW.
 * Buffer size must be aligned to MVPP2_POOL_BUF_SIZE_OFFSET
 */
static inline void pp2_bm_pool_bufsize_set(uintptr_t cpu_slot,
					   u32 pool_id, uint32_t buf_size)
{
	u32 val = ALIGN(buf_size, 1 << MVPP2_POOL_BUF_SIZE_OFFSET);

	pp2_reg_write(cpu_slot, MVPP2_POOL_BUF_SIZE_REG(pool_id), val);
}

/* BM HW disable pool */
void pp2_bm_hw_pool_destroy(uintptr_t cpu_slot, uint32_t pool_id)
{
	u32 val;

	val = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id));

	if (val & MVPP2_BM_STATE_MASK) {
		val |= MVPP2_BM_STOP_MASK;

		pp2_reg_write(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id), val);

		pr_debug("BM: stopping pool %u ...\n", pool_id);
		/* Wait pool stop notification */
		do {
			val = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id));
		} while (val & MVPP2_BM_STATE_MASK);
		pr_debug("BM: stopped pool %u ...\n", pool_id);
	}

	/* Mask & Clear interrupt flags */
	pp2_reg_write(cpu_slot, MVPP2_BM_INTR_MASK_REG(pool_id), 0);
	pp2_reg_write(cpu_slot, MVPP2_BM_INTR_CAUSE_REG(pool_id), 0);

	/* Clear BPPE base */
	pp2_reg_write(cpu_slot, MVPP2_BM_POOL_BASE_ADDR_REG(pool_id), 0);
	pp2_reg_write(cpu_slot, MVPP22_BM_POOL_BASE_ADDR_HIGH_REG,
		      0 & MVPP22_BM_POOL_BASE_ADDR_HIGH_MASK);

	pp2_bm_pool_bufsize_set(cpu_slot, pool_id, 0);
#if PP2_BM_BUF_DEBUG
	pp2_bm_pool_print_regs(cpu_slot, pool_id);
#endif
}

/* BM pool hardware enable */
static uint32_t
pp2_bm_hw_pool_create(uintptr_t cpu_slot, uint32_t pool_id,
		      u32 bppe_num, uintptr_t pool_phys_addr)
{
	u32 val;
	u32 phys_lo;
	u32 phys_hi;
	u32 pool_bufs;

	phys_lo = ((uint32_t)pool_phys_addr) & MVPP2_BM_POOL_BASE_ADDR_MASK;
	phys_hi = ((uint64_t)pool_phys_addr) >> 32;

	/* Check control register to see if this pool is already initialized */
	val = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id));
	if (val & MVPP2_BM_STATE_MASK) {
		pr_err("BM: pool=%u is already active\n", pool_id);
		return 1;
	}

	pp2_reg_write(cpu_slot, MVPP2_BM_POOL_BASE_ADDR_REG(pool_id), phys_lo);
	pp2_reg_write(cpu_slot, MVPP22_BM_POOL_BASE_ADDR_HIGH_REG,
		      phys_hi & MVPP22_BM_POOL_BASE_ADDR_HIGH_MASK);

	pool_bufs = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_SIZE_REG(pool_id));

	if (!pool_bufs)
		pp2_reg_write(cpu_slot, MVPP2_BM_POOL_SIZE_REG(pool_id), bppe_num);
	else if (pool_bufs != bppe_num) {
		pr_err("BM: pool%u: already configured pool size (%d) does not match the required value (%d)\n",
			pool_id, pool_bufs, bppe_num);
		return 1;
	}

	val = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id));
	val |= MVPP2_BM_START_MASK;

	pp2_reg_write(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id), val);

	/* Wait pool start notification */
	do {
		val = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_CTRL_REG(pool_id));
	} while (!(val & MVPP2_BM_STATE_MASK));

	return 0;
}

int pp2_bm_pool_create(struct pp2 *pp2, struct bm_pool_param *param)
{
	struct pp2_inst *pp2_inst;
	uintptr_t cpu_slot;
	u32 bppe_num;
	u32 bppe_size;
	u32 bppe_region_size, num_bm_pools;
	struct pp2_bm_pool *bm_pool;

	/* FS_A8K Table 1558: Provided buffer numbers divisible by
	 * PP2_BPPE_UNIT_SIZE in order to avoid incomplete BPPEs
	 */
	if (param->buf_num % PP2_BPPE_UNIT_SIZE) {
		pr_err("BM: pool buffer number param must be a multiple of %u\n",
			PP2_BPPE_UNIT_SIZE);
		return -EACCES;
	}
	/* FS_A8K Table 1713: Buffer 32-byte aligned and greater
	 * than packet offset configured in RXSWQCFG register
	 */
	if (param->buf_size < PP2_PACKET_OFFSET) {
		pr_err("BM: pool buffer size must be 32-byte aligned and greater than PP2_PACKET_OFFSET(%lu)\n",
			PP2_PACKET_OFFSET);
		return -EACCES;
	}
	/* Allocate space for pool handler */
	bm_pool = kcalloc(1, sizeof(struct pp2_bm_pool), GFP_KERNEL);
	if (unlikely(!bm_pool)) {
		pr_err("BM: cannot allocate memory for a BM pool\n");
		return -ENOMEM;
	}

	/* Offset pool hardware ID depending on configured pool offset */
	bm_pool->bm_pool_id = param->id;
	bm_pool->bm_pool_buf_sz = param->buf_size;

	/* Store packet processor parent ID */
	bm_pool->pp2_id = param->pp2_id;

	/* Number of buffers */
	bm_pool->bm_pool_buf_num = param->buf_num;

	/* FS_A8K Table 1558: A BPPE holds 8 x BPs (buffers), and for each
	 * buffer, 2 x pointer sizes must be allocated. The BPPE region size
	 * is computed by adding up all BPPEs.
	 *
	 * Always aligned to PP2_BPPE_UNIT_SIZE
	 */
	bppe_num = bm_pool->bm_pool_buf_num;
	bppe_size = (2 * sizeof(uint64_t));
	bppe_region_size = (bppe_num * bppe_size);

	pr_debug("BM: pool=%u buf_num %u bppe_num %u bppe_region_size %u\n",
		bm_pool->bm_pool_id, bm_pool->bm_pool_buf_num, bppe_num, bppe_region_size);

	bm_pool->bm_pool_virt_base = (uintptr_t)mv_sys_dma_mem_alloc(bppe_region_size, MVPP2_BM_POOL_PTR_ALIGN);
	if (unlikely(!bm_pool->bm_pool_virt_base)) {
		pr_err("BM: cannot allocate region for pool BPPEs\n");
		kfree(bm_pool);
		return -ENOMEM;
	}

	bm_pool->bm_pool_phys_base = (uintptr_t)mv_sys_dma_mem_virt2phys((void *)bm_pool->bm_pool_virt_base);

	if (!IS_ALIGNED(bm_pool->bm_pool_phys_base, MVPP2_BM_POOL_PTR_ALIGN)) {
		pr_err("BM: pool=%u is not %u bytes aligned", param->id,
			MVPP2_BM_POOL_PTR_ALIGN);
		mv_sys_dma_mem_free((void *)bm_pool->bm_pool_virt_base);
		kfree(bm_pool);
		return -EIO;
	}

	pr_debug("BM: pool=%u BPPEs phys_base 0x%lX virt_base 0x%lX\n", bm_pool->bm_pool_id,
		bm_pool->bm_pool_phys_base, bm_pool->bm_pool_virt_base);

	pp2_inst = pp2->pp2_inst[param->pp2_id];
	cpu_slot = pp2_inst->hw.base[PP2_DEFAULT_REGSPACE].va;

	/*TODO YUVAL: Add lock here, to protect simultaneous creation of bm_pools */
	if (pp2_bm_hw_pool_create(cpu_slot, bm_pool->bm_pool_id,
				  bppe_num, bm_pool->bm_pool_phys_base)) {
		pr_err("BM: could not initialize hardware pool%u\n", bm_pool->bm_pool_id);
		mv_sys_dma_mem_free((void *)bm_pool->bm_pool_virt_base);
		kfree(bm_pool);
		return -EIO;
	}

	pp2_bm_pool_bufsize_set(cpu_slot, bm_pool->bm_pool_id, bm_pool->bm_pool_buf_sz);

#if PP2_BM_BUF_DEBUG
	pp2_bm_pool_print_regs(cpu_slot, bm_pool->bm_pool_id);
#endif
	num_bm_pools = pp2->pp2_inst[param->pp2_id]->num_bm_pools++;
	pp2->pp2_inst[param->pp2_id]->bm_pools[num_bm_pools] = bm_pool;

	return 0;
}

uint32_t pp2_bm_pool_flush(uintptr_t cpu_slot, uint32_t pool_id)
{
	u32 j;
	u32 resid_bufs = 0;
	u32 pool_bufs;

	resid_bufs += (pp2_reg_read(cpu_slot, MVPP2_BM_POOL_PTRS_NUM_REG(pool_id))
			& MVPP22_BM_POOL_PTRS_NUM_MASK);
	resid_bufs += (pp2_reg_read(cpu_slot, MVPP2_BM_BPPI_PTRS_NUM_REG(pool_id))
			& MVPP2_BM_BPPI_PTR_NUM_MASK);
	if (resid_bufs == 0)
		return 0;

	/* Actual number of registered buffers */
	pool_bufs = pp2_reg_read(cpu_slot, MVPP2_BM_POOL_SIZE_REG(pool_id));
	if (pool_bufs && (resid_bufs + 1) > pool_bufs)
		pr_warn("BM: number of buffers in pool #%d (%d) is more than pool size (%d)\n",
			pool_id, resid_bufs, pool_bufs);

	for (j = 0; j < (resid_bufs + 1); j++) {
		/* Clean all buffers even if return NULL pointer that can be
		 * buffers remained from incorrect previous closure.
		 */
		pp2_bm_hw_buf_get(cpu_slot, pool_id);
	}
	pr_debug("pp2_bm_pool_flush: clear %d buffers from pool ID=%u\n", j, pool_id);
	resid_bufs = 0;
	resid_bufs += (pp2_reg_read(cpu_slot, MVPP2_BM_POOL_PTRS_NUM_REG(pool_id))
			& MVPP22_BM_POOL_PTRS_NUM_MASK);
	resid_bufs += (pp2_reg_read(cpu_slot, MVPP2_BM_BPPI_PTRS_NUM_REG(pool_id))
			& MVPP2_BM_BPPI_PTR_NUM_MASK);

	return resid_bufs;
}

int pp2_bm_pool_destroy(struct pp2_bm_if *bm_if,
			struct pp2_bm_pool *bm_pool)
{
	u32 pool_id;
	u32 resid_bufs = 0;

	pool_id = bm_pool->bm_pool_id;

	pr_debug("BM: destroying pool ID=%u\n", pool_id);

	/* If client did not clean up explicitly before
	 * destroying this pool, then implictly clear up the
	 * BM stack of virtual addresses by allocating
	 * every available buffer from this pool
	 */
	resid_bufs = pp2_bm_pool_flush(bm_if->cpu_slot, pool_id);
	if (resid_bufs) {
		pr_debug("BM: could not clear all buffers from pool ID=%u\n", pool_id);
		pr_debug("BM: total bufs    : %u\n", bm_pool->bm_pool_buf_num);
		pr_debug("BM: residual bufs : %u\n", resid_bufs);
	}

	pp2_bm_hw_pool_destroy(bm_if->cpu_slot, pool_id);

	mv_sys_dma_mem_free((void *)bm_pool->bm_pool_virt_base);

	kfree(bm_pool);

	return 0;
}

uintptr_t pp2_bm_buf_get(struct pp2_bm_if *bm_if, struct pp2_bm_pool *pool)
{
	return pp2_bm_hw_buf_get(bm_if->cpu_slot, pool->bm_pool_id);
}

void pp2_bm_pool_assign(struct pp2_port *port, uint32_t pool_id,
			u32 rxq_id, uint32_t type)
{
	u32 val;
	u32 mask = 0;
	u32 offset = 0;

	if (type == BM_TYPE_LONG_BUF_POOL) {
		mask = MVPP22_RXQ_POOL_LONG_MASK;
		offset = MVPP22_RXQ_POOL_LONG_OFFS;
	} else if (type == BM_TYPE_SHORT_BUF_POOL) {
		mask = MVPP22_RXQ_POOL_SHORT_MASK;
		offset = MVPP22_RXQ_POOL_SHORT_OFFS;
	}

	val = pp2_reg_read(port->cpu_slot, MVPP2_RXQ_CONFIG_REG(rxq_id));
	val &= ~mask;
	val |= ((pool_id << offset) & mask);
	pp2_reg_write(port->cpu_slot, MVPP2_RXQ_CONFIG_REG(rxq_id), val);
}

uint32_t pp2_bm_pool_get_id(struct pp2_bm_pool *pool)
{
	return pool->bm_pool_id;
}

struct pp2_bm_pool *pp2_bm_pool_get_pool_by_id(struct pp2_inst *pp2_inst, uint32_t pool_id)
{
	u32 i;

	for (i = 0; i < pp2_inst->num_bm_pools; i++)
		if (pool_id == pp2_inst->bm_pools[i]->bm_pool_id)
			return pp2_inst->bm_pools[i];

	return NULL;
}

