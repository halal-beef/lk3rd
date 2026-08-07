/*
 * Copyright (c) 2026 Umer Uddin <umer.uddin@mentallysanemainliners.org>
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 *
 */
#pragma once

#define SEC_DEBUG_MAGIC0 0x11221133
#define SEC_DEBUG_MAGIC1 0x12121313

#define SDN_BASE 0x91200000
#define SDN_TOTAL_SIZE 0x01000000

enum sdn_map_id {
    SDN_MAP_DUMP_SUMMARY  = 0,
    SDN_MAP_AUTO_COMMENT  = 1,
    SDN_MAP_EXTRA_INFO    = 2,
    SDN_MAP_AUTO_ANALYSIS = 3,
    SDN_MAP_INITTASK_LOG  = 4,
    SDN_MAP_DEBUG_PARAM   = 5,
    SDN_MAP_FIRST2M_LOG   = 6,
    SDN_MAP_SPARED_BUFFER = 7,
    NR_SDN_MAP            = 8,
};

struct sdn_buf_entry {
    u64 base;
    u64 size;
};

struct sdn_header {
    u32 magic[2];
    u32 version[2];
    struct sdn_buf_entry map[NR_SDN_MAP];
};

struct sdn_map_desc {
    const char *name;
    enum sdn_map_id id;
    u64 size;
    int clear;
};

void sdn_init(void);