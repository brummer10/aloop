/*
 * xui.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

#include <mutex>
#include <condition_variable>
#include <sndfile.hh>

#include "xwidgets.h"
#include "xfile-dialog.h"

#pragma once

#ifndef AUDIOLOOPERUI_H
#define AUDIOLOOPERUI_H

class AudioLooperUi
{
public:
    Widget_t *w;
    ParallelThread pa;

    float *samples;
    uint32_t channels;
    uint32_t samplesize;
    uint32_t samplerate;
    uint32_t position;
    float gain;
    bool loadNew;
    bool play;

    AudioLooperUi() {
        samplesize = 0;
        position = 0;
        gain = std::pow(1e+01, 0.05 * 0.0);
        samples = nullptr;
        loadNew = false;
        play = true;
    };

    ~AudioLooperUi() {
        delete[] samples;
        pa.stop();
    };

    void createGUI(Xputty *app, std::condition_variable *Sync_) {
        SyncWait =Sync_;
        w = create_window(app, DefaultRootWindow(app->dpy), 0, 0, 400, 170);
        widget_set_title(w, "alooper");
        widget_set_icon_from_png(w,LDVAR(alooper_png));
        widget_set_dnd_aware(w);
        w->parent_struct = (void*)this;
        w->func.expose_callback = draw_window;
        w->func.dnd_notify_callback = dnd_load_response;
        wview = add_waveview(w, "", 20, 20, 360, 100);
        wview->scale.gravity = NORTHWEST;
        wview->parent_struct = (void*)this;
        wview->adj = add_adjustment(wview,0.0, 0.0, 0.0, 1000.0,1.0, CL_METER);
        wview->func.expose_callback = draw_wview;
        filebutton = add_file_button(w, 20, 130, 30, 30, getenv("HOME") ? getenv("HOME") : "/", "audio");
        filebutton->scale.gravity = SOUTHEAST;
        filebutton->parent_struct = (void*)this;
        widget_get_png(filebutton, LDVAR(dir_png));
        filebutton->func.user_callback = dialog_response;
        volume = add_knob(w, "dB",255,130,28,28);
        volume->parent_struct = (void*)this;
        volume->scale.gravity = SOUTHWEST;
        set_adjustment(volume->adj, 0.0, 0.0, -20.0, 6.0, 0.1, CL_CONTINUOS);
        volume->func.expose_callback = draw_knob;
        volume->func.value_changed_callback = volume_callback;
        
        backset = add_button(w, "", 290, 130, 30, 30);
        backset->parent_struct = (void*)this;
        backset->scale.gravity = SOUTHWEST;
        widget_get_png(backset, LDVAR(rewind_png));
        backset->func.value_changed_callback = button_backset_callback;
        paus = add_image_toggle_button(w, "", 320, 130, 30, 30);
        paus->scale.gravity = SOUTHWEST;
        paus->parent_struct = (void*)this;
        widget_get_png(paus, LDVAR(pause_png));
        paus->func.value_changed_callback = button_pause_callback;
        w_quit = add_button(w, "", 350, 130, 30, 30);
        w_quit->parent_struct = (void*)this;
        widget_get_png(w_quit, LDVAR(exit_png));
        w_quit->scale.gravity = SOUTHWEST;
        w_quit->func.value_changed_callback = button_quit_callback;
        widget_show_all(w);
        
        pa.startTimeout(60);
        pa.set<AudioLooperUi, &AudioLooperUi::updateUI>(this);
    }

private:
    Widget_t *w_quit;
    Widget_t *filebutton;
    Widget_t *wview;
    Widget_t *paus;
    Widget_t *backset;
    Widget_t *volume;
    std::condition_variable *SyncWait;
    std::mutex WMutex;

    void read_soundfile(const char* file) {
        // struct to hols sound file info
        SF_INFO info;
        info.format = 0;

        channels = 0;
        samplesize = 0;
        samplerate = 0;
        position = 0;
        std::unique_lock<std::mutex> lk(WMutex);
        SyncWait->wait(lk);
        delete[] samples;
        samples = nullptr;

        // Open the wave file for reading
        SNDFILE *sndfile = sf_open(file, SFM_READ, &info);

        if (!sndfile) {
            std::cerr << "Error: could not open file" << std::endl;
            return ;
        }
        if (info.channels > 2) {
            std::cerr << "Error: Maximal two channels been supported!" << std::endl;
            return ;
        }
        samples = new float[info.frames * info.channels];
        sf_readf_float(sndfile, &samples[0], info.frames );
        channels = info.channels;
        samplesize = info.frames;
        samplerate = info.samplerate;
        position = 0;
        sf_close(sndfile);
        loadNew = true;
        adj_set_max_value(wview->adj, (float)samplesize);

        update_waveview(wview, samples, samplesize);
        char name[256];
        strncpy(name, file, 255);
        widget_set_title(w, basename(name));
    }

    static void dnd_load_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if(user_data !=NULL) {
            char* dndfile = NULL;
            dndfile = strtok(*(char**)user_data, "\r\n");
            while (dndfile != NULL) {
                if (strstr(dndfile, ".wav") ) {
                    self->read_soundfile(dndfile);
                    break;
                } else if (strstr(dndfile, ".flac")) {
                    self->read_soundfile(dndfile);
                    break;
                } else if (strstr(dndfile, ".ogg")) {
                    self->read_soundfile(dndfile);
                    break;
                }
                dndfile = strtok(NULL, "\r\n");
            }
        }
    }

    static void dialog_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if(user_data !=NULL) {
            self->read_soundfile(*(const char**)user_data);
        } else {
            fprintf(stderr, "no file selected\n");
        }
    }

    static void dummy_callback(void *w_, void* user_data) {}

    void updateUI() {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        XLockDisplay(w->app->dpy);
        wview->func.adj_callback = dummy_callback;
#endif
        adj_set_value(wview->adj, (float) position);
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        expose_widget(wview);
        XFlush(w->app->dpy);
        wview->func.adj_callback = transparent_draw;
        XUnlockDisplay(w->app->dpy);
#endif
    }

    void roundrec(cairo_t *cr, float x, float y, float width, float height, float r) {
        cairo_arc(cr, x+r, y+r, r, M_PI, 3*M_PI/2);
        cairo_arc(cr, x+width-r, y+r, r, 3*M_PI/2, 0);
        cairo_arc(cr, x+width-r, y+height-r, r, 0, M_PI/2);
        cairo_arc(cr, x+r, y+height-r, r, M_PI/2, M_PI);
        cairo_close_path(cr);
    }

    static void draw_knob(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;

        const double scale_zero = 20 * (M_PI/180); // defines "dead zone" for knobs
        int arc_offset = 0;
        int knob_x = 0;
        int knob_y = 0;

        int grow = (width > height) ? height:width;
        knob_x = grow-1;
        knob_y = grow-1;
        /** get values for the knob **/

        const int knobx1 = width* 0.5;

        const int knoby1 = height * 0.5;

        const double knobstate = adj_get_state(w->adj_y);
        const double angle = scale_zero + knobstate * 2 * (M_PI - scale_zero);

        const double pointer_off =knob_x/6;
        const double radius = min(knob_x-pointer_off, knob_y-pointer_off) / 2;

        const double add_angle = 90 * (M_PI / 180.);
        // base
        use_base_color_scheme(w, INSENSITIVE_);
        cairo_set_line_width(w->crb,  5.0/w->scale.ascale);
        cairo_arc (w->crb, knobx1+arc_offset, knoby1+arc_offset, radius,
              add_angle + scale_zero, add_angle + scale_zero + 320 * (M_PI/180));
        cairo_stroke(w->crb);

        // indicator
        cairo_set_line_width(w->crb,  3.0/w->scale.ascale);
        cairo_new_sub_path(w->crb);
        cairo_set_source_rgba(w->crb, 0.75, 0.75, 0.75, 1);
        cairo_arc (w->crb,knobx1+arc_offset, knoby1+arc_offset, radius,
              add_angle + scale_zero, add_angle + angle);
        cairo_stroke(w->crb);

        use_text_color_scheme(w, get_color_state(w));
        cairo_text_extents_t extents;
        /** show value on the kob**/
        char s[64];
        float value = adj_get_value(w->adj);
        snprintf(s, 63, "%.1f", value);
        cairo_set_font_size (w->crb, (w->app->small_font-2)/w->scale.ascale);
        cairo_text_extents(w->crb, s, &extents);
        cairo_move_to (w->crb, knobx1-extents.width/2, knoby1+extents.height/2);
        cairo_show_text(w->crb, s);
        cairo_new_path (w->crb);
    }

    void create_waveview_image(Widget_t *w, int width, int height) {
        cairo_surface_destroy(w->image);
        w->image = NULL;   
        w->image = cairo_surface_create_similar (w->surface, 
                            CAIRO_CONTENT_COLOR_ALPHA, width, height);
        cairo_t *cri = cairo_create (w->image);

        WaveView_t *wave_view = (WaveView_t*)w->private_struct;
        int half_height_t = height/2;

        cairo_set_line_width(cri,2);
        cairo_set_source_rgba(cri, 0.05, 0.05, 0.05, 1);
        roundrec(cri, 0, 0, width, height, 5);
        cairo_fill_preserve(cri);
        cairo_set_source_rgba(cri, 0.33, 0.33, 0.33, 1);
        cairo_stroke(cri);
        cairo_move_to(cri,2,half_height_t);
        cairo_line_to(cri, width, half_height_t);
        cairo_stroke(cri);

        if (wave_view->size<1) return;
        int step = (wave_view->size/width)/channels;
        float lstep = (float)(half_height_t)/channels;
        cairo_set_line_width(cri,2);
        cairo_set_source_rgba(cri, 0.55, 0.65, 0.55, 1);

        int pos = half_height_t/channels;
        for (int c = 0; c < (int)channels; c++) {
            for (int i=0;i<width-4;i++) {
                cairo_move_to(cri,i+2,pos);
                cairo_line_to(cri, i+2,(float)(pos)+ (-wave_view->wave[int(c+(i*channels)*step)]*lstep));
                cairo_line_to(cri, i+2,(float)(pos)+ (wave_view->wave[int(c+(i*channels)*step)]*lstep));
            }
            pos += half_height_t;
        }
        cairo_stroke(cri);
        cairo_destroy(cri);
    }

    static void draw_wview(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width_t = metrics.width;
        int height_t = metrics.height;
        if (!metrics.visible) return;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        int width, height;
        if (w->image) {
            os_get_surface_size(w->image, &width, &height);
            if ((width != width_t || height != height_t) || self->loadNew) {
                self->loadNew = false;
                self->create_waveview_image(w, width_t, height_t);
                os_get_surface_size(w->image, &width, &height);
            }
        } else {
            self->create_waveview_image(w, width_t, height_t);
            os_get_surface_size(w->image, &width, &height);
        }
        cairo_set_source_surface (w->crb, w->image, 0, 0);
        cairo_rectangle(w->crb,0, 0, width, height);
        cairo_fill(w->crb);

        double state = adj_get_state(w->adj);
        cairo_set_source_rgba(w->crb, 0.55, 0.05, 0.05, 1);
        cairo_rectangle(w->crb, 1+(width-2)*state,2,3, height-4);
        cairo_fill(w->crb);
    }

    static void boxShadowOutset(cairo_t* const cr, int x, int y, int width, int height, bool fill) {
        cairo_pattern_t *pat = cairo_pattern_create_linear (x, y, x + width, y);
        cairo_pattern_add_color_stop_rgba
            (pat, 0,0.33,0.33,0.33, 1.0);
        cairo_pattern_add_color_stop_rgba
            (pat, 0.1,0.33 * 0.6,0.33 * 0.6,0.33 * 0.6, 0.0);
        cairo_pattern_add_color_stop_rgba
            (pat, 0.97, 0.05 * 2.0, 0.05 * 2.0, 0.05 * 2.0, 0.0);
        cairo_pattern_add_color_stop_rgba 
            (pat, 1, 0.05, 0.05, 0.05, 1.0);
        cairo_set_source(cr, pat);
        if (fill) cairo_fill_preserve (cr);
        else cairo_paint (cr);
        cairo_pattern_destroy (pat);
        pat = NULL;
        pat = cairo_pattern_create_linear (x, y, x, y + height);
        cairo_pattern_add_color_stop_rgba
            (pat, 0,0.33,0.33,0.33, 1.0);
        cairo_pattern_add_color_stop_rgba
            (pat, 0.1,0.33 * 0.6,0.33 * 0.6,0.33 * 0.6, 0.0);
        cairo_pattern_add_color_stop_rgba
            (pat, 0.97, 0.05 * 2.0, 0.05 * 2.0, 0.05 * 2.0, 0.0);
        cairo_pattern_add_color_stop_rgba
            (pat, 1, 0.05, 0.05, 0.05, 1.0);
        cairo_set_source(cr, pat);
        if (fill) cairo_fill_preserve (cr);
        else cairo_paint (cr);
        cairo_pattern_destroy (pat);
    }

    static void draw_window(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;

        cairo_pattern_t *pat;
        pat = cairo_pattern_create_linear (0.0, 0.0, width , height);
        cairo_pattern_add_color_stop_rgba (pat, 1, 0.2, 0.2, 0.2, 1);
        cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0., 1);
        cairo_rectangle(w->crb,0,0,width,height);
        cairo_set_source (w->crb, pat);
        cairo_fill_preserve (w->crb);
        boxShadowOutset(w->crb, 0.0, 0.0, width , height, true);
        cairo_fill (w->crb);
        cairo_pattern_destroy (pat);
    }

    static void button_quit_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if (w->flags & HAS_POINTER && !*(int*)user_data){
            Widget_t *p = (Widget_t*)w->parent;
            self->pa.stop();
            quit(p);
        }
    }

    static void button_pause_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            self->play = false;
        } else self->play = true;
    }

    static void button_backset_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && !adj_get_value(w->adj)){
            self->position = 0;
        }
    }

    static void volume_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        self->gain = std::pow(1e+01, 0.05 * adj_get_value(w->adj));
    }

};

#endif

