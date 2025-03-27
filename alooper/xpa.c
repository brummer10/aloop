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

// process audio in background thread
static void processBuffer() {
    float* out = ui.audioBuffer;
    uint32_t frames = ui.frameSize;
    static float fRec0[2] = {0};
    static float ramp = 0.0;
    static const float ramp_step = 256.0;
    static const float ramp_impl = 1.0/ramp_step;
    float *const *rubberband_input_buffers = ui.vs.rubberband_input_buffers;
    float *const *rubberband_output_buffers = ui.vs.rubberband_output_buffers;

    ui.vs.rb->setTimeRatio(ui.timeRatio);
    ui.vs.rb->setPitchScale(ui.pitchScale);

    uint32_t source_channel_count = min(ui.af.channels,ui.vs.rb->getChannelCount());
    uint32_t ouput_channel_count = 2;
        
    if (( ui.af.samplesize && ui.af.samples != nullptr) && !ui.stop && ui.ready) {
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
                    }
                    fRec0[1] = fRec0[0];
                }
                needed -= retrived_frames_count;
            }
            if (needed>0){
                int process_samples = min(frames, MAX_RUBBERBAND_BUFFER_FRAMES);
                for (int i = 0 ; i < process_samples ;i++){
                    ui.playBackwards ? --ui.position : ++ui.position;
                    // check if play position excite play range
                    // if so reset play position and trigger check if new file
                    // should be loaded from play list
                    if (ui.playBackwards && ui.position <= ui.loopPoint_l) {
                        ui.position = ui.loopPoint_r;
                        ui.loadFile();
                    } else if (!ui.playBackwards && ui.position >= ui.loopPoint_r) {
                        ui.position = ui.loopPoint_l;
                        ui.loadFile();
                    }
                    // copy (de-interleaved)source to rubberband buffers
                    for (uint32_t c = 0 ; c < source_channel_count ;c++){
                        rubberband_input_buffers[c][i] = ui.af.samples[(ui.position * ui.af.channels) + c];
                    }
                    // cross fade over loop points
                    // ramp up on loop begin point + ramp_step
                    if (ui.playBackwards ?
                            ui.position > ui.loopPoint_r - ramp_step :
                            ui.position < ui.loopPoint_l + ramp_step) {
                        if (ramp < ramp_step) ++ramp;
                        const float fade = max(0.0,ramp) * ramp_impl ;
                        rubberband_input_buffers[0][i] *= fade;
                        rubberband_input_buffers[1][i] *= fade;
                    // ramp down on loop end point - ramp_step
                    } else if (ui.playBackwards ?
                            ui.position < ui.loopPoint_l + ramp_step :
                            ui.position > ui.loopPoint_r - ramp_step) {
                        if (ramp > 0.0) --ramp;
                        const float fade = max(0.0,ramp) * ramp_impl ;
                        rubberband_input_buffers[0][i] *= fade;
                        rubberband_input_buffers[1][i] *= fade;
                    }
                }
                // process source with rubberband stretcher
                ui.vs.rb->process( rubberband_input_buffers,process_samples,false);
            }
        }
    } else {
        ui.vs.rb->reset();
        memset(out, 0.0, (uint32_t)frames * 2 * sizeof(float));
    }
    ui.SyncWait.notify_one();
}


// the portaudio server process callback
static int process(const void* inputBuffer, void* outputBuffer,
    unsigned long frames, const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void* data) {

    float* out = static_cast<float*>(outputBuffer);
    (void) timeInfo;
    (void) statusFlags;
    static const float ramp_step = 2048.0;
    static const float ramp_impl = 1.0/ramp_step;
    static float ramp = ramp_step;
    static bool isDown = false;

    if (ui.inSave.load(std::memory_order_acquire)) {
        memset(out, 0.0, (uint32_t)frames * 2 * sizeof(float));
        ui.SyncWait.notify_one();
        return 0;
    }

    if (ui.frameSize != static_cast<uint32_t>(frames)) {
        ui.frameSize = static_cast<uint32_t>(frames);
        ui.getTimeOutTime.store(true, std::memory_order_release);
    }

    // get data from previous process and copy it to output
    ui.pr.processWait();
    memcpy(out, ui.audioBuffer, (uint32_t)frames * 2 * sizeof(float));

    // fade in/out when start/stop the playback
    if (!ui.play && !ui.stop) {
        for(uint32_t i = 0; i < (uint32_t)frames*2; i++) {
            if (ramp > 0.0) {
                --ramp;
            } else {
                ui.stop = true;
                isDown = true;
                ramp = ramp_step;
                uint32_t reset = ui.playBackwards ? 4096 : -4096;
                ui.position += reset;
            }
            const float fade = max(0.0,ramp) * ramp_impl;
            out[i] *= fade;
        }
    } else if (ui.play && isDown) {
        ui.stop = false;
        for(uint32_t i = 0; i < (uint32_t)frames*2; i++) {
            if (ramp < ramp_step) {
                ++ramp;
            } else {
                isDown = false;
                ramp = 0.0;
            }
            const float fade = max(0.0,ramp) * ramp_impl;
            out[i] *= fade;
        }
    }

    // process data from current process in background
    if (ui.pr.getProcess()) ui.pr.runProcess();

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

    main_init(&app);
    ui.createGUI(&app);

    #if defined(__linux__) || defined(__FreeBSD__) || \
        defined(__NetBSD__) || defined(__OpenBSD__)
    signal (SIGQUIT, signal_handler);
    signal (SIGTERM, signal_handler);
    signal (SIGHUP, signal_handler);
    signal (SIGINT, signal_handler);
    #endif

    XPa xpa ("alooper");
    if(!xpa.openStream(0, 2, &process, nullptr)) ui.onExit();

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

    ui.pr.set<processBuffer>();

    main_run(&app);

    ui.pl.stop();
    ui.pa.stop();
    main_quit(&app);
    xpa.stopStream();
    printf("bye bye\n");
    return 0;
}

