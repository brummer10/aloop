/*
 * vs.c
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 rubberplayer
 */


#include "vs.h"

float *const *allocate_desinterleaved_buffer(int channel_count, uint32_t sample_count) {
    float **channels = new float *[channel_count];
    for (int i = 0; i < channel_count; ++i) {
        channels[i] = new float[sample_count];
    }
    return channels;
}
void free_desinterleaved_buffer(const float *const *buffer, int channel_count) {
    if (buffer) {
        float **channels = (float **)buffer;
        for (int i = 0; i < channel_count; ++i) {
            delete[] channels[i];
        }
        delete[] channels;
    }
}
Varispeed::Varispeed() {
    rubberband_input_buffers = allocate_desinterleaved_buffer(MAX_RUBBERBAND_CHANNELS, MAX_RUBBERBAND_BUFFER_FRAMES);
    rubberband_output_buffers = allocate_desinterleaved_buffer(MAX_RUBBERBAND_CHANNELS, MAX_RUBBERBAND_BUFFER_FRAMES);
}
Varispeed::~Varispeed() {
    free_desinterleaved_buffer(rubberband_input_buffers, MAX_RUBBERBAND_CHANNELS);
    free_desinterleaved_buffer(rubberband_output_buffers, MAX_RUBBERBAND_CHANNELS);
}
void Varispeed::initialize(uint32_t sr) {
    RubberBand::RubberBandStretcher::Options rb_options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
        // | RubberBand::RubberBandStretcher::OptionEngineFiner;
    int stereo_channel_count = 2;
    rb = std::make_unique<RubberBand::RubberBandStretcher>(sr, stereo_channel_count, rb_options);
    rb->setMaxProcessSize(MAX_RUBBERBAND_BUFFER_FRAMES);
    rb->process( rubberband_input_buffers,MAX_RUBBERBAND_BUFFER_FRAMES,false);
    rb->reset();
}
