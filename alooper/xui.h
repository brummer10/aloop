
/*
 * xui.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */


#ifndef JACKAPI
#include <portaudio.h>
#endif
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>
#include <vector>
#include <string>
#include <sndfile.hh>
#include <fstream>
#include <limits>
#include <cstdint>

#include "PlayList.h"
#include "AudioFile.h"
#include "xwidgets.h"
#include "xfile-dialog.h"
#include "TextEntry.h"

#pragma once

#ifndef AUDIOLOOPERUI_H
#define AUDIOLOOPERUI_H

/****************************************************************
    class SupportedFormats - check libsndfile for supported file formats
****************************************************************/

class SupportedFormats {
public:
    SupportedFormats() {
        supportedExtensions = getSupportedFileExtensions();
    }

    bool isSupported(std::string filename) {
        std::filesystem::path p(filename);
        std::string ext = p.extension().string();

        if (not ext.empty()) {
            // check for lower-cased file extension
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            return supportedExtensions.count(ext.substr(1)) >= 1;
        }

        return false;
    }

private:
    std::set<std::string> supportedExtensions;

    std::set<std::string> getSupportedFileExtensions() {
        std::set<std::string> extensions;

        // Get the number of supported simple formats
        int simpleFormatCount;
        sf_command(nullptr, SFC_GET_SIMPLE_FORMAT_COUNT, &simpleFormatCount, sizeof(int));

        // Get the number of supported major formats
        int majorFormatCount;
        sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &majorFormatCount, sizeof(int));

        // Get the number of supported sub formats
        int subFormatCount;
        sf_command(nullptr, SFC_GET_FORMAT_SUBTYPE_COUNT, &subFormatCount, sizeof(int));

        // Get information about each simple format
        for (int i = 0; i < simpleFormatCount; ++i) {
            SF_FORMAT_INFO formatInfo;
            formatInfo.format = i;
            sf_command(nullptr, SFC_GET_SIMPLE_FORMAT, &formatInfo, sizeof(formatInfo));

            if (formatInfo.extension != nullptr)
                extensions.insert(formatInfo.extension);
        }

        // Get information about each major format
        for (int i = 0; i < majorFormatCount; i++) {
            SF_FORMAT_INFO formatInfo;
            formatInfo.format = i;
            sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &formatInfo, sizeof(formatInfo));

            if (formatInfo.extension != nullptr)
                extensions.insert(formatInfo.extension);
        }

        // Get information about each sub format
        for (int j = 0; j < subFormatCount; j++) {
            SF_FORMAT_INFO formatInfo;
            formatInfo.format = j;
            sf_command(nullptr, SFC_GET_FORMAT_SUBTYPE, &formatInfo, sizeof(SF_FORMAT_INFO));

            if (formatInfo.extension != nullptr)
                extensions.insert(formatInfo.extension);
        }

        return extensions;
    }
};

/****************************************************************
    class AudioLooperUi - create the GUI for alooper
****************************************************************/

class AudioLooperUi: public TextEntry
{
public:
    Widget_t *w;
    ParallelThread pa;
    ParallelThread pl;
    AudioFile af;

    uint32_t jack_sr;
    uint32_t position;
    uint32_t loopPoint_l;
    uint32_t loopPoint_r;
    float gain;
    bool loadNew;
    bool play;
    bool ready;
    bool playBackwards;

    AudioLooperUi() : af(), pre_af(), plist("alooper") {
        jack_sr = 0;
        position = 0;
        loopPoint_l = 0;
        loopPoint_r = 1000;
        playNow = 0;
        gain = std::pow(1e+01, 0.05 * 0.0);
        pre_load = false;
        is_loaded = false;
        loadNew = false;
        play = true;
        ready = true;
        usePlayList = false;
        forceReload = false;
        playBackwards = false;
        blockWriteToPlayList = false;
        viewPlayList = nullptr;
        stream = nullptr;
        execute.store(true, std::memory_order_release);
        plist.read_PlayList();
    };

    ~AudioLooperUi() {
        pl.stop();
        pa.stop();
    };

/****************************************************************
                      public function calls
****************************************************************/

    // stop background threads and quit main window
    void onExit() {
        pl.stop();
        pa.stop();
        quit(w);
    }

    // receive Sample Rate from audio back-end
    void setJackSampleRate(uint32_t sr) {
        jack_sr = sr;
    }

    // receive stream object from portaudio to check 
    // if the server is actual running
    void setPaStream(PaStream* stream_) {
        stream = stream_;
    }

    // receive a file name from the File Browser or the command-line
    static void dialog_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if (!Pa_IsStreamActive(self->stream)) return;
        if(user_data !=NULL) {
            self->pre_load = false;
            self->blockWriteToPlayList = true;
            self->addToPlayList(*(char**)user_data, true);
            self->forceReload = true;
            self->playNow = self->plist.Play_list.size()-2;
            self->loadFile();
            self->blockWriteToPlayList = false;
        } else {
            std::cerr << "no file selected" <<std::endl;
        }
    }

    // load a audio file in background process
    void loadFile() {
        if (execute.load(std::memory_order_acquire)) {
            execute.store(false, std::memory_order_release);
            if (pl.getProcess()) pl.runProcess();
        }
    }

/****************************************************************
                      main window
****************************************************************/

    // create the main GUI
    void createGUI(Xputty *app, std::condition_variable *Sync_) {
        SyncWait =Sync_;
        w_top = create_window(app, os_get_root_window(app, IS_WINDOW), 0, 0, 400, 170);
        widget_set_title(w_top, "alooper");
        widget_set_icon_from_png(w_top,LDVAR(alooper_png));
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        widget_set_dnd_aware(w_top);
        #endif
        w_top->parent_struct = (void*)this;
        w_top->func.dnd_notify_callback = dnd_load_response;
        w_top->func.key_press_callback = key_press;

        w = create_widget(app, w_top, 0, 0, 400, 170);
        widget_set_icon_from_png(w,LDVAR(alooper_png));
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        widget_set_dnd_aware(w);
        #endif
        w->parent_struct = (void*)this;
        w->parent = w_top;
        w->scale.gravity = NORTHWEST;
        w->func.expose_callback = draw_window;
        w->func.dnd_notify_callback = dnd_load_response;
        w->func.key_press_callback = key_press;

        loopMark_L = add_hslider(w, "",15, 2, 185, 18);
        loopMark_L->scale.gravity = EASTCENTER;
        loopMark_L->parent_struct = (void*)this;
        loopMark_L->adj_x = add_adjustment(loopMark_L,0.0, 0.0, 0.0, 1000.0,1.0, CL_CONTINUOS);
        loopMark_L->adj = loopMark_L->adj_x;
        loopMark_L->func.expose_callback = draw_l_slider;
        loopMark_L->func.button_release_callback = slider_l_released;
        loopMark_L->func.value_changed_callback = slider_l_changed_callback;

        loopMark_R = add_hslider(w, "",200, 2, 185, 18);
        loopMark_R->scale.gravity = WESTCENTER;
        loopMark_R->parent_struct = (void*)this;
        loopMark_R->adj_x = add_adjustment(loopMark_R,0.0, 0.0, -1000.0, 0.0,1.0, CL_CONTINUOS);
        loopMark_R->adj = loopMark_R->adj_x;
        loopMark_R->func.expose_callback = draw_r_slider;
        loopMark_R->func.button_release_callback = slider_r_released;
        loopMark_R->func.value_changed_callback = slider_r_changed_callback;

        wview = add_waveview(w, "", 20, 20, 360, 100);
        wview->scale.gravity = NORTHWEST;
        wview->parent_struct = (void*)this;
        wview->adj_x = add_adjustment(wview,0.0, 0.0, 0.0, 1000.0,1.0, CL_METER);
        wview->adj = wview->adj_x;
        wview->func.expose_callback = draw_wview;
        wview->func.button_release_callback = set_playhead;
        wview->func.key_press_callback = key_press;

        expand = add_image_toggle_button(w, "", 20, 130, 30, 30);
        expand->parent_struct = (void*)this;
        expand->scale.gravity = SOUTHEAST;
        widget_get_png(expand, LDVAR(expand_png));
        expand->func.value_changed_callback = button_expand_callback;

        filebutton = add_file_button(w, 60, 130, 30, 30, getenv("HOME") ? getenv("HOME") : "/", "audio");
        filebutton->scale.gravity = SOUTHEAST;
        filebutton->parent_struct = (void*)this;
        widget_get_png(filebutton, LDVAR(dir_png));
        filebutton->func.user_callback = dialog_response;

        lview = add_image_toggle_button(w, "", 90, 130, 30, 30);
        lview->parent_struct = (void*)this;
        lview->scale.gravity = SOUTHEAST;
        widget_get_png(lview, LDVAR(menu_png));
        lview->func.value_changed_callback = button_lview_callback;

        saveLoop = add_save_file_button(w, 120, 130, 30, 30, getenv("HOME") ? getenv("HOME") : "/", "audio");
        saveLoop->parent_struct = (void*)this;
        saveLoop->scale.gravity = SOUTHEAST;
        widget_get_png(saveLoop, LDVAR(save__png));
        saveLoop->func.user_callback = write_soundfile;

        volume = add_knob(w, "dB",220,130,28,28);
        volume->parent_struct = (void*)this;
        volume->scale.gravity = SOUTHWEST;
        set_adjustment(volume->adj, 0.0, 0.0, -20.0, 6.0, 0.1, CL_CONTINUOS);
        volume->func.expose_callback = draw_knob;
        volume->func.value_changed_callback = volume_callback;

        backwards = add_image_toggle_button(w, "", 260, 130, 30, 30);
        backwards->scale.gravity = SOUTHWEST;
        backwards->parent_struct = (void*)this;
        widget_get_png(backwards, LDVAR(backwards_png));
        backwards->func.value_changed_callback = button_backwards_callback;

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
        widget_get_png(w_quit, LDVAR(exit__png));
        w_quit->scale.gravity = SOUTHWEST;
        w_quit->func.value_changed_callback = button_quit_callback;

        widget_show_all(w_top);

        pa.startTimeout(60);
        pa.set<AudioLooperUi, &AudioLooperUi::updateUI>(this);

        pl.start();
        pl.set<AudioLooperUi, &AudioLooperUi::loadFromPlayList>(this);

    }

private:
    Widget_t *w_top;
    Widget_t *w_quit;
    Widget_t *filebutton;
    Widget_t *wview;
    Widget_t *loopMark_L;
    Widget_t *loopMark_R;
    Widget_t *paus;
    Widget_t *backset;
    Widget_t *volume;
    Widget_t *backwards;
    Widget_t *lview;
    Widget_t *viewPlayList;
    Widget_t *playList;
    Widget_t *deleteEntry;
    Widget_t *upEntry;
    Widget_t *downEntry;
    Widget_t *loadPlayList;
    Widget_t *savePlayList;
    Widget_t *LoadMenu;
    Widget_t *SaveMenu;
    Widget_t *SaveItem;
    Widget_t *saveLoop;
    Widget_t *expand;

    SupportedFormats supportedFormats;
    AudioFile pre_af;
    PlayList plist;

    PaStream* stream;

    std::condition_variable *SyncWait;
    std::mutex WMutex;

    uint32_t playNow;
    bool usePlayList;
    bool forceReload;
    bool blockWriteToPlayList;
    bool pre_load;
    bool is_loaded;
    std::atomic<bool>  execute;
    std::string currentPlayList;
    std::string newLabel;

/****************************************************************
            PlayList - create the window
****************************************************************/

    // create the Play List window
    void createPlayListView(Xputty *app) {
        viewPlayList = create_widget(app, w_top, 0, 170, 400, 340);
        viewPlayList->flags |= HIDE_ON_DELETE;
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        widget_set_dnd_aware(viewPlayList);
        #endif
        viewPlayList->parent_struct = (void*)this;
        viewPlayList->parent = w_top;
        viewPlayList->scale.gravity = EASTSOUTH;
        viewPlayList->func.expose_callback = draw_window;
        viewPlayList->func.dnd_notify_callback = dnd_load_playlist;

        playList = add_listbox(viewPlayList, "", 20, 20, 360, 270);
        playList->parent_struct = (void*)this;
        playList->scale.gravity = NORTHWEST;
        playList->func.user_paste_callback = listbox_move_callback;

        loadPlayList = add_button(viewPlayList, "", 20, 300, 30, 30);
        loadPlayList->parent_struct = (void*)this;
        widget_get_png(loadPlayList, LDVAR(load__png));
        loadPlayList->scale.gravity = SOUTHEAST;
        loadPlayList->func.value_changed_callback = load_up_callback;

        savePlayList = add_button(viewPlayList, "", 50, 300, 30, 30);
        savePlayList->parent_struct = (void*)this;
        widget_get_png(savePlayList, LDVAR(save__png));
        savePlayList->scale.gravity = SOUTHEAST;
        savePlayList->func.value_changed_callback = save_callback;

        upEntry = add_button(viewPlayList, "", 320, 300, 30, 30);
        upEntry->parent_struct = (void*)this;
        widget_get_png(upEntry, LDVAR(up_png));
        upEntry->scale.gravity = SOUTHWEST;
        upEntry->func.value_changed_callback = up_entry_callback;

        downEntry = add_button(viewPlayList, "", 350, 300, 30, 30);
        downEntry->parent_struct = (void*)this;
        widget_get_png(downEntry, LDVAR(down_png));
        downEntry->scale.gravity = SOUTHWEST;
        downEntry->func.value_changed_callback = down_entry_callback;

        deleteEntry = add_button(viewPlayList, "", 290, 300, 30, 30);
        deleteEntry->parent_struct = (void*)this;
        widget_get_png(deleteEntry, LDVAR(quit_png));
        deleteEntry->scale.gravity = SOUTHWEST;
        deleteEntry->func.value_changed_callback = remove_entry_callback;

        LoadMenu = create_menu(loadPlayList,25);
        LoadMenu->parent_struct = (void*)this;
        LoadMenu->func.button_release_callback = load_playlist_callback;

        SaveMenu = create_menu(savePlayList,25);
        SaveMenu->parent_struct = (void*)this;
        SaveItem = menu_add_item(SaveMenu, "Save ");
        menu_add_item(SaveMenu, "Save as");
        SaveMenu->func.button_release_callback = save_playlist_callback;
    }

/****************************************************************
            PlayList - load a file from Play List
****************************************************************/

    // load next file from Play List, called from background thread,
    // triggered by audio server when end of current file is reached,
    // or triggered from dnd btw. a load file event (File browser)
    void loadFromPlayList() {
        if (((plist.Play_list.size() < 2) || !usePlayList) && !forceReload) {
            execute.store(true, std::memory_order_release);
            return;
        }
        playNow++;
        plist.lfile = plist.Play_list.begin()+playNow;
        if (plist.lfile >= plist.Play_list.end()) {
            plist.lfile = plist.Play_list.begin();
            playNow = 0;
        }
        if (!pre_load)
            preload_soundfile(std::get<1>(*plist.lfile).c_str(), true);
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        XLockDisplay(w->app->dpy);
        #endif
        forceReload = false;
        blockWriteToPlayList = true;
        listbox_set_active_entry(playList, playNow);
        
        read_soundfile(std::get<1>(*plist.lfile).c_str(), true);
        blockWriteToPlayList = false;
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        XFlush(w->app->dpy);
        XUnlockDisplay(w->app->dpy);
        #endif
        auto it = plist.lfile + 1;
        if (it >= plist.Play_list.end()) {
            it = plist.Play_list.begin();
        }
        preload_soundfile(std::get<1>(*it).c_str(), false);
        execute.store(true, std::memory_order_release);
    }

/****************************************************************
            PlayList - callbacks
****************************************************************/

    // add a file to the Play List
    void addToPlayList(void* fileName, bool laod) {
        plist.Play_list.push_back(std::tuple<std::string, std::string, uint32_t, uint32_t>(
            std::string(basename((char*)fileName)), std::string((const char*)fileName),
            0, (uint32_t)INT_MAX));
        if (!viewPlayList) createPlayListView(w->app);
        auto it = plist.Play_list.end()-1;
        listbox_add_entry(playList, std::get<0>(*it).c_str());
        if (laod) playNow = plist.Play_list.size()-1;
        Metrics_t metrics;
        os_get_window_metrics(playList, &metrics);
        if (metrics.visible) {
            if (laod) listbox_set_active_entry(playList, playNow);
            widget_show_all(playList);
        }
    }

    // remove a file from the Play List by given index
    void removeFromPlayList(int v) {
        if (!plist.Play_list.size()) return;
        plist.Play_list.erase(plist.Play_list.begin() + v);
        rebuildPlayList();
        forceReload = true;
        pre_load = false;
    }

    // move a file in the Play List from index to index
    void moveInPlayList(int from, int to) {
        if (!plist.Play_list.size()) return;
        plist.move(plist.Play_list, from, to);
        rebuildPlayList();
        forceReload = true;
        pre_load = false;
    }

    // callback from listbox that a file is to be moved
    static void listbox_move_callback(void *w_, void* button_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if(user_data !=NULL ) {
        XButtonEvent *xbutton = (XButtonEvent*)button_;
            int x1, y1;
            os_translate_coords(w, os_get_root_window(w->app, IS_WIDGET), 
                self->w->widget, xbutton->x_root, xbutton->y_root, &x1, &y1);
            int x2, y2;
            os_translate_coords(w, os_get_root_window(w->app, IS_WIDGET), 
                w->widget, xbutton->x_root, xbutton->y_root, &x2, &y2);
            int *v = static_cast<int*>(user_data);
            if (x1 > 0 && y1 > 0 && x1 < self->w->width && y1 < self->w->height) {
                self->playNow = *v > 0? *v-1 : self->plist.Play_list.size()-1;
                self->pre_load = false;
                self->forceReload = true;
                self->loadFile();
            } else if (x2 > 0 && y2 > 0 && x2 < w->width && y2 < w->height) {
                int to = max(0, min(static_cast<int>(self->plist.Play_list.size() -1), (max(1, y2)/25)));
                if (*v != to) self->moveInPlayList(*v, to);
            } else {
                self->removeFromPlayList(*v);
            }
        }
        
    }

    // handle drag and drop for the Play List window
    static void dnd_load_playlist(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if (!Pa_IsStreamActive(self->stream)) return;
        if (user_data != NULL) {
            self->blockWriteToPlayList = true;
            char* dndfile = NULL;
            dndfile = strtok(*(char**)user_data, "\r\n");
            while (dndfile != NULL) {
                if (self->supportedFormats.isSupported(dndfile) ) {
                    self->pre_load = false;
                    self->addToPlayList(dndfile, false);
                    self->forceReload = true;
                    if (self->plist.Play_list.size()<2)
                        self->loadFile();
                } else {
                    std::cerr << "Unrecognized file extension: " << dndfile << std::endl;
                }
                dndfile = strtok(NULL, "\r\n");
            }
            self->blockWriteToPlayList = false;
        }
    }

    // re-build the Play List when files was re-moved
    void rebuildPlayList() {
        listbox_remove_entrys(playList);
        for (auto it = plist.Play_list.begin(); it != plist.Play_list.end(); it++)
            listbox_add_entry(playList, std::get<0>(*it).c_str());
        listbox_set_active_entry(playList, playNow);
        Metrics_t metrics;
        os_get_window_metrics(playList, &metrics);
        if (metrics.visible) widget_show_all(viewPlayList);
    }

    // remove a entry from the Play List
    static void remove_entry_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            if (!self->plist.Play_list.size()) return;
            int remove = static_cast<int>(adj_get_value(self->playList->adj));
            self->removeFromPlayList(remove);
        }
    }

    // move a entry one place up in the Play List
    static void up_entry_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            if (!self->plist.Play_list.size()) return;
            int up = static_cast<int>(adj_get_value(self->playList->adj));
            if (!up) return;
            std::swap(self->plist.Play_list[up-1],self->plist.Play_list[up]);
            self->rebuildPlayList();
            self->pre_load = false;
        }
    }

    // move a entry one place down in the Play List
    static void down_entry_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            if (!self->plist.Play_list.size()) return;
            int down = static_cast<int>(adj_get_value(self->playList->adj));
            if (down > static_cast<int>(self->plist.Play_list.size()-1)) return;
            std::swap(self->plist.Play_list[down],self->plist.Play_list[down+1]);
            self->rebuildPlayList();
            self->pre_load = false;
        }
    }

    // load a saved Play List by a given name
    static void load_playlist_callback(void* w_, void* item_, void* data_) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        int v = *(int*)item_;
        self->plist.Play_list.clear();
        self->plist.load_PlayList(self->plist.PlayListNames[v].c_str());
        self->rebuildPlayList();
        self->currentPlayList = self->plist.PlayListNames[v];
        if (!self->plist.Play_list.size()) return;
        if (!self->af.samples) {
            self->plist.lfile = self->plist.Play_list.begin();
            self->playNow = self->plist.Play_list.size();
            if (self->usePlayList) {
                self->ready = false;
                self->loadFile();
            }
        } else {
            self->playNow = self->plist.Play_list.size()-1;
            self->pre_load = false;
            delete[] self->pre_af.samples;
            self->pre_af.samples = nullptr;
        }
    }

    // pop up a menu to select the Play List to load
    static void load_up_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && !adj_get_value(w->adj)){
            self->plist.read_PlayList();

            Widget_t *view_port = self->LoadMenu->childlist->childs[0];
            int i = view_port->childlist->elem;
            for(;i>-1;i--) {
                menu_remove_item(self->LoadMenu,view_port->childlist->childs[i]);
            }

            for (auto i = self->plist.PlayListNames.begin(); i != self->plist.PlayListNames.end(); i++) {
                menu_add_item(self->LoadMenu, (*i).c_str());
            }
            pop_menu_show(w, self->LoadMenu, 6, true);
        }
    }

    // save a Play List under the given name
    static void save_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if(user_data !=NULL && strlen(*(const char**)user_data)) {
            AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
            std::string lname(*(const char**)user_data);
            if (std::find(self->plist.PlayListNames.begin(),
                    self->plist.PlayListNames.end(), lname) != self->plist.PlayListNames.end()) {
                self->plist.remove_PlayList(lname);
                self->plist.save_PlayList(lname, true);
           } else {
                self->plist.save_PlayList(lname, true);
            }
        }
    }

    // pop up a text entry to enter a name for a Play List to save
    void save_as() {
        if (!plist.Play_list.size()) return;
        Widget_t* dia = showTextEntry(viewPlayList, 
                    "Playlist - save as:", "Save Play List as:");
        int x1, y1;
        os_translate_coords( viewPlayList, viewPlayList->widget, 
            os_get_root_window(w->app, IS_WIDGET), 0, 0, &x1, &y1);
        os_move_window(w->app->dpy,dia,x1+60, y1+16);
        viewPlayList->func.dialog_callback = save_response;
    }

    // save button callback
    static void save_as_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && !adj_get_value(w->adj)){
            if (!self->plist.Play_list.size()) return;
            self->save_as();
        }
    }

    // save menu callback
    static void save_playlist_callback(void* w_, void* item_, void* data_) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        int v = *(int*)item_;
        if (v == 0) {
            const char* f = self->currentPlayList.c_str();
            save_response(w, &f);
        }
        else if (v == 1) self->save_as();
    }

    // save button callback, check if we need to popup menu or textentry
    static void save_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && !adj_get_value(w->adj)){
            if (!self->plist.Play_list.size()) return;
            if (!self->currentPlayList.empty()) {
                self->newLabel = "Save as " + self->currentPlayList;
                self->SaveItem->label = self->newLabel.c_str();
                pop_menu_show(w, self->SaveMenu, 2, true);
            } else {
                save_as_callback(w, nullptr);
            }
        }
    }

/****************************************************************
                    save Sound File
****************************************************************/

    // save a loop to file
    static void write_soundfile(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if(user_data !=NULL && strlen(*(const char**)user_data)) {
            AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
            if (!self->af.samples) return;
            std::string lname(*(const char**)user_data);
            self->af.saveAudioFile(lname, self->loopPoint_l, self->loopPoint_r, self->jack_sr);
        }
    }

/****************************************************************
                    Sound File loading
****************************************************************/

    // when Sound File loading fail, clear wave view and reset tittle
    void failToLoad() {
        loadNew = true;
        update_waveview(wview, af.samples, af.samplesize);
        widget_set_title(w_top, "alooper");
    }

    // pre-load a Sound File on demand
    void preload_soundfile(const char* file, bool block_play = false) {
        if (block_play) ready = false;
        pre_load = pre_af.getAudioFile(file, jack_sr);
    }

    // load a Sound File when pre-load is the wrong file
    void load_soundfile(const char* file) {
        af.channels = 0;
        af.samplesize = 0;
        af.samplerate = 0;
        position = 0;

        if (Pa_IsStreamActive(stream)) {
            std::unique_lock<std::mutex> lk(WMutex);
            SyncWait->wait_for(lk, std::chrono::milliseconds(60));
        }

        ready = false;
        is_loaded = af.getAudioFile(file, jack_sr);
        if (!is_loaded) failToLoad();
    }

    // load Sound File data into memory
    void read_soundfile(const char* file, bool haveLoopPoints = false) {
        if (!pre_load) {
            if (!is_loaded) {
                load_soundfile(file);
                is_loaded = false;
            }
        } else {
            af.channels = 0;
            af.samplesize = 0;
            af.samplerate = 0;
            position = 0;
            if (Pa_IsStreamActive(stream)) {
                std::unique_lock<std::mutex> lk(WMutex);
                SyncWait->wait_for(lk, std::chrono::milliseconds(60));
            }
            ready = false;
            delete[] af.samples;
            af.samples = nullptr;
            af.samples = pre_af.samples;
            pre_af.samples = nullptr;
            af.channels = pre_af.channels;
            af.samplesize = pre_af.samplesize;
            af.samplerate = pre_af.samplerate;
            pre_load = false;
            
        }
        loadNew = true;
        if (af.samples) {
            adj_set_max_value(wview->adj, (float)af.samplesize);
            //adj_set_max_value(loopMark_L->adj, (float)af.samplesize*0.5);
            adj_set_state(loopMark_L->adj, 0.0);
            loopPoint_l = 0;
            //adj_set_max_value(loopMark_R->adj, (float)af.samplesize*0.5);
            adj_set_state(loopMark_R->adj,1.0);
            loopPoint_r = af.samplesize;
            if (haveLoopPoints) {
                if (std::get<3>(*plist.lfile) > af.samplesize)
                    std::get<3>(*plist.lfile) = af.samplesize;
            }
            
            update_waveview(wview, af.samples, af.samplesize);
            char name[256];
            strncpy(name, file, 255);
            widget_set_title(w_top, basename(name));
        } else {
            af.samplesize = 0;
            std::cerr << "Error: could not resample file" << std::endl;
            failToLoad();
        }
        if (playBackwards) position = af.samplesize;
        if (haveLoopPoints) setLoopPoints();
        ready = true;
    }

    // set the loop points for a new loaded file
    void setLoopPoints() {
        float point_l = static_cast<float>(std::get<2>(*plist.lfile));
        float upper_l = static_cast<float>(af.samplesize*0.5);
        float point_r = static_cast<float>(std::get<3>(*plist.lfile) - upper_l);
        position = std::get<2>(*plist.lfile)+1;
        loopPoint_l = std::get<2>(*plist.lfile);
        loopPoint_r = std::get<3>(*plist.lfile);
        adj_set_state(loopMark_L->adj, point_l/upper_l);
        adj_set_state(loopMark_R->adj, point_r/upper_l);
    }

/****************************************************************
            drag and drop handling for the main window
****************************************************************/

    static void dnd_load_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if (!Pa_IsStreamActive(self->stream)) return;
        if (user_data != NULL) {
            self->blockWriteToPlayList = true;
            char* dndfile = NULL;
            dndfile = strtok(*(char**)user_data, "\r\n");
            while (dndfile != NULL) {
                if (self->supportedFormats.isSupported(dndfile) ) {
                    self->pre_load = false;
                    self->forceReload = true;
                    self->addToPlayList(dndfile, true);
                    self->playNow = self->plist.Play_list.size() -2;
                    self->loadFile();
                    break;
                } else {
                    std::cerr << "Unrecognized file extension: " << dndfile << std::endl;
                }
                dndfile = strtok(NULL, "\r\n");
            }
            self->blockWriteToPlayList = false;
        }
    }

/****************************************************************
            Play head (called from timeout thread) 
****************************************************************/

    static void dummy_callback(void *w_, void* user_data) {}

    // frequently (60ms) update the wave view widget for playhead position
    // triggered from the timeout background thread 
    void updateUI() {
        static int waitOne = 0;
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        XLockDisplay(w->app->dpy);
        #endif
        wview->func.adj_callback = dummy_callback;
        if (ready) adj_set_value(wview->adj, (float) position);
        else {
            waitOne++;
            if (waitOne > 2) {
                transparent_draw(wview, nullptr);
                waitOne = 0;
            }
        }
        expose_widget(wview);
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        XFlush(w->app->dpy);
        XUnlockDisplay(w->app->dpy);
        #endif
        wview->func.adj_callback = transparent_draw;
    }

/****************************************************************
                      Button callbacks 
****************************************************************/

    // quit
    static void button_quit_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if (w->flags & HAS_POINTER && !*(int*)user_data){
            self->onExit();
        }
    }

    #if defined(_WIN32)
    void getWindowDecorationSize(int *width, int *height) {
        DWORD dwStyle = WS_OVERLAPPEDWINDOW;
        DWORD dwExStyle = WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES;
        RECT Rect = {0};
        BOOL bMenu = false;
        Rect.right = *width;
        Rect.bottom = *height;
        if (AdjustWindowRectEx(&Rect, dwStyle, bMenu, dwExStyle)) {
            *width = Rect.right - Rect.left;
            *height = Rect.bottom - Rect.top;
        }
       
    }
    #else
    void getWindowDecorationSize(int *width, int *height) {
        Atom type;
        int format;
        unsigned long  count = 0, remaining;
        unsigned char* data = 0;
        long* extents;
        Atom _NET_FRAME_EXTENTS = XInternAtom(w_top->app->dpy, "_NET_FRAME_EXTENTS", True);
        XGetWindowProperty(w_top->app->dpy, w_top->widget, _NET_FRAME_EXTENTS,
            0, 4, False, AnyPropertyType,&type, &format, &count, &remaining, &data);
        extents = (long*) data;
        *height = - static_cast<int>(extents[2])/2;
        *width = - static_cast<int>(extents[0])/2;
    }
    #endif

    // toggle pause button with space bar
    static void key_press(void *w_, void *key_, void *user_data) {
        Widget_t *w = (Widget_t*)w_;
        if (!w) return;
        XKeyEvent *key = (XKeyEvent*)key_;
        if (!key) return;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if (key->keycode == XKeysymToKeycode(w->app->dpy, XK_space)) {
            adj_set_value(self->paus->adj, !adj_get_value(self->paus->adj));
            self->play = adj_get_value(self->paus->adj) ? false : true;
        } else if (key->keycode == XKeysymToKeycode(w->app->dpy, XK_Left)) {
            adj_set_value(self->backwards->adj, !adj_get_value(self->backwards->adj));
            self->playBackwards = adj_get_value(self->backwards->adj) ? true : false;
        } else if (key->keycode == XKeysymToKeycode(w->app->dpy, XK_q)) {
            self->onExit();
        } else if ((key->state & ControlMask) && (key->keycode == XKeysymToKeycode(w->app->dpy, XK_plus))) {
            int x = 1;
            int y = 1;
            #if defined(_WIN32)
            self->getWindowDecorationSize(&x, &y);
            #endif
            os_resize_window(self->w->app->dpy, self->w, self->w->width + x, self->w->height + y);
            expose_widget(self->w);
        } else if ((key->state & ControlMask) && (key->keycode == XKeysymToKeycode(w->app->dpy, XK_minus))) {
            int x = 1;
            int y = 1;
            #if defined(_WIN32)
            self->getWindowDecorationSize(&x, &y);
            #endif
            os_resize_window(self->w->app->dpy, self->w, self->w->width + x -2, self->w->height + y -2);
            expose_widget(self->w);
        }
    }

    static void button_expand_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            if (!self->viewPlayList) self->createPlayListView(self->w->app);
            self->rebuildPlayList();
            int x = 0;
            int y = 0;
            int x1 = 0;
            int y1 = 0;
            #if defined(_WIN32)
            self->getWindowDecorationSize(&x, &y);
            x1 = x;
            y1 = y;
            #else
            self->getWindowDecorationSize(&x1, &y1);
            #endif
            os_translate_coords(self->w_top, self->w_top->widget, 
                 os_get_root_window(w->app, IS_WIDGET), x1, y1, &x1, &y1);
            self->w_top->height += 340;
            self->w_top->scale.init_height +=340;
            os_set_window_min_size(self->w_top, 200, 170 /2 + 340, self->w_top->width + x, self->w_top->height + y);
            os_resize_window(self->w->app->dpy, self->w_top, self->w_top->width + x, self->w_top->height + y);
            os_move_window(self->w->app->dpy, self->w_top, x1, y1);
            widget_show_all(self->viewPlayList);
        } else {
            int x = 0;
            int y = 0;
            int x1 = 0;
            int y1 = 0;
            #if defined(_WIN32)
            self->getWindowDecorationSize(&x, &y);
            x1 = x;
            y1 = y;
            #else
            self->getWindowDecorationSize(&x1, &y1);
            #endif
            os_translate_coords(self->w_top, self->w_top->widget,
                os_get_root_window(w->app, IS_WIDGET), x1, y1, &x1, &y1);
            self->w_top->height -= 340;
            self->w_top->scale.init_height -= 340;
            os_set_window_min_size(self->w_top, 200, 170/2, self->w_top->width + x , self->w_top->height + y);
            os_resize_window(self->w->app->dpy, self->w_top, self->w_top->width + x , self->w_top->height + y);
            os_move_window(self->w->app->dpy, self->w_top, x1, y1);
            widget_hide(self->viewPlayList);
        }
        transparent_draw(self->w, nullptr);
        //expose_widget(self->w_top);
        expose_widget(self->w_top);
    }

    // pause
    static void button_pause_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            self->play = false;
        } else self->play = true;
    }

    // play backwards
    static void button_backwards_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            self->playBackwards = true;
        } else self->playBackwards = false;
    }

    // move playhead to start position 
    static void button_backset_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && !adj_get_value(w->adj)){
            self->position = 0;
        }
    }

    // set left loop point by mouse move/scroll
    static void slider_l_changed_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        float st = adj_get_state(w->adj);
        uint32_t lp = (self->af.samplesize *0.5) * st;
        if (lp > self->position) {
            lp = self->position;
            st = max(0.0, min(1.0, self->position/(self->af.samplesize*0.5)));
        }
        adj_set_state(w->adj, st);
        self->loopPoint_l = lp;
        if (!self->plist.Play_list.size()) return;
        if (w->flags & HAS_POINTER && !self->blockWriteToPlayList) {
            std::get<2>(*(self->plist.Play_list.begin()+self->playNow)) = self->loopPoint_l;
        }
    }

    // set left loop point by mouse right click
    static void slider_l_released(void *w_, void* xbutton_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        XButtonEvent *xbutton = (XButtonEvent*)xbutton_;
        if (w->flags & HAS_POINTER) {
            if(xbutton->state & Button3Mask) {
                Metrics_t metrics;
                os_get_window_metrics(w, &metrics);
                int width = metrics.width;
                int x = xbutton->x;
                float st = max(0.0, min(1.0, static_cast<float>((float)x/(float)width)));
                uint32_t lp = (self->af.samplesize *0.5) * st;
                if (lp > self->position) {
                    lp = self->position;
                    st = max(0.0, min(1.0, self->position/(self->af.samplesize*0.5)));
                }
                adj_set_state(w->adj, st);
                self->loopPoint_l = lp;
                if (!self->plist.Play_list.size()) return;
                std::get<2>(*(self->plist.Play_list.begin()+self->playNow)) = self->loopPoint_l;
            }
        }
        expose_widget(w);
    }

    // set right loop point by mouse move/scroll
    static void slider_r_changed_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        float st = adj_get_state(w->adj);
        uint32_t lp = (self->af.samplesize *0.5) + ((self->af.samplesize*0.5) * st);
        if (lp < self->position) {
            lp = self->position;
            st = max(0.0, min(1.0, (self->position - (self->af.samplesize*0.5))/(self->af.samplesize*0.5)));
        }
        adj_set_state(w->adj, st);
        self->loopPoint_r = lp;
        if (!self->plist.Play_list.size()) return;
        if (w->flags & HAS_POINTER && !self->blockWriteToPlayList) {
            std::get<3>(*(self->plist.Play_list.begin()+self->playNow)) = self->loopPoint_r;
        }
    }

    // set right loop point by mouse right click
    static void slider_r_released(void *w_, void* xbutton_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        XButtonEvent *xbutton = (XButtonEvent*)xbutton_;
        if (w->flags & HAS_POINTER) {
            if(xbutton->state & Button3Mask) {
                Metrics_t metrics;
                os_get_window_metrics(w, &metrics);
                int width = metrics.width;
                int x = xbutton->x;
                float st = max(0.0, min(1.0, static_cast<float>((float)x/(float)width)));
                uint32_t lp = (self->af.samplesize *0.5) + ((self->af.samplesize*0.5) * st);
                if (lp < self->position) {
                    lp = self->position;
                    st = max(0.0, min(1.0, (self->position - (self->af.samplesize*0.5))/(self->af.samplesize*0.5)));
                }
                adj_set_state(w->adj, st);
                self->loopPoint_r = lp;
                if (!self->plist.Play_list.size()) return;
                std::get<3>(*(self->plist.Play_list.begin()+self->playNow)) = self->loopPoint_r;
            }
        }
        expose_widget(w);
    }

    // set playhead position to mouse pointer
    static void set_playhead(void *w_, void* xbutton_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        XButtonEvent *xbutton = (XButtonEvent*)xbutton_;
        if (w->flags & HAS_POINTER) {
            if(xbutton->state & Button1Mask) {
                Metrics_t metrics;
                os_get_window_metrics(w, &metrics);
                int width = metrics.width;
                int x = xbutton->x;
                float st = max(0.0, min(1.0, static_cast<float>((float)x/(float)width)));
                uint32_t lp = adj_get_max_value(w->adj) * st;
                if (lp > self->loopPoint_r) lp = self->loopPoint_r;
                if (lp < self->loopPoint_l) lp = self->loopPoint_l;
                self->position = lp;
            }
        }
    }

    // show the Play List window
    static void button_lview_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        if ((w->flags & HAS_POINTER) && adj_get_value(w->adj)){
            if (!self->viewPlayList) {
                self->createPlayListView(self->w->app);
                self->rebuildPlayList();
            }
            //int x1, y1;
           // os_translate_coords( self->w, self->w->widget, 
            //    os_get_root_window(self->w->app, IS_WIDGET), 0, 0, &x1, &y1);
            //widget_show_all(self->viewPlayList);
            //os_move_window(self->w->app->dpy,self->viewPlayList,x1, y1+16+self->w->height);
            self->usePlayList = true;
            if (!self->af.samples && self->plist.Play_list.size()) {
                self->ready = false;
                self->plist.lfile = self->plist.Play_list.begin();
                self->playNow = self->plist.Play_list.size();
                self->loadFile();
            }

        } else {
            //widget_hide(self->viewPlayList);
            self->usePlayList = false;
        }
    }

    // volume control
    static void volume_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        self->gain = std::pow(1e+01, 0.05 * adj_get_value(w->adj));
    }

/****************************************************************
                      drawings 
****************************************************************/

    static void draw_l_slider(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;
        float center = (float)height/2;
        float upcenter = (float)height;
        
        float sliderstate = adj_get_state(w->adj_x);

        use_fg_color_scheme(w, get_color_state(w));
        float point = 5.0 + ((width - 5.0) * sliderstate);
        cairo_move_to (w->crb, point - 5.0, center);
        cairo_line_to(w->crb, point + 5.0, center);
        cairo_line_to(w->crb, point , upcenter);
        cairo_line_to(w->crb, point - 5.0 , center);
        cairo_fill(w->crb);
    }

    static void draw_r_slider(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;
        float center = (float)height/2;
        float upcenter = (float)height;
        
        float sliderstate = adj_get_state(w->adj_x);

        use_fg_color_scheme(w, get_color_state(w));
        float point =  ((width - 5.0) * sliderstate);
        cairo_move_to (w->crb, point - 5.0, center);
        cairo_line_to(w->crb, point + 5.0, center);
        cairo_line_to(w->crb, point , upcenter);
        cairo_line_to(w->crb, point - 5.0 , center);
        cairo_fill(w->crb);
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

        if (wave_view->size<1 || !ready) return;
        int step = (wave_view->size/width)/af.channels;
        float lstep = (float)(half_height_t)/af.channels;
        cairo_set_line_width(cri,2);
        cairo_set_source_rgba(cri, 0.55, 0.65, 0.55, 1);

        int pos = half_height_t/af.channels;
        for (int c = 0; c < (int)af.channels; c++) {
            cairo_pattern_t *pat = cairo_pattern_create_linear (0, pos, 0, height);
            cairo_pattern_add_color_stop_rgba
                (pat, 0,1.53,0.33,0.33, 1.0);
            cairo_pattern_add_color_stop_rgba
                (pat, 0.7,0.53,0.33,0.33, 1.0);
            cairo_pattern_add_color_stop_rgba
                (pat, 0.3,0.33,0.53,0.33, 1.0);
            cairo_pattern_add_color_stop_rgba
                (pat, 0, 0.55, 0.55, 0.55, 1.0);
            cairo_pattern_set_extend(pat, CAIRO_EXTEND_REFLECT);
            cairo_set_source(cri, pat);
            for (int i=0;i<width-4;i++) {
                cairo_move_to(cri,i+2,pos);
                float w = wave_view->wave[int(c+(i*af.channels)*step)];
                cairo_line_to(cri, i+2,(float)(pos)+ (-w * lstep));
                cairo_line_to(cri, i+2,(float)(pos)+ (w * lstep));
            }
            pos += half_height_t;
            cairo_pattern_destroy (pat);
            pat = nullptr;
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
        static bool clearImage = false;
        static bool clearImageDone = false;
        if (!self->ready && !clearImageDone) clearImage = true;
        if (w->image) {
            os_get_surface_size(w->image, &width, &height);
            if (((width != width_t || height != height_t) || self->loadNew) && self->ready) {
                self->loadNew = false;
                clearImageDone = false;
                self->create_waveview_image(w, width_t, height_t);
                os_get_surface_size(w->image, &width, &height);
            }
        } else {
            self->create_waveview_image(w, width_t, height_t);
            os_get_surface_size(w->image, &width, &height);
        }
        if (clearImage) {
            clearImage = false;
            clearImageDone = true;
            self->create_waveview_image(w, width_t, height_t);
            os_get_surface_size(w->image, &width, &height);
        }
        cairo_set_source_surface (w->crb, w->image, 0, 0);
        cairo_rectangle(w->crb,0, 0, width, height);
        cairo_fill(w->crb);

        double state = adj_get_state(w->adj);
        cairo_set_source_rgba(w->crb, 0.55, 0.05, 0.05, 1);
        cairo_rectangle(w->crb, (width * state) - 1.5,2,3, height-4);
        cairo_fill(w->crb);

        int halfWidth = width*0.5;

        double state_l = adj_get_state(self->loopMark_L->adj);
        cairo_set_source_rgba(w->crb, 0.25, 0.25, 0.05, 0.666);
        cairo_rectangle(w->crb, 0, 2, (halfWidth*state_l), height-4);
        cairo_fill(w->crb);

        double state_r = adj_get_state(self->loopMark_R->adj);
        cairo_set_source_rgba(w->crb, 0.25, 0.25, 0.05, 0.666);
        int point = halfWidth + (halfWidth*state_r);
        cairo_rectangle(w->crb, point, 2 , width - point, height-4);
        cairo_fill(w->crb);
        if (!self->ready) 
            show_spinning_wheel(w, nullptr);

    }

    static void drawWheel(Widget_t *w, float di, int x, int y, int radius, float s) {
        cairo_set_line_width(w->crb,10 / w->scale.ascale);
        cairo_set_line_cap (w->crb, CAIRO_LINE_CAP_ROUND);
        int i;
        const int d = 1;
        for (i=375; i<455; i++) {
            double angle = i * 0.01 * 2 * M_PI;
            double rx = radius * sin(angle);
            double ry = radius * cos(angle);
            double length_x = x - rx;
            double length_y = y + ry;
            double radius_x = x - rx * s ;
            double radius_y = y + ry * s ;
            double z = i/420.0;
            if ((int)di < d) {
                cairo_set_source_rgba(w->crb, 0.66*z, 0.66*z, 0.66*z, 0.3);
                cairo_move_to(w->crb, radius_x, radius_y);
                cairo_line_to(w->crb,length_x,length_y);
                cairo_stroke_preserve(w->crb);
            }
            di++;
            if (di>8.0) di = 0.0;
       }
    }

    static void show_spinning_wheel(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;
        AudioLooperUi *self = static_cast<AudioLooperUi*>(w->parent_struct);
        static const float sCent = 0.666;
        static float collectCents = 0;
        collectCents -= sCent;
        if (collectCents>8.0) collectCents = 0.0;
        else if (collectCents<0.0) collectCents = 8.0;
        self->drawWheel (w, collectCents,width*0.5, height*0.5, height*0.3, 0.98);
        cairo_stroke(w->crb);
    }

    static void boxShadowOutset(cairo_t* const cr, int x, int y, int width, int height, bool fill) {
        float step = static_cast<float>(17.0/height);
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
            (pat, step,0.33 * 0.6,0.33 * 0.6,0.33 * 0.6, 0.0);
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
        Widget_t *p = (Widget_t*)w->parent;
        Metrics_t metrics;
        os_get_window_metrics(p, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;
        int x1, y1;
        os_translate_coords(w, p->widget, w->widget, 0, 0, &x1, &y1);


        cairo_pattern_t *pat;
        pat = cairo_pattern_create_linear (x1, y1, width , height);
        cairo_pattern_add_color_stop_rgba (pat, 1, 0.2, 0.2, 0.2, 1);
        cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0., 1);
        //cairo_rectangle(w->crb,0,0,width,height);
        cairo_set_source (w->crb, pat);
        cairo_paint (w->crb);

        boxShadowOutset(w->crb, x1, y1, width , height, false);
        //cairo_fill (w->crb);
        cairo_pattern_destroy (pat);
    }

};

#endif

