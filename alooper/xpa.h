
/*
 * xpa.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

/****************************************************************
  XPa - a C++ wrapper for portaudio

  silent the portaudio device probe messages
  connection preference is set to jackd, pulse audio, alsa 

****************************************************************/

#include <portaudio.h>
#include <pa_jack.h>
#include <cmath>
#include <vector>
#include <cstdio>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string>
#include <stdio.h>

#pragma once

#ifndef XPA_H
#define XPA_H

class XPa {
public:

    XPa(){
        init();
        SampleRate = 0;
    };

    ~XPa(){Pa_Terminate();};

    // set the client name to be used within jackd
    void setClientName(const char* cname) {
        PaJack_SetClientName (cname);
    }

    // open a audio stream for input/output channels and set the audio process callback
    bool openStream(uint32_t ichannels, uint32_t ochannels, PaStreamCallback *process, void* arg) {
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
                dev.SampleRate = SampleRate = info->defaultSampleRate;
                devices.push_back(dev);
            }
        }

        std::sort(devices.begin(), devices.end(), 
        [](Devices const &a, Devices const &b) {
            return a.order < b.order; 
        });
        auto it = devices.begin();
        PaStreamParameters inputParameters;
        inputParameters.device = it->index;
        inputParameters.channelCount = ichannels;
        inputParameters.sampleFormat = paFloat32;
        inputParameters.hostApiSpecificStreamInfo = nullptr;

        PaStreamParameters outputParameters;
        outputParameters.device = it->index;
        outputParameters.channelCount = ochannels;
        outputParameters.sampleFormat = paFloat32;
        outputParameters.hostApiSpecificStreamInfo = nullptr;

        err = Pa_OpenStream(&stream, ichannels ? &inputParameters : nullptr, 
                            ochannels ? &outputParameters : nullptr, it->SampleRate,
                            paFramesPerBufferUnspecified, paClipOff, process, arg);

        devices.clear();
        return err == paNoError ? true : false;
    }

    // start the audio processing
    bool startStream() {
        err = Pa_StartStream(stream);
        return err == paNoError ? true : false;
    }

    // helper function to get a pointer to the PaStream object
    PaStream* getStream() {
        return stream;
    }

    // helper function to get the SampleRate used by the audio sever
    uint32_t getSampleRate() {
        return SampleRate;
    }

    // stop the audio processing
    void stopStream() {
        if (Pa_IsStreamActive(stream)) {
            err = Pa_StopStream(stream);
            if (err != paNoError) {
                std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
            }
            err = Pa_CloseStream(stream);
            if (err != paNoError) {
                std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
            }
        }
    }

private:
    PaStream* stream;
    PaError err;
    uint32_t SampleRate;

    struct Devices {
        int order;
        int index;
        uint32_t SampleRate;
    };

    void init() {
        char buffer[1024];
        auto fp = fmemopen(buffer, 1024, "w");
        if ( !fp ) { std::printf("error"); }
        auto old = stderr;
        stderr = fp;
        Pa_Initialize();
        std::fclose(fp);
        stderr = old;
    }

};


#endif
