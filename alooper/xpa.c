/*
 * xpa.c
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */


#include <portaudio.h>
#include <pa_jack.h>
#include <cmath>
#include <vector>
#include <signal.h>
#include <cstdio>
#include <algorithm>
#include <unistd.h>
#include <iostream>
#include <string>
#include <condition_variable>
#include <memory>


#include "ParallelThread.h"
#include "xui.h"



Xputty app;
AudioLooperUi ui;

std::condition_variable      Sync;

float fRec0[2] = {0};

struct Devices {
    int order;
    int index;
    uint32_t SampleRate;
    const char* host;
};


int process(const void* inputBuffer, void* outputBuffer,
    unsigned long frames, const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void* data) {

    float* out = static_cast<float*>(outputBuffer);
    (void) timeInfo;
    (void) statusFlags;

    if (( ui.samplesize && ui.samples != nullptr) && ui.play && ui.ready) {
        float fSlow0 = 0.0010000000000000009 * ui.gain;
        for (uint32_t i = 0; i<(uint32_t)frames; i++) {
            fRec0[0] = fSlow0 + 0.999 * fRec0[1];
            for (uint32_t c = 0; c < ui.channels; c++) {
                if (!c) {
                    *out++ = ui.samples[ui.position*ui.channels] * fRec0[0];
                    if (ui.channels ==1) *out++ = ui.samples[ui.position*ui.channels] * fRec0[0];
                } else *out++ = ui.samples[ui.position*ui.channels+c] * fRec0[0];
            }
            fRec0[1] = fRec0[0];
            ui.position++;
            if (ui.position > ui.samplesize) ui.position = 0;
        }
    } else {
        memset(out, 0.0, (uint32_t)frames * 2 * sizeof(float));
    }
    Sync.notify_all();

    return 0;
}

const char* getApiName(unsigned int index) {
    unsigned int acount = Pa_GetHostApiCount();
    if (acount <= 0 || (index > acount-1)){
        return "";
    }
    const PaHostApiInfo* info =  Pa_GetHostApiInfo(index);
    return info->name;
}

void
signal_handler (int sig)
{
    switch (sig) {
        case SIGINT:
        case SIGHUP:
        case SIGTERM:
        case SIGQUIT:
            fprintf (stderr, "signal %i received, exiting ...\n", sig);
#if defined(__linux__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
            XLockDisplay(app.dpy);
#endif
            ui.onExit();
#if defined(__linux__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
            XFlush(app.dpy);
            XUnlockDisplay(app.dpy);
#endif
        break;
        default:
        break;
    }
}

int main(int argc, char *argv[]){
#if defined(__linux__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
    if(0 == XInitThreads()) 
        fprintf(stderr, "Warning: XInitThreads() failed\n");
#endif
    main_init(&app);
    ui.createGUI(&app, &Sync);

    signal (SIGQUIT, signal_handler);
    signal (SIGTERM, signal_handler);
    signal (SIGHUP, signal_handler);
    signal (SIGINT, signal_handler);

    PaJack_SetClientName ("alooper");

    char buffer[1024];
    auto fp = fmemopen(buffer, 1024, "w");
    if ( !fp ) { std::printf("error"); return 0; }
    auto old = stderr;
    stderr = fp;

    PaError err;
    err = Pa_Initialize();
    if( err != paNoError ) {
        ui.onExit();
    }

    std::vector<Devices> devices;
    int d = Pa_GetDeviceCount();
    const PaDeviceInfo *info;
    for (int i = 0; i<d;i++) {
        info = Pa_GetDeviceInfo(i);
        if ((std::strcmp(info->name, "pulse") ==0) ||
           (std::strcmp(info->name, "default") ==0) ||
           (std::strcmp(info->name, "system") ==0)) {
            Devices dev;
            if (std::strcmp(info->name, "pulse") ==0) dev.order = 2; // pulse audio
            if (std::strcmp(info->name, "default") ==0) dev.order = 3; // alsa
            if (std::strcmp(info->name, "system") ==0) dev.order = 1; // jackd
            dev.index = i;
            dev.SampleRate = info->defaultSampleRate;
            dev.host = getApiName(info->hostApi);
            devices.push_back(dev);
        }
    }

    std::sort(devices.begin(), devices.end(), 
    [](Devices const &a, Devices const &b) {
        return a.order < b.order; 
    });

    auto it = devices.begin();
    
    PaStream* stream;
    PaStreamParameters outputParameters;
    outputParameters.device = it->index;
    outputParameters.channelCount = 2;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(&stream, nullptr, &outputParameters, it->SampleRate,
                        paFramesPerBufferUnspecified, paClipOff, process, nullptr);

    std::fclose(fp);
    stderr = old;

    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        ui.onExit();
    }
    ui.setJackSampleRate(it->SampleRate);

    err = Pa_StartStream(stream);
    ui.setPaStream(stream);

    if (argv[1])
#ifdef __XDG_MIME_H__
    if(strstr(xdg_mime_get_mime_type_from_file_name(argv[1]), "audio")) {
#else
    if( access(argv[1], F_OK ) != -1 ) {
#endif
        ui.dialog_response(ui.w, (void*) &argv[1]);
    }
 
    main_run(&app);
   
    ui.pa.stop();
    main_quit(&app);
   
    if (Pa_IsStreamActive(stream)) {
        err = Pa_StopStream(stream);
        if (err != paNoError) {
            std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        }
        err = Pa_CloseStream(stream);
        if (err != paNoError) {
            std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        }
        Pa_Terminate();
    }

    devices.clear();
    printf("bye bye\n");
    return 0;
}

