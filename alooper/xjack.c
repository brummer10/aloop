/*
 * xjack.c
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */


#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>
#include <string>
#include <cmath>
#include <condition_variable>

#include <jack/jack.h>

#include "ParallelThread.h"
#include "xui.h"


Xputty app;
AudioLooperUi ui;

std::condition_variable      Sync;

jack_client_t *client;
jack_port_t *out_port;
jack_port_t *out_port1;
jack_port_t *default_out_port;
jack_port_t *default_out_port1;

float fRec0[2] = {0};

void
jack_shutdown (void *arg)
{
    ui.pa.stop();
    quit(ui.w);
    exit (1);
}

int
jack_xrun_callback(void *arg)
{
    fprintf (stderr, "Xrun \r");
    return 0;
}

int
jack_srate_callback(jack_nframes_t samplerate, void* arg)
{
    fprintf (stderr, "Samplerate %iHz \n", samplerate);
    ui.setJackSampleRate(samplerate);
    return 0;
}

int
jack_buffersize_callback(jack_nframes_t nframes, void* arg)
{
    fprintf (stderr, "Buffersize is %i samples \n", nframes);
    return 0;
}

int
jack_process(jack_nframes_t nframes, void *arg)
{
    float *out = static_cast<float *>(jack_port_get_buffer (out_port, nframes));
    float *out1 = static_cast<float *>(jack_port_get_buffer (out_port1, nframes));

    if (( ui.samplesize && ui.samples != nullptr) && ui.play) {
        float fSlow0 = 0.0010000000000000009 * ui.gain;
        for (uint32_t i = 0; i<nframes; i++) {
            fRec0[0] = fSlow0 + 0.999 * fRec0[1];
            for (uint32_t c = 0; c < ui.channels; c++) {
                if (!c) {
                    out[i] = ui.samples[ui.position*ui.channels] * fRec0[0];
                    if (ui.channels ==1) out1[i] = out[i];
                } else out1[i] = ui.samples[ui.position*ui.channels+c] * fRec0[0];
            }
            fRec0[1] = fRec0[0];
            ui.position++;
            if (ui.position > ui.samplesize) ui.position = 0;
        }
    } else {
        memset(out, 0.0, nframes * sizeof(float));
        memset(out1, 0.0, nframes * sizeof(float));
    }
    Sync.notify_all();
    return 0;
}

void
signal_handler (int sig)
{
    jack_client_close (client);
    if (sig == SIGTERM) {
        ui.pa.stop();
        quit(ui.w);
    }
    fprintf (stderr, "\n signal %i received, exiting ...\n", sig);
    exit (0);
}

int
main (int argc, char *argv[])
{
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    if(0 == XInitThreads()) 
        fprintf(stderr, "Warning: XInitThreads() failed\n");
#endif
    main_init(&app);
    ui.createGUI(&app, &Sync);

    if ((client = jack_client_open ("aloop", JackNoStartServer, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        ui.pa.stop();
        quit(ui.w);
        return 1;
    }

    out_port = jack_port_register(
                   client, "out_0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    out_port1 = jack_port_register(
                   client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    signal (SIGQUIT, signal_handler);
    signal (SIGTERM, signal_handler);
    signal (SIGHUP, signal_handler);
    signal (SIGINT, signal_handler);

    jack_set_xrun_callback(client, jack_xrun_callback, 0);
    jack_set_sample_rate_callback(client, jack_srate_callback, 0);
    jack_set_buffer_size_callback(client, jack_buffersize_callback, 0);
    jack_set_process_callback(client, jack_process, 0);
    jack_on_shutdown (client, jack_shutdown, 0);

    if (jack_activate (client)) {
        fprintf (stderr, "cannot activate client");
        ui.pa.stop();
        quit(ui.w);
        return 1;
    }

    if (!jack_is_realtime(client)) {
        fprintf (stderr, "jack isn't running with realtime priority\n");
    } else {
        fprintf (stderr, "jack running with realtime priority\n");
    }

    default_out_port = jack_port_by_name(client, "system:playback_1");
    default_out_port1 = jack_port_by_name(client, "system:playback_2");
    if (default_out_port && default_out_port1) {
        fprintf(stderr, "output connected to system:playback\n");
        jack_connect(client,"aloop:out_0", "system:playback_1");
        jack_connect(client,"aloop:out_1", "system:playback_2");
    }

    main_run(&app);
   
    ui.pa.stop();
    main_quit(&app);

    if (jack_port_connected(out_port)) jack_port_disconnect(client,out_port);
    jack_port_unregister(client,out_port);
    if (jack_port_connected(out_port1)) jack_port_disconnect(client,out_port1);
    jack_port_unregister(client,out_port1);
    jack_client_close (client);

    printf("bye bye\n");
    exit (0);
}
