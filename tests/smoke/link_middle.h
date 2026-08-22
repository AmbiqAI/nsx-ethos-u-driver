/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interface of the middle library used by the three-level override acceptance
 * (see tests/smoke/CMakeLists.txt and link_middle.c).
 */

#ifndef NSX_ETHOS_U_SMOKE_LINK_MIDDLE_H
#define NSX_ETHOS_U_SMOKE_LINK_MIDDLE_H

#include <stdint.h>

/** Bring up the NPU through the driver. Defined in link_middle.c. */
int nsx_smoke_middle_init(void *npu_base, uint32_t irq_num);

#endif /* NSX_ETHOS_U_SMOKE_LINK_MIDDLE_H */
