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

#include <string.h>
#include <stdio.h>

#include "mv_std.h"
#include "lib/lib_misc.h"
#include "env/mv_sys_dma.h"

#include "mvapp.h"
#include "mv_pp2.h"
#include "mv_pp2_bpool.h"
#include "utils.h"

#define MVAPPS_MAX_BURST_SIZE 256

u64 sys_dma_high_addr;

static u16 used_bpools[MVAPPS_MAX_PKT_PROC] = {MVAPPS_PP2_BPOOLS_RSRV, MVAPPS_PP2_BPOOLS_RSRV};
static u16 used_hifs = MVAPPS_PP2_HIFS_RSRV;

static u64 buf_alloc_cnt;
static u64 hw_rxq_buf_free_cnt;
static u64 hw_bm_buf_free_cnt;
static u64 buf_free_cnt;
static u64 hw_buf_free_cnt;
static u64 tx_shadow_q_buf_free_cnt[MVAPPS_MAX_NUM_CORES];

/*
 * app_get_line()
 * get input from stdin into buffer and according to it
 * create argc and argv, which need while calling for getopt
 */
int app_get_line(char *prmpt, char *buff, size_t sz, int *argc, char *argv[])
{
	int ch, extra;
	char *p2;

	/* because getopt starting parsing from argument = 1 we are skipping argument zero */
	*argc = 1;

	/* Get line with buffer overrun protection */
	if (prmpt) {
		printf("%s", prmpt);
		fflush(stdout);
	}
	if (!fgets(buff, sz, stdin))
		return -EINVAL;

	/*
	 * if it was too long, there'll be no newline. In that case, we flush
	 * to end of line so that excess doesn't affect the next call.
	 */
	if (buff[strlen(buff) - 1] != '\n') {
		extra = 0;
		while (((ch = getchar()) != '\n') && (ch != EOF))
		extra = 1;
		return (extra == 1) ? -EFAULT : 0;
	}

	/* otherwise remove newline and give string back to caller */
	buff[strlen(buff) - 1] = '\0';

	p2 = strtok(buff, " ");
	while (p2 && *argc < sz - 1) {
		argv[(*argc)++] = p2;
		p2 = strtok(NULL, " ");
	}
	argv[*argc] = NULL;

	return 0;
}

static int find_free_bpool(u32 pp_id)
{
	int i;

	for (i = 0; i < MVAPPS_PP2_TOTAL_NUM_BPOOLS; i++) {
		if (!((1 << i) & used_bpools[pp_id])) {
			used_bpools[pp_id] |= (1 << i);
			break;
		}
	}
	if (i == MVAPPS_PP2_TOTAL_NUM_BPOOLS) {
		pr_err("no free BPool found!\n");
		return -ENOSPC;
	}
	return i;
}

static int find_free_hif(void)
{
	int i;

	for (i = 0; i < MVAPPS_PP2_TOTAL_NUM_HIFS; i++) {
		if (!((1 << i) & used_hifs)) {
			used_hifs |= (1 << i);
			break;
		}
	}
	if (i == MVAPPS_PP2_TOTAL_NUM_HIFS) {
		pr_err("no free HIF found!\n");
		return -ENOSPC;
	}

	return i;
}

int app_hif_init(struct pp2_hif **hif, u32 queue_size)
{
	int hif_id;
	char name[15];
	struct pp2_hif_params hif_params;
	int err;

	hif_id = find_free_hif();
	if (hif_id < 0) {
		pr_err("free HIF not found!\n");
		return hif_id;
	}

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "hif-%d", hif_id);
	pr_debug("found hif: %s\n", name);
	memset(&hif_params, 0, sizeof(hif_params));
	hif_params.match = name;
	hif_params.out_size = queue_size;
	err = pp2_hif_init(&hif_params, hif);
	if (err)
		return err;
	if (!hif) {
		pr_err("HIF init failed!\n");
		return -EIO;
	}

	return 0;
}

int app_build_all_bpools(struct bpool_desc ***ppools, int num_pools, struct bpool_inf infs[], struct pp2_hif *hif)
{
	struct pp2_bpool_params		bpool_params;
	int				i, j, k, err, pool_id;
	char				name[15];
	struct bpool_desc		**pools = NULL;
	struct pp2_buff_inf		*buffs_inf = NULL;
	u8  pp2_num_inst = pp2_get_num_inst();

	buf_alloc_cnt = 0;

	if (num_pools > MVAPPS_PP2_MAX_NUM_BPOOLS) {
		pr_err("only %d pools allowed!\n", MVAPPS_PP2_MAX_NUM_BPOOLS);
		return -EINVAL;
	}
/* TODO: release memory on error */
	pools = (struct bpool_desc **)malloc(pp2_num_inst * sizeof(struct bpool_desc *));
	if (!pools) {
		pr_err("no mem for bpool_desc array!\n");
		return -ENOMEM;
	}

	*ppools = pools;

	for (i = 0; i < pp2_num_inst; i++) {
		pools[i] = (struct bpool_desc *)malloc(num_pools * sizeof(struct bpool_desc));
		if (!pools[i]) {
			pr_err("no mem for bpool_desc array!\n");
			return -ENOMEM;
		}

		for (j = 0; j < num_pools; j++) {
			pool_id = find_free_bpool(i);
			if (pool_id < 0) {
				pr_err("free bpool not found!\n");
				return pool_id;
			}
			memset(name, 0, sizeof(name));
			snprintf(name, sizeof(name), "pool-%d:%d", i, pool_id);
			memset(&bpool_params, 0, sizeof(bpool_params));
			bpool_params.match = name;
			bpool_params.buff_len = infs[j].buff_size;

			pr_info("%s: buff_size %d, num_buffs %d\n", name, infs[j].buff_size, infs[j].num_buffs);
			err = pp2_bpool_init(&bpool_params, &pools[i][j].pool);
			if (err)
				return err;

			if (!pools[i][j].pool) {
				pr_err("BPool id%d init failed!\n", pool_id);
				return -EIO;
			}

			pools[i][j].buffs_inf =
				(struct pp2_buff_inf *)malloc(infs[j].num_buffs * sizeof(struct pp2_buff_inf));

			if (!pools[i][j].buffs_inf) {
				pr_err("no mem for bpools-inf array!\n");
				return -ENOMEM;
			}

			buffs_inf = pools[i][j].buffs_inf;
			pools[i][j].num_buffs = infs[j].num_buffs;

			for (k = 0; k < infs[j].num_buffs; k++) {
				void *buff_virt_addr;

				buff_virt_addr = mv_sys_dma_mem_alloc(infs[j].buff_size, 4);
				if (!buff_virt_addr) {
					pr_err("failed to allocate mem (%d)!\n", k);
					return -1;
				}
				if (k == 0) {
					sys_dma_high_addr = ((u64)buff_virt_addr) & (~((1ULL << 32) - 1));
					pr_debug("sys_dma_high_addr (0x%lx)\n", sys_dma_high_addr);
				}
				if ((upper_32_bits((u64)buff_virt_addr)) != (sys_dma_high_addr >> 32)) {
					pr_err("buff_virt_addr(%p)  upper out of range; skipping this buff\n",
					       buff_virt_addr);
					continue;
				}
				buffs_inf[k].addr = (bpool_dma_addr_t)mv_sys_dma_mem_virt2phys(buff_virt_addr);
				/* cookie contains lower_32_bits of the va */
				buffs_inf[k].cookie = lower_32_bits((u64)buff_virt_addr);
			}

			for (k = 0; k < infs[j].num_buffs; k++) {
				struct pp2_buff_inf	tmp_buff_inf;

				tmp_buff_inf.cookie = buffs_inf[k].cookie;
				tmp_buff_inf.addr   = buffs_inf[k].addr;
				err = pp2_bpool_put_buff(hif, pools[i][j].pool, &tmp_buff_inf);
				if (err)
					return err;
				buf_alloc_cnt++;
			}
		}
	}
	return 0;
}

int app_find_port_info(struct port_desc *port_desc)
{
	char		 name[20];
	u8		 pp, ppio;
	int		 err;

	if (!port_desc->name) {
		pr_err("No port name given!\n");
		return -1;
	}

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "%s", port_desc->name);
	err = pp2_netdev_get_ppio_info(name, &pp, &ppio);
	if (err) {
		pr_err("PP2 Port %s not found!\n", port_desc->name);
		return err;
	}

	port_desc->ppio_id = ppio;
	port_desc->pp_id = pp;

	return 0;
}

int app_port_init(struct port_desc *port, int num_pools, struct bpool_desc *pools, u16 mtu)
{
	struct pp2_ppio_params		*port_params = &port->port_params;
	struct pp2_ppio_inq_params	inq_params;
	char				name[MVAPPS_PPIO_NAME_MAX];
	int				i, j, err = 0;
	u16				curr_mtu;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "ppio-%d:%d",
		 port->pp_id, port->ppio_id);
	pr_debug("found port: %s\n", name);
	port_params->match = name;
	port_params->type = port->ppio_type;
	port_params->inqs_params.num_tcs = port->num_tcs;
	port_params->specific_type_params.log_port_params.first_inq = port->first_inq;
	for (i = 0; i < port->num_tcs; i++) {
		port_params->inqs_params.tcs_params[i].pkt_offset = MVAPPS_PKT_OFFS >> 2;
		port_params->inqs_params.tcs_params[i].num_in_qs = port->num_inqs;
		inq_params.size = port->inq_size;
		port_params->inqs_params.tcs_params[i].inqs_params = &inq_params;
		for (j = 0; j < num_pools; j++)
			port_params->inqs_params.tcs_params[i].pools[j] = pools[j].pool;
	}
	port_params->outqs_params.num_outqs = port->num_outqs;
	for (i = 0; i < port->num_outqs; i++) {
		port_params->outqs_params.outqs_params[i].size = port->outq_size;
		port_params->outqs_params.outqs_params[i].weight = 1;
	}

	err = pp2_ppio_init(port_params, &port->ppio);
	if (err) {
		pr_err("PP-IO init failed (error: %d)!\n", err);
		return err;
	}

	if (!port->ppio) {
		pr_err("PP-IO init failed!\n");
		return -EIO;
	}

	if (mtu) {
		/* Change port MTU if needed */
		pp2_ppio_get_mtu(port->ppio, &curr_mtu);
		if (curr_mtu != mtu) {
			pp2_ppio_set_mtu(port->ppio, mtu);
			pp2_ppio_set_mru(port->ppio, MVAPPS_MTU_TO_MRU(mtu));
			pr_info("Set port ppio-%d:%d MTU to %d\n",
				port->pp_id, port->ppio_id, mtu);
		}
	}

	err = pp2_ppio_enable(port->ppio);

	return err;
}

void app_port_local_init(int id, int lcl_id, struct lcl_port_desc *lcl_port, struct port_desc *port)
{
	int i;

	lcl_port->id		= id;
	lcl_port->lcl_id	= lcl_id;
	lcl_port->pp_id		= port->pp_id;
	lcl_port->ppio_id	= port->ppio_id;
	lcl_port->ppio		= port->ppio;

	lcl_port->num_shadow_qs = port->num_outqs;
	lcl_port->shadow_q_size	= port->outq_size;
	lcl_port->shadow_qs = (struct tx_shadow_q *)malloc(port->num_outqs * sizeof(struct tx_shadow_q));

	for (i = 0; i < lcl_port->num_shadow_qs; i++) {
		lcl_port->shadow_qs[i].read_ind = 0;
		lcl_port->shadow_qs[i].write_ind = 0;

		lcl_port->shadow_qs[i].ents =
			(struct tx_shadow_q_entry *)malloc(port->outq_size * sizeof(struct tx_shadow_q_entry));
	}
}

void app_port_local_deinit(struct lcl_port_desc *lcl_port)
{
	int i, cnt = 0;

	for (i = 0; i < lcl_port->num_shadow_qs; i++) {
		struct tx_shadow_q *shadow_q = &lcl_port->shadow_qs[i];

		if (shadow_q->read_ind > shadow_q->write_ind) {
			cnt = lcl_port->shadow_q_size - shadow_q->read_ind + shadow_q->write_ind;
			tx_shadow_q_buf_free_cnt[lcl_port->lcl_id] += cnt;
		} else {
			cnt = shadow_q->write_ind - shadow_q->read_ind;
			tx_shadow_q_buf_free_cnt[lcl_port->lcl_id] += cnt;
		}
		pr_debug("Release %d buffers from shadow_q-%d port:%d:%d\n",
			 cnt, i, lcl_port->pp_id, lcl_port->ppio_id);
		shadow_q->read_ind = 0;
		shadow_q->write_ind = 0;
		free(shadow_q->ents);
	}

	free(lcl_port->shadow_qs);
}

static void free_rx_queues(struct pp2_ppio *port, u16 num_tcs, u16 num_inqs)
{
	struct pp2_ppio_desc	descs[MVAPPS_MAX_BURST_SIZE];
	u8			tc = 0, qid = 0;
	u16			num;

	for (tc = 0; tc < num_tcs; tc++) {
		for (qid = 0; qid < num_inqs; qid++) {
			num = MVAPPS_MAX_BURST_SIZE;
			while (num) {
				pp2_ppio_recv(port, tc, qid, descs, &num);
				hw_rxq_buf_free_cnt += num;
			}
		}
	}
}

void app_disable_all_ports(struct port_desc *ports, int num_ports, u16 num_tcs, u16 num_inqs)
{
	int i;

	for (i = 0;  i < num_ports; i++) {
		if (ports[i].ppio) {
			pp2_ppio_disable(ports[i].ppio);
			free_rx_queues(ports[i].ppio, num_tcs, num_inqs);
		}
	}
}

void app_deinit_all_ports(struct port_desc *ports, int num_ports)
{
	int i;

	for (i = 0;  i < num_ports; i++) {
		if (ports[i].ppio)
			pp2_ppio_deinit(ports[i].ppio);
	}

	/* Calculate number of buffers released from PP2 */
	for (i = 0; i < MVAPPS_MAX_NUM_CORES; i++)
		hw_buf_free_cnt += tx_shadow_q_buf_free_cnt[i];
	hw_buf_free_cnt += hw_bm_buf_free_cnt + hw_rxq_buf_free_cnt;

	if (buf_free_cnt != buf_alloc_cnt)
		pr_err("Not all buffers were released: allocated: %lu, freed: %lu\n",
		       buf_alloc_cnt, buf_free_cnt);

	if (buf_free_cnt != hw_buf_free_cnt) {
		pr_err("Error in buffer release: allocated: %lu, app freed: %lu!!!\n",
		       buf_alloc_cnt, buf_free_cnt);

		pr_err("pp2 freed: %lu bm free: %lu, rxq free: %lu, tx free: %lu !\n",
		       hw_buf_free_cnt, hw_bm_buf_free_cnt, hw_rxq_buf_free_cnt,
			(hw_buf_free_cnt - hw_bm_buf_free_cnt - hw_rxq_buf_free_cnt));
	}

	pr_debug("allocated: %lu, app freed: %lu, pp2 freed: %lu bm free: %lu, rxq free: %lu, tx free: %lu !\n",
		 buf_alloc_cnt, buf_free_cnt, hw_buf_free_cnt, hw_bm_buf_free_cnt, hw_rxq_buf_free_cnt,
		(hw_buf_free_cnt - hw_bm_buf_free_cnt - hw_rxq_buf_free_cnt));
}

static void flush_pool(struct pp2_bpool *bpool, struct pp2_hif *hif)
{
	u32 i, buf_num, cnt = 0, err = 0;

	pp2_bpool_get_num_buffs(bpool, &buf_num);
	for (i = 0; i < buf_num; i++) {
		struct pp2_buff_inf buff;

		err = 0;
		while (pp2_bpool_get_buff(hif, bpool, &buff)) {
			err++;
			if (err == 10000) {
				buff.cookie = 0;
				break;
			}
		}

		if (err) {
			if (err == 10000) {
				pr_err("flush_pool: p2_id=%d, pool_id=%d: Got NULL buf (%d of %d)\n",
				       bpool->pp2_id, bpool->id, i, buf_num);
				continue;
			}
			pr_warn("flush_pool: p2_id=%d, pool_id=%d: Got buf (%d of %d) after %d retries\n",
				bpool->pp2_id, bpool->id, i, buf_num, err);
		}
		cnt++;
	}
	hw_bm_buf_free_cnt += cnt;
	pp2_bpool_deinit(bpool);
}

static void free_pool_buffers(struct pp2_buff_inf *buffs, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		void *buff_virt_addr = (char *)(((uintptr_t)(buffs[i].cookie)) | sys_dma_high_addr);

		mv_sys_dma_mem_free(buff_virt_addr);
		buf_free_cnt++;
	}
}

void app_free_all_pools(struct bpool_desc **pools, int num_pools, struct pp2_hif *hif)
{
	int i, j;
	u8  pp2_num_inst = pp2_get_num_inst();

	if (pools) {
		for (i = 0; i < pp2_num_inst; i++) {
			if (pools[i]) {
				for (j = 0; j < num_pools; j++)
					if (pools[i][j].pool) {
						flush_pool(pools[i][j].pool, hif);
						free_pool_buffers(pools[i][j].buffs_inf, pools[i][j].num_buffs);
						free(pools[i][j].buffs_inf);
					}
				free(pools[i]);
			}
		}
		free(pools);
	}
}

