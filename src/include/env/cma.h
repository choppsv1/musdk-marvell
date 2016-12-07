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
 * @file cma.h
 *
 * Contiguous memory allocator
 *
 */

#ifndef __CMA_H__
#define __CMA_H__

#include <stdint.h>


/**
 * Initialization routine for pp contiguous memory allocator
 *
 * @retval 0 Success
 * @retval non-0 Failure.
 *
 */
int cma_init(void);

/**
 * De-initialization routine for pp contiguous memory allocator
 *
 */
void cma_deinit(void);

/**
 * Allocates physical zeroed contiguous memory
 *
 * @param    size            memory size
 *
 * @retval Handle to the allocated memory Success
 * @retval NULL Failure.
 *
 */
uintptr_t cma_calloc(size_t size);

/**
 * Free physical memory
 *
 * @param    buf             Handle to buff
 *
 */
void cma_free(uintptr_t buf);

/**
 * Get virtual address from pp CMA
 *
 * @param    buf             pointer to buff
 *
 * @retval virtual address  Success
 * @retval NULL Failure.
 *
 */
uintptr_t cma_get_vaddr(uintptr_t buf);

/**
 * Get physical address from pp CMA
 *
 * @param    buf             pointer to buff
 *
 * @retval physical address  Success
 * @retval NULL Failure.
 *
 */
uintptr_t cma_get_paddr(uintptr_t buf);

#endif /* __CMA_H__ */
