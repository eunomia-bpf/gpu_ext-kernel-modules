/* SPDX-License-Identifier: MIT */
#ifndef NV_GPU_RPC_DIAGNOSTIC_H
#define NV_GPU_RPC_DIAGNOSTIC_H
#include "nvtypes.h"

/* Read-only observation emitted only AFTER a real GSP RPC wait. No policy
 * decision or driver state is carried back from this hook. */
struct nv_gpu_gsp_control_complete_ctx {
    NvU64 input_value;
    NvU32 hClient;
    NvU32 hObject;
    NvU32 command;
    NvU32 input_size;
    NvU32 wire_size;
    NvU32 input_valid;
    NvU32 transport_status;
    NvU32 gsp_status;
    NvU32 gsp_status_valid;
    NvU32 reserved;
};

static inline void nv_gpu_gsp_observe_status(
    struct nv_gpu_gsp_control_complete_ctx *observation,
    NvU32 transportStatus, NvU32 gspStatus)
{
    observation->transport_status = transportStatus;
    observation->gsp_status_valid = transportStatus == 0;
    observation->gsp_status = transportStatus == 0 ? gspStatus : ~(NvU32)0;
}
#endif
