#pragma once
/*
 * vs.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 rubberplayer
 */



#include <rubberband/RubberBandStretcher.h>

#pragma once

#ifndef VS_H
#define VS_H

#define MAX_RUBBERBAND_CHANNELS ((uint32_t)8)
#define MAX_RUBBERBAND_BUFFER_FRAMES ((uint32_t)4096)

class Varispeed {
   public:
    float *const *rubberband_input_buffers;
    float *const *rubberband_output_buffers;
    std::unique_ptr<RubberBand::RubberBandStretcher> rb;

    Varispeed();
    ~Varispeed();
    void initialize(uint32_t sr);
};

// maybe :
//
// use std::unique_ptr<std::unique_ptr<float[]>[]> 
// 
// 
// std::unique_ptr<std::unique_ptr<float[]>[]> allocate_desinterleaved_buffer(int channel_count, uint32_t sample_count) {
//     std::unique_ptr<std::unique_ptr<float[]>[]> channels(new std::unique_ptr<float[]>[channel_count]);
//     for (int i = 0; i < channel_count; ++i) {
//         channels[i] = std::make_unique<float[]>(sample_count);
//     }
//     return channels;
// }
//
// destructor is auto.
//
//
// and use it in RubberBand API with 
// Get raw pointers to pass to the API
// const float *const *inputBuffers = reinterpret_cast<const float *const *>(rubberband_input_buffers.get());
// float *const *outputBuffers = reinterpret_cast<float *const *>(rubberband_output_buffers.get());
//
//

#endif
