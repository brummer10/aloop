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
#include "vs.h"
#include "xui.h"
#include "xpa.h"

AudioLooperUi ui;

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

    float *const *rubberband_input_buffers = ui.vs.rubberband_input_buffers;
    float *const *rubberband_output_buffers = ui.vs.rubberband_output_buffers;

    ui.vs.rb->setTimeRatio(ui.timeRatio);
    ui.vs.rb->setPitchScale(ui.pitchScale);

    uint32_t source_channel_count = min(ui.af.channels,ui.vs.rb->getChannelCount());
    uint32_t ouput_channel_count = 2;
        
    if (( ui.af.samplesize && ui.af.samples != nullptr) && ui.play && ui.ready) {
        float fSlow0 = 0.0010000000000000009 * ui.gain;
        uint32_t needed = frames;
        while (needed>0){
            size_t available = ui.vs.rb->available();
            if (available > 0){
                size_t retrived_frames_count = ui.vs.rb->retrieve(rubberband_output_buffers,min(available,min(needed,MAX_RUBBERBAND_BUFFER_FRAMES)));
                for (size_t i = 0 ; i < retrived_frames_count ;i++){
                    fRec0[0] = fSlow0 + 0.999 * fRec0[1];
                    for (uint32_t c = 0 ; c < ouput_channel_count ;c++){
                        *out++ = rubberband_output_buffers[c%source_channel_count][i] * fRec0[0];
                        fRec0[1] = fRec0[0];
                    }
                    // cross-fade over loop points 
                    // ramp up on loop start point
                    if (ui.playBackwards ?
                        ui.position > ui.loopPoint_r - ramp_step :
                        ui.position < ui.loopPoint_l + ramp_step) {
                        if (ramp < ramp_step) {
                            ++ramp;
                        }
                        const float fade = max(0.0,ramp) /ramp_step ;
                        *(out - 2) *= fade;
                        *(out - 1) *= fade;
                    // ramp down on loop end point
                    } else if (ui.playBackwards ?
                                ui.position < ui.loopPoint_l + ramp_step :
                                ui.position > ui.loopPoint_r - ramp_step) {
                        if (ramp > 0.0) {
                            --ramp; 
                        }
                        const float fade = max(0.0,ramp) /ramp_step ;
                        *(out - 2) *= fade;
                        *(out - 1) *= fade;
                    }
                }
                needed -= retrived_frames_count;
            }
            if (needed>0){            
                int process_samples;
                if (ui.playBackwards){
                    process_samples = min(frames, MAX_RUBBERBAND_BUFFER_FRAMES);
                    for (int i = 0 ; i < process_samples ;i++){
                        if (ui.position-i <= ui.loopPoint_l) {
                            ui.position = ui.loopPoint_r;
                            ui.loadFile(); 
                        }    
                        for (uint32_t c = 0 ; c < source_channel_count ;c++){
                            rubberband_input_buffers[c][i] = ui.af.samples[ (ui.position - i) * ui.af.channels + c];
                        }
                    }
                    ui.position -= process_samples;
                } else {
                    process_samples = min(frames, MAX_RUBBERBAND_BUFFER_FRAMES);
                    for (int i = 0 ; i < process_samples ;i++){
                        if (ui.position+i >= ui.loopPoint_r) {
                            ui.position = ui.loopPoint_l;
                            ui.loadFile(); 
                        }
                        for (uint32_t c = 0 ; c < source_channel_count ;c++){
                            rubberband_input_buffers[c][i] = ui.af.samples[ (ui.position + i) * ui.af.channels + c];
                        }
                    }
                    ui.position += process_samples;
                }            
                ui.vs.rb->process( rubberband_input_buffers,process_samples,false);
                
            }
        }
    } else {
        ui.vs.rb->reset();
        memset(out, 0.0, (uint32_t)frames * 2 * sizeof(float));
    }
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
    printf("bye bye\n");
    return 0;
}

