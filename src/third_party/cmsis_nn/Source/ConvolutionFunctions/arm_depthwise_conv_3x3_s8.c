/*
 * SPDX-FileCopyrightText: Copyright 2010-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* ----------------------------------------------------------------------
 * Project:      CMSIS NN Library
 * Title:        arm_depthwise_conv_3x3_s8.c
 * Description:  Optimized s8 depthwise convolution function for channel
 *               multiplier of 1 and 3x3 kernel size.
 *
 * $Date:        5 January 2023
 * $Revision:    V.3.2.0
 *
 * Target :  Arm(R) M-Profile Architecture
 *
 * -------------------------------------------------------------------- */

#include "third_party/cmsis_nn/Include/arm_nnfunctions.h"
#include "third_party/cmsis_nn/Include/arm_nnsupportfunctions.h"

/**
 *  @ingroup Public
 */

/**
 * @addtogroup NNConv
 * @{
 */

// Uncomment this to try the experimental dual core support on the RP2040.
#define TF_LITE_PICO_MULTICORE

#ifdef TF_LITE_PICO_MULTICORE

#include "pico/stdlib.h"
#include "pico/multicore.h"

typedef struct {
    const cmsis_nn_dw_conv_params *dw_conv_params;
    const cmsis_nn_per_channel_quant_params *quant_params;
    const cmsis_nn_dims *input_dims;
    const int8_t *input;
    const cmsis_nn_dims *filter_dims;
    const int8_t *kernel;
    const int32_t *bias;
    const cmsis_nn_dims *output_dims;
    int8_t *output;
    int32_t start_y;
    int32_t end_y;
} DepthwiseArgs;

static DepthwiseArgs g_core1_depthwise_args;

#endif  // TF_LITE_PICO_MULTICORE

/*
 * Optimized s8 depthwise convolution function with constraint that
 * in_channel == out_channel and kernel_x == kernel_y == 3 with pads at most 1
 *
 *  Refer prototype header file for details.
 *
 */

void arm_depthwise_conv_3x3_s8_task(const cmsis_nn_dw_conv_params *dw_conv_params,
                                    const cmsis_nn_per_channel_quant_params *quant_params,
                                    const cmsis_nn_dims *input_dims,
                                    const int8_t *input,
                                    const cmsis_nn_dims *filter_dims,
                                    const int8_t *kernel,
                                    const int32_t *bias,
                                    const cmsis_nn_dims *output_dims,
                                    int8_t *output,
                                    const int32_t start_y,
                                    const int32_t end_y)
{
    const int32_t input_x = input_dims->w;
    const int32_t input_y = input_dims->h;
    const int32_t input_ch = input_dims->c;
    const int32_t output_ch = output_dims->c;
    const int32_t pad_x = dw_conv_params->padding.w;
    const int32_t pad_y = dw_conv_params->padding.h;
    const int32_t stride_x = dw_conv_params->stride.w;
    const int32_t stride_y = dw_conv_params->stride.h;
    const int32_t *output_shift = quant_params->shift;
    const int32_t *output_mult = quant_params->multiplier;
    const int32_t output_x = output_dims->w;
    const int32_t output_y = output_dims->h;
    const int32_t output_offset = dw_conv_params->output_offset;
    const int32_t input_offset = dw_conv_params->input_offset;
    const int32_t output_activation_min = dw_conv_params->activation.min;
    const int32_t output_activation_max = dw_conv_params->activation.max;


    const int32_t *bias_base = bias;
    for (int32_t in_h = (start_y * stride_y) - pad_y, out_h = start_y, out_idx = (start_y * output_x * output_ch); out_h < end_y; in_h += stride_y, ++out_h)
    {
        for (int32_t in_w = -pad_x, out_w = 0, ker_h_start = MAX(0, -in_h); out_w < output_x; in_w += stride_x, ++out_w)
        {
            int32_t in_ch = 0;
            int32_t ker_w_start = MAX(0, -in_w);

            bias = bias_base;
            for (; in_ch <= (input_ch - 4); in_ch += 4)
            {
                int32_t out_buff0 = 0;
                int32_t out_buff1 = 0;
                int32_t out_buff2 = 0;
                int32_t out_buff3 = 0;
                if (bias)
                {
                    out_buff0 = *bias++;
                    out_buff1 = *bias++;
                    out_buff2 = *bias++;
                    out_buff3 = *bias++;
                }

                const int8_t *input_ptr = input + (in_h + ker_h_start) * (input_ch * input_x) + in_w * input_ch + in_ch;
                const int8_t *kernel_ptr = kernel + ker_h_start * (input_ch * 3) + in_ch;
#if defined(ARM_MATH_DSP)
                const uint32_t lhs_offset_s16x2 = PKHBT(input_offset, input_offset, 16);

                for (int32_t ker_h = ker_h_start; ker_h < MIN(3, input_y - in_h); ++ker_h)
                {
                    int32_t in_val = 0;
                    int32_t ker_val = 0;
                    int32_t in_val_1 = 0;
                    int32_t ker_val_1 = 0;

                    if (ker_w_start == 0)
                    {
                        in_val = arm_nn_read_s8x4(input_ptr);
                        ker_val = arm_nn_read_s8x4(kernel_ptr);

                        in_val_1 = SXTAB16_RORn(lhs_offset_s16x2, (uint32_t)in_val, 8);
                        ker_val_1 = SXTB16_RORn((uint32_t)ker_val, 8);

                        out_buff1 = SMLABB(in_val_1, ker_val_1, out_buff1);
                        in_val = SXTAB16(lhs_offset_s16x2, (uint32_t)in_val);
                        out_buff3 = SMLATT(in_val_1, ker_val_1, out_buff3);
                        ker_val = SXTB16((uint32_t)ker_val);
                        out_buff0 = SMLABB(in_val, ker_val, out_buff0);
                        out_buff2 = SMLATT(in_val, ker_val, out_buff2);
                    }

                    in_val = arm_nn_read_s8x4(input_ptr + input_ch);
                    ker_val = arm_nn_read_s8x4(kernel_ptr + input_ch);
                    in_val_1 = SXTAB16_RORn(lhs_offset_s16x2, (uint32_t)in_val, 8);
                    ker_val_1 = SXTB16_RORn((uint32_t)ker_val, 8);

                    out_buff1 = SMLABB(in_val_1, ker_val_1, out_buff1);
                    in_val = SXTAB16(lhs_offset_s16x2, (uint32_t)in_val);
                    out_buff3 = SMLATT(in_val_1, ker_val_1, out_buff3);
                    ker_val = SXTB16((uint32_t)ker_val);
                    out_buff0 = SMLABB(in_val, ker_val, out_buff0);
                    out_buff2 = SMLATT(in_val, ker_val, out_buff2);

                    if ((input_x - in_w) >= 3)
                    {
                        in_val = arm_nn_read_s8x4(input_ptr + (input_ch << 1));
                        ker_val = arm_nn_read_s8x4(kernel_ptr + (input_ch << 1));
                        in_val_1 = SXTAB16_RORn(lhs_offset_s16x2, (uint32_t)in_val, 8);
                        ker_val_1 = SXTB16_RORn((uint32_t)ker_val, 8);

                        out_buff1 = SMLABB(in_val_1, ker_val_1, out_buff1);
                        in_val = SXTAB16(lhs_offset_s16x2, (uint32_t)in_val);
                        out_buff3 = SMLATT(in_val_1, ker_val_1, out_buff3);
                        ker_val = SXTB16((uint32_t)ker_val);
                        out_buff0 = SMLABB(in_val, ker_val, out_buff0);
                        out_buff2 = SMLATT(in_val, ker_val, out_buff2);
                    }

                    input_ptr += (input_ch * input_x);
                    kernel_ptr += (input_ch * 3);
                }

#else

                for (int32_t ker_h = ker_h_start; ker_h < MIN(3, input_y - in_h); ++ker_h)
                {
                    int32_t in_val = 0;
                    int32_t ker_val = 0;

                    if (ker_w_start == 0)
                    {
                        in_val = arm_nn_read_s8x4(input_ptr);
                        ker_val = arm_nn_read_s8x4(kernel_ptr);
                        out_buff0 += ((int8_t)in_val + input_offset) * (int8_t)ker_val;
                        out_buff1 += ((int8_t)(in_val >> 8) + input_offset) * (int8_t)(ker_val >> 8);
                        out_buff2 += ((int8_t)(in_val >> 16) + input_offset) * (int8_t)(ker_val >> 16);
                        out_buff3 += ((int8_t)(in_val >> 24) + input_offset) * (int8_t)(ker_val >> 24);
                    }

                    in_val = arm_nn_read_s8x4(input_ptr + input_ch);
                    ker_val = arm_nn_read_s8x4(kernel_ptr + input_ch);

                    out_buff0 += ((int8_t)in_val + input_offset) * (int8_t)ker_val;
                    out_buff1 += ((int8_t)(in_val >> 8) + input_offset) * (int8_t)(ker_val >> 8);
                    out_buff2 += ((int8_t)(in_val >> 16) + input_offset) * (int8_t)(ker_val >> 16);
                    out_buff3 += ((int8_t)(in_val >> 24) + input_offset) * (int8_t)(ker_val >> 24);

                    if ((input_x - in_w) >= 3)
                    {
                        in_val = arm_nn_read_s8x4(input_ptr + (input_ch << 1));
                        ker_val = arm_nn_read_s8x4(kernel_ptr + (input_ch << 1));

                        out_buff0 += ((int8_t)in_val + input_offset) * (int8_t)ker_val;
                        out_buff1 += ((int8_t)(in_val >> 8) + input_offset) * (int8_t)(ker_val >> 8);
                        out_buff2 += ((int8_t)(in_val >> 16) + input_offset) * (int8_t)(ker_val >> 16);
                        out_buff3 += ((int8_t)(in_val >> 24) + input_offset) * (int8_t)(ker_val >> 24);
                    }

                    input_ptr += (input_ch * input_x);
                    kernel_ptr += (input_ch * 3);
                }
#endif

                out_buff0 = arm_nn_requantize(out_buff0, output_mult[in_ch + 0], output_shift[in_ch + 0]);
                out_buff1 = arm_nn_requantize(out_buff1, output_mult[in_ch + 1], output_shift[in_ch + 1]);
                out_buff2 = arm_nn_requantize(out_buff2, output_mult[in_ch + 2], output_shift[in_ch + 2]);
                out_buff3 = arm_nn_requantize(out_buff3, output_mult[in_ch + 3], output_shift[in_ch + 3]);

                out_buff0 += output_offset;
                out_buff1 += output_offset;
                out_buff2 += output_offset;
                out_buff3 += output_offset;

                out_buff0 = MIN(MAX(out_buff0, output_activation_min), output_activation_max);
                out_buff1 = MIN(MAX(out_buff1, output_activation_min), output_activation_max);
                out_buff2 = MIN(MAX(out_buff2, output_activation_min), output_activation_max);
                out_buff3 = MIN(MAX(out_buff3, output_activation_min), output_activation_max);

                output[out_idx++] = (int8_t)out_buff0;
                output[out_idx++] = (int8_t)out_buff1;
                output[out_idx++] = (int8_t)out_buff2;
                output[out_idx++] = (int8_t)out_buff3;
            }

            // Leftover
            for (; in_ch < input_ch; ++in_ch)
            {
                int32_t out_buff = 0;
                if (bias)
                {
                    out_buff = *bias++;
                }

                const int8_t *input_ptr = input + (in_h + ker_h_start) * (input_ch * input_x) + in_w * input_ch + in_ch;
                const int8_t *kernel_ptr = kernel + ker_h_start * (input_ch * 3) + in_ch;

                for (int32_t ker_h = ker_h_start; ker_h < MIN(3, input_y - in_h); ++ker_h)
                {
                    if (ker_w_start == 0)
                    {
                        out_buff += (*(input_ptr) + input_offset) * *(kernel_ptr);
                    }

                    out_buff += (*(input_ptr + input_ch) + input_offset) * *(kernel_ptr + input_ch);

                    if ((input_x - in_w) >= 3)
                    {
                        out_buff += (*(input_ptr + (input_ch << 1)) + input_offset) * *(kernel_ptr + (input_ch << 1));
                    }

                    input_ptr += (input_ch * input_x);
                    kernel_ptr += (input_ch * 3);
                }

                out_buff = arm_nn_requantize(out_buff, output_mult[in_ch], output_shift[in_ch]);
                out_buff += output_offset;
                out_buff = MIN(MAX(out_buff, output_activation_min), output_activation_max);
                output[out_idx++] = (int8_t)out_buff;
            }
        }
    }
}

#ifdef TF_LITE_PICO_MULTICORE
static void depthwise_task(DepthwiseArgs* args) {
    arm_depthwise_conv_3x3_s8_task(args->dw_conv_params,
                                   args->quant_params,
                                   args->input_dims,
                                   args->input,
                                   args->filter_dims,
                                   args->kernel,
                                   args->bias,
                                   args->output_dims,
                                   args->output,
                                   args->start_y,
                                   args->end_y);
}

static void core1_depthwise_worker(void) {
    depthwise_task(&g_core1_depthwise_args);

    // Signal we're done by pushing a result of zero.
    multicore_fifo_push_blocking(ARM_CMSIS_NN_SUCCESS);
}
#endif  // TF_LITE_PICO_MULTICORE

arm_cmsis_nn_status arm_depthwise_conv_3x3_s8(const cmsis_nn_context *ctx,
                                              const cmsis_nn_dw_conv_params *dw_conv_params,
                                              const cmsis_nn_per_channel_quant_params *quant_params,
                                              const cmsis_nn_dims *input_dims,
                                              const int8_t *input,
                                              const cmsis_nn_dims *filter_dims,
                                              const int8_t *kernel,
                                              const cmsis_nn_dims *bias_dims,
                                              const int32_t *bias,
                                              const cmsis_nn_dims *output_dims,
                                              int8_t *output)
{
    (void)ctx;
    (void)bias_dims;

    const int32_t input_ch = input_dims->c;
    const int32_t output_ch = output_dims->c;
    const int32_t pad_x = dw_conv_params->padding.w;
    const int32_t output_y = output_dims->h;
    const int32_t stride_y = dw_conv_params->stride.h;

    /* Check input constraints input_ch == output_ch */
    if (input_ch != output_ch)
    {
        return ARM_CMSIS_NN_ARG_ERROR;
    }
    /* Check input constraints pad_x <= 1 */
    if (pad_x > 1 || filter_dims->w != 3 || filter_dims->h != 3)
    {
        return ARM_CMSIS_NN_ARG_ERROR;
    }

#ifdef TF_LITE_PICO_MULTICORE

    const int mid_y = (((output_y / stride_y) / 2) * stride_y);

    DepthwiseArgs shared_args;
    shared_args.dw_conv_params = dw_conv_params;
    shared_args.quant_params = quant_params;
    shared_args.input_dims = input_dims;
    shared_args.input = input;
    shared_args.filter_dims = filter_dims;
    shared_args.kernel = kernel;
    shared_args.bias = bias;
    shared_args.output_dims = output_dims;
    shared_args.output = output;

    DepthwiseArgs core0_args = shared_args;
    core0_args.start_y = 0;
    core0_args.end_y = mid_y;

    DepthwiseArgs core1_args = shared_args;
    core1_args.start_y = mid_y;
    core1_args.end_y = output_y;

    // Start the second core working.
    g_core1_depthwise_args = core1_args;
    multicore_reset_core1();
    multicore_launch_core1(core1_depthwise_worker);

    // Do the processing on the first core.
    depthwise_task(&core0_args);

    // A result of ARM_CMSIS_NN_SUCCESS means success. Blocks until core 1 is
    // done.
    const uint32_t core1_result = multicore_fifo_pop_blocking();
    if (core1_result != ARM_CMSIS_NN_SUCCESS) {
        return core1_result;
    }

#else  // TF_LITE_PICO_MULTICORE

    arm_depthwise_conv_3x3_s8_task(dw_conv_params,
                                   quant_params,
                                   input_dims,
                                   input,
                                   filter_dims,
                                   kernel,
                                   bias,
                                   output_dims,
                                   output,
                                   0,
                                   output_y);

#endif // TF_LITE_PICO_MULTICORE

    /* Return to application */
    return ARM_CMSIS_NN_SUCCESS;
}

/**
 * @} end of NNConv group
 */
