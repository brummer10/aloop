/*
 * xpa.c
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */


#include <cmath>
#include <vector>
#include <signal.h>
#include <cstdio>
#include <algorithm>
#include <unistd.h>
#include <iostream>
#include <string>
#include <condition_variable>

#include "ParallelThread.h"
#include "xui.h"
#include "xpa.h"

AudioLooperUi ui;


#define MAX_RUBBERBAND_CHANNELS ((uint32_t)8)
#define MAX_RUBBERBAND_BUFFER_FRAMES ((uint32_t)4096)

uint32_t position;

float *const * rubberband_input_buffers;
float *const * rubberband_output_buffers;

float *const * allocate_desinterleaved_buffer(int channel_count, uint32_t sample_count){
    float **channels = new float *[channel_count];
    for (int i = 0; i < channel_count; ++i) {
        channels[i] = new float[sample_count];        
    }
    return channels;
}
void free_desinterleaved_buffer(const float *const *buffer, int channel_count) {
    float **channels = (float **)buffer;
   for (int i = 0; i < channel_count; ++i) {
        delete[] channels[i];
    }
    delete[] channels;
}

void allocate_rubberband_buffers(){
    rubberband_input_buffers = /*(const float *const *)*/allocate_desinterleaved_buffer(MAX_RUBBERBAND_CHANNELS,MAX_RUBBERBAND_BUFFER_FRAMES);
    rubberband_output_buffers = allocate_desinterleaved_buffer(MAX_RUBBERBAND_CHANNELS,MAX_RUBBERBAND_BUFFER_FRAMES);
}

void free_rubberband_buffers(){
    free_desinterleaved_buffer(rubberband_input_buffers,MAX_RUBBERBAND_CHANNELS);
    free_desinterleaved_buffer(rubberband_output_buffers,MAX_RUBBERBAND_CHANNELS);
 
}

// the portaudio server process callback
static int process(const void* inputBuffer, void* outputBuffer,
    unsigned long frames, const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void* data) {

    float* out = static_cast<float*>(outputBuffer);
    static std::condition_variable *Sync = static_cast<std::condition_variable*>(data);
    static float fRec0[2] = {0};
    static float ramp = 0.0;
    static const float ramp_step = 256.0;
    (void) timeInfo;
    (void) statusFlags;

    // TODO : put in init.
    ui.rb->setMaxProcessSize(MAX_RUBBERBAND_BUFFER_FRAMES) ;   
    ui.rb->setTimeRatio(ui.timeRatio);
    ui.rb->setPitchScale(ui.pitchScale);

    
    if (( ui.af.samplesize && ui.af.samples != nullptr) && ui.play && ui.ready) {
        float fSlow0 = 0.0010000000000000009 * ui.gain;
        uint32_t needed = frames;
        while (needed>0){
            size_t available = ui.rb->available();
            if (available > 0){
                size_t retrived_frames_count = ui.rb->retrieve(rubberband_output_buffers,min(available,min(needed,MAX_RUBBERBAND_BUFFER_FRAMES)));
                for (size_t i = 0 ; i < retrived_frames_count ;i++){
                    fRec0[0] = fSlow0 + 0.999 * fRec0[1];
                    for (uint32_t c = 0 ; c < min(ui.af.channels,MAX_RUBBERBAND_CHANNELS) ;c++){
                        *out++ = rubberband_output_buffers[c][i] * fRec0[0];
                    }
                    fRec0[1] = fRec0[0];
                }
                needed -= retrived_frames_count;
            }
            if (needed>0){            
                int process_samples;
                if (ui.playBackwards){
                    if (ui.position <= ui.loopPoint_l) {
                        ui.position = ui.loopPoint_r;
                    }    
                    process_samples = min(ui.position - ui.loopPoint_l,MAX_RUBBERBAND_BUFFER_FRAMES);
                    for (int i = 0 ; i < process_samples ;i++){
                        for (uint32_t c = 0 ; c < min(ui.af.channels,MAX_RUBBERBAND_CHANNELS) ;c++){
                            rubberband_input_buffers[c][i] = ui.af.samples[ (ui.position - i) * ui.af.channels + c];
                        }
                    }
                    ui.position -= process_samples;
                } else {
                    if (ui.position >= ui.loopPoint_r) {
                        ui.position = ui.loopPoint_l;  
                        // TODO why is loadFile used at this point in original audio callback ????
                        // ui.loadFile(); 
                    }
                   // could estimate what is needed instead of passing the whole buffer in worst case
                    process_samples = min(ui.loopPoint_r-ui.position,MAX_RUBBERBAND_BUFFER_FRAMES);
                    for (int i = 0 ; i < process_samples ;i++){
                        for (uint32_t c = 0 ; c < min(ui.af.channels,MAX_RUBBERBAND_CHANNELS) ;c++){
                            rubberband_input_buffers[c][i] = ui.af.samples[ (ui.position + i) * ui.af.channels + c];
                        }
                    }
                    ui.position += process_samples;
                }            
                ui.rb->process( rubberband_input_buffers,process_samples,false);
                
            }
        }
    } else {
            ui.rb->reset();
            memset(out, 0.0, (uint32_t)frames * 2 * sizeof(float));
    }
    

/*
    while (rb.available < frames){

        process 	( 	const float *const * 	input,
            size_t 	samples,
            bool 	final )

    }

    size_t samples_requiered = rubberBandStretcher->getSamplesRequired();

*/

    //if (( ui.af.samplesize && ui.af.samples != nullptr) && ui.play && ui.ready) {
    //    float fSlow0 = 0.0010000000000000009 * ui.gain;
    //    for (uint32_t i = 0; i<(uint32_t)frames; i++) {
    //        fRec0[0] = fSlow0 + 0.999 * fRec0[1];
    //        for (uint32_t c = 0; c < ui.af.channels; c++) {
    //            if (!c) {
    //                *out++ = ui.af.samples[ui.position*ui.af.channels] * fRec0[0];
    //                if (ui.af.channels ==1) *out++ = ui.af.samples[ui.position*ui.af.channels] * fRec0[0];
    //            } else *out++ = ui.af.samples[ui.position*ui.af.channels+c] * fRec0[0];
    //        }
    //        fRec0[1] = fRec0[0];
    //        // track play-head position
    //        ui.playBackwards ? ui.position-- : ui.position++;
    //        if (ui.position > ui.loopPoint_r) {
    //            ui.position = ui.loopPoint_l;
    //            ui.loadFile();
    //        } else if (ui.position <= ui.loopPoint_l) {
    //            ui.position = ui.loopPoint_r;
    //            ui.loadFile();
    //        // ramp up on loop start point
    //        } else if (ui.playBackwards ?
    //                    ui.position > ui.loopPoint_r - ramp_step :
    //                    ui.position < ui.loopPoint_l + ramp_step) {
    //            if (ramp < ramp_step) {
    //                ++ramp;
    //            }
    //            const float fade = max(0.0,ramp) /ramp_step ;
    //            *(out - 2) *= fade;
    //            *(out - 1) *= fade;
    //        // ramp down on loop end point
    //        } else if (ui.playBackwards ?
    //                    ui.position < ui.loopPoint_l + ramp_step :
    //                    ui.position > ui.loopPoint_r - ramp_step) {
    //            if (ramp > 0.0) {
    //                --ramp; 
    //            }
    //            const float fade = max(0.0,ramp) /ramp_step ;
    //            *(out - 2) *= fade;
    //            *(out - 1) *= fade;
    //        }
    //    }
    //} else {
    //    memset(out, 0.0, (uint32_t)frames * 2 * sizeof(float));
    //}
    Sync->notify_one();

    return 0;
}

#if defined(__linux__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
// catch signals and exit clean
void
signal_handler (int sig)
{
    switch (sig) {
        case SIGINT:
        case SIGHUP:
        case SIGTERM:
        case SIGQUIT:
            std::cerr << "\nsignal "<< sig <<" received, exiting ...\n"  <<std::endl;
            XLockDisplay(ui.w->app->dpy);
            ui.onExit();
            XFlush(ui.w->app->dpy);
            XUnlockDisplay(ui.w->app->dpy);
        break;
        default:
        break;
    }
}
#endif

int main(int argc, char *argv[]){


    allocate_rubberband_buffers();

    #if defined(__linux__) || defined(__FreeBSD__) || \
        defined(__NetBSD__) || defined(__OpenBSD__)
    if(0 == XInitThreads()) 
        std::cerr << "Warning: XInitThreads() failed\n" << std::endl;
    #endif

    Xputty app;
    std::condition_variable Sync;

    main_init(&app);
    ui.createGUI(&app, &Sync);

    #if defined(__linux__) || defined(__FreeBSD__) || \
        defined(__NetBSD__) || defined(__OpenBSD__)
    signal (SIGQUIT, signal_handler);
    signal (SIGTERM, signal_handler);
    signal (SIGHUP, signal_handler);
    signal (SIGINT, signal_handler);
    #endif

    XPa xpa ("alooper");
    if(!xpa.openStream(0, 2, &process, (void*) &Sync)) ui.onExit();

    ui.setJackSampleRate(xpa.getSampleRate());

    if(!xpa.startStream()) ui.onExit();
    ui.setPaStream(xpa.getStream());

    if (argv[1])
    #ifdef __XDG_MIME_H__
    if(strstr(xdg_mime_get_mime_type_from_file_name(argv[1]), "audio")) {
    #else
    if( access(argv[1], F_OK ) != -1 ) {
    #endif
        ui.dialog_response(ui.w, (void*) &argv[1]);
    }

    main_run(&app);

    ui.pl.stop();
    ui.pa.stop();
    main_quit(&app);
    xpa.stopStream();
    free_rubberband_buffers();
    printf("bye bye\n");
    return 0;
}

