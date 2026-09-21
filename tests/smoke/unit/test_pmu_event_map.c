/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Regression test: a symbolic PMU event must program its own hardware event
 * code into PMEVTYPER.
 *
 * Upstream 24.08 resolved the symbolic enum by *indexing* a table generated
 * from the interface header, and its ETHOSU85 enum carried one U55-only entry
 * (CC_STALLED_ON_SHRAM_RECONFIG). Every U85 event after it silently programmed
 * the following hardware event: NPU_ACTIVE counted MAC_ACTIVE, MAC_ACTIVE
 * counted MAC_DPU_ACTIVE, WD_ACTIVE counted WD_STALLED, and the *_DATA_BEAT_*
 * events counted *_TRAN_REQ_STALLED. Nothing failed; the numbers were just
 * wrong. This drives the real upstream PMU TUs against a fake register block
 * so any future bump that reintroduces a mismatch fails here.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ethosu_driver.h"
#include "pmu_ethosu.h"

#if defined(ETHOSU85)
#include "ethosu_interface_u85.h"
#define PMU_DESC ethosu_pmu_desc_u85
#elif defined(ETHOSU65)
#include "ethosu_interface_u65.h"
#define PMU_DESC ethosu_pmu_desc_u65
#else
#include "ethosu_interface_u55.h"
#define PMU_DESC ethosu_pmu_desc_u55
#endif

static struct NPU_REG g_reg;
static int g_failures;

/* The PMU TUs bracket some register accesses with the driver's power hooks,
 * which live in ethosu_driver.c. That TU is deliberately not linked here. */
int ethosu_request_power(struct ethosu_driver *drv) {
    (void)drv;
    return 0;
}

void ethosu_release_power(struct ethosu_driver *drv) {
    (void)drv;
}

static void expect(struct ethosu_driver *drv, const char *name, enum ethosu_pmu_event_type sym, uint32_t hw) {
    drv->dev.reg->PMEVTYPER[0].word = 0xFFFFFFFFu;
    ETHOSU_PMU_Set_EVTYPER(drv, 0, sym);
    const uint32_t got = drv->dev.reg->PMEVTYPER[0].word;
    if (got != hw) {
        printf("FAIL %s: programmed %u, expected %u\n", name, (unsigned)got, (unsigned)hw);
        g_failures++;
    }
    if (ETHOSU_PMU_Get_EVTYPER(drv, 0) != sym) {
        printf("FAIL %s: read-back does not round-trip\n", name);
        g_failures++;
    }
}

int main(void) {
    struct ethosu_driver drv;
    memset(&drv, 0, sizeof(drv));
    memset(&g_reg, 0, sizeof(g_reg));
    drv.dev.reg = &g_reg;
    drv.pmu     = &PMU_DESC;

    /* Literal codes from the Ethos-U TRM for the events every family shares. */
    expect(&drv, "CYCLE", ETHOSU_PMU_CYCLE, 0x11);
    expect(&drv, "NPU_IDLE", ETHOSU_PMU_NPU_IDLE, 0x20);
    expect(&drv, "CC_STALLED_ON_BLOCKDEP", ETHOSU_PMU_CC_STALLED_ON_BLOCKDEP, 0x21);
    expect(&drv, "NPU_ACTIVE", ETHOSU_PMU_NPU_ACTIVE, 0x23);
    expect(&drv, "MAC_ACTIVE", ETHOSU_PMU_MAC_ACTIVE, 0x30);
    expect(&drv, "WD_ACTIVE", ETHOSU_PMU_WD_ACTIVE, 0x50);
    /* And against the interface header for the family-specific bus events. */
    expect(&drv, "MAC_DPU_ACTIVE", ETHOSU_PMU_MAC_DPU_ACTIVE, PMU_EVENT_MAC_DPU_ACTIVE);
    expect(&drv, "WD_STALLED", ETHOSU_PMU_WD_STALLED, PMU_EVENT_WD_STALLED);
#if defined(ETHOSU85)
    expect(&drv, "SRAM_RD_DATA_BEAT_RECEIVED", ETHOSU_PMU_SRAM_RD_DATA_BEAT_RECEIVED, PMU_EVENT_SRAM_RD_DATA_BEAT_RECEIVED);
    expect(&drv, "SRAM_WR_DATA_BEAT_WRITTEN", ETHOSU_PMU_SRAM_WR_DATA_BEAT_WRITTEN, PMU_EVENT_SRAM_WR_DATA_BEAT_WRITTEN);
    expect(&drv, "EXT_RD_DATA_BEAT_RECEIVED", ETHOSU_PMU_EXT_RD_DATA_BEAT_RECEIVED, PMU_EVENT_EXT_RD_DATA_BEAT_RECEIVED);
    expect(&drv, "EXT_WR_DATA_BEAT_WRITTEN", ETHOSU_PMU_EXT_WR_DATA_BEAT_WRITTEN, PMU_EVENT_EXT_WR_DATA_BEAT_WRITTEN);
    expect(&drv, "SRAM_RD_TRAN_REQ_STALLED", ETHOSU_PMU_SRAM_RD_TRAN_REQ_STALLED, PMU_EVENT_SRAM_RD_TRAN_REQ_STALLED);
#else
    expect(&drv, "AXI0_RD_DATA_BEAT_RECEIVED", ETHOSU_PMU_AXI0_RD_DATA_BEAT_RECEIVED, PMU_EVENT_AXI0_RD_DATA_BEAT_RECEIVED);
    expect(&drv, "AXI1_RD_DATA_BEAT_RECEIVED", ETHOSU_PMU_AXI1_RD_DATA_BEAT_RECEIVED, PMU_EVENT_AXI1_RD_DATA_BEAT_RECEIVED);
#endif

    if (g_failures == 0) {
        printf("PASS pmu event map\n");
    }
    return g_failures ? 1 : 0;
}
