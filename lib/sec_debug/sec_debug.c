/*
 * Copyright (c) 2026 Umer Uddin <umer.uddin@mentallysanemainliners.org>
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 *
 */

#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>

#include <lib/sec_debug.h>

static const struct sdn_map_desc sdn_layout[NR_SDN_MAP] =
{
    { "summary.html", SDN_MAP_DUMP_SUMMARY, 0x200000, 1 },
    { "auto_comment", SDN_MAP_AUTO_COMMENT, 0x000f3c, 1 },
    { "extra_info", SDN_MAP_EXTRA_INFO, 0x010000, 0 },
    { "auto_analysis", SDN_MAP_AUTO_ANALYSIS, 0x100000, 1 },
    { "init_task_log", SDN_MAP_INITTASK_LOG, 0x020000, 0 },
    { "debug_history", SDN_MAP_DEBUG_PARAM, 0x100000, 1 },
    { "first2m_kmsg", SDN_MAP_FIRST2M_LOG, 0x200000, 0 },
    { "boot_reset", SDN_MAP_SPARED_BUFFER, 0x100000, 1 },
};

void sdn_init(void)
{
    struct sdn_header *hdr = (struct sdn_header *)(uintptr_t)SDN_BASE;
    
    hdr->magic[0] = SEC_DEBUG_MAGIC0;
    hdr->magic[1] = SEC_DEBUG_MAGIC1;
    hdr->version[0] = 0x7A1A1B3F;
    hdr->version[1] = 0;

    u64 cur = ROUNDUP(SDN_BASE + 8, 0x100);

    for (int i = 0; i < NR_SDN_MAP; i++) {
        const struct sdn_map_desc *d = &sdn_layout[i];
        u64 sz = d->size;

        hdr->map[d->id].base = cur;
        hdr->map[d->id].size = sz;

        if (d->clear)
            memset((void *)(uintptr_t)cur, 0, sz);

        cur = ROUNDUP(cur + sz, 0x100);
    }
}