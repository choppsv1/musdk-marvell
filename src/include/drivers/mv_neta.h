/******************************************************************************
 *	Copyright (C) 2017 Marvell International Ltd.
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

#ifndef __MV_NETA_H__
#define __MV_NETA_H__

#include "mv_std.h"


#define NETA_NUM_ETH_PPIO	4 /**< Maximum number of io instances in each packet processor */

/** @addtogroup grp_neta_init Packet Processor: Initialization
 *
 *  Packet Processor Initialization API documentation
 *
 *  @{
 */

/**
 * neta init parameters
 *
 */
struct neta_init_params {
	/** Bitmap of reserved bm_pools (0-3). */
	u16			bm_pool_reserved_map;
};


/**
 * Initialize the global NETA
 *
 * @param[in]	params	A pointer to structure that contains all relevant parameters.
 *
 * @retval	A pointer to a DMA memory on success
 * @retval	<0 on failure
 */
int neta_init(struct neta_init_params *params);

/**
 * Destroy the global NETA
 *
 */
void neta_deinit(void);

int neta_netdev_get_port_info(char *ifname, u8 *port_id);

/** @} */ /* end of grp_neta_init */

#endif /* __MV_NETA_H__ */