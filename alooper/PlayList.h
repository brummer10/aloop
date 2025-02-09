
/*
 * PlayList.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */

#include <vector>
#include <cstring>
#include <tuple>
#include <climits>
#include <fstream>
#include <iostream>

#pragma once

#ifndef PLAYLIST_H
#define PLAYLIST_H

/****************************************************************
    class PlayList  -  store audio files with loop points in a Play List,
                       load Play Lists from config file
                       store Play Lists to config file
****************************************************************/

class PlayList
{
public:
    std::vector<std::tuple< std::string, std::string, uint32_t, uint32_t> > Play_list;
    std::vector<std::tuple< std::string, std::string, uint32_t, uint32_t> >::iterator lfile;
    std::vector<std::string> PlayListNames;

    PlayList(std::string configFile) { 
         if (getenv("XDG_CONFIG_HOME")) {
            std::string path = getenv("XDG_CONFIG_HOME");
            config_file = path + "/" + configFile + "-" + ALVER + ".conf";
        } else {
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
            std::string path = getenv("HOME");
            config_file = path +"/.config/" + configFile + "-" + ALVER + ".conf";
        #else
            std::string path = getenv("APPDATA");
            config_file = path +"\\.config\\" + configFile + "-" + ALVER + ".conf";
        #endif
       }
        
    };

    ~PlayList() {
        Play_list.clear();
        PlayListNames.clear();
    };

    // remove key from line
    std::string remove_sub(std::string a, std::string b) {
        std::string::size_type fpos = a.find(b);
        if (fpos != std::string::npos )
            a.erase(a.begin() + fpos, a.begin() + fpos + b.length());
        return (a);
    }

    // remove a Play List from the config file
    void remove_PlayList(std::string LoadName) {
        std::ifstream infile(config_file);
        std::ofstream outfile(config_file + "temp");
        std::string line;
        std::string key;
        std::string value;
        std::string ListName;
        if (infile.is_open() && outfile.is_open()) {
            while (std::getline(infile, line)) {
                bool save = true;
                std::istringstream buf(line);
                buf >> key;
                buf >> value;
                if (key.compare("[PlayList]") == 0) ListName = remove_sub(line, "[PlayList] ");
                if (ListName.compare(LoadName) == 0) {
                    save = false;
                    if (key.compare("[File]") == 0) {
                        save = false;
                    } else if (key.compare("[LoopPointL]") == 0) {
                        save = false;
                    } else if (key.compare("[LoopPointR]") == 0) {
                        save = false;
                    }
                }
                if (save) outfile << line<< std::endl;
                key.clear();
                value.clear();
            }
        infile.close();
        outfile.close();
        std::remove(config_file.c_str());
        std::rename((config_file + "temp").c_str(), config_file.c_str());
        }
    }

    // save a Play List to the config file
    void save_PlayList(std::string lname, bool append) {
        std::ofstream outfile(config_file, append ? std::ios::app : std::ios::trunc);
        if (outfile.is_open()) {
            outfile << "[PlayList] "<< lname << std::endl;
            for (auto i = Play_list.begin(); i != Play_list.end(); i++) {
                outfile << "[File] "<< std::get<1>(*i) << std::endl;
                outfile << "[LoopPointL] "<< std::get<2>(*i) << std::endl;
                outfile << "[LoopPointR] "<< std::get<3>(*i) << std::endl;
            }
        }
        outfile.close();
    }

    // load a Play List by given name
    void load_PlayList(std::string LoadName) {
        std::ifstream infile(config_file);
        std::string line;
        std::string key;
        std::string value;
        std::string ListName;
        std::string fileName;
        uint32_t lPointL = 0;
        uint32_t lPointR = INT_MAX;
        if (infile.is_open()) {
            while (std::getline(infile, line)) {
                std::istringstream buf(line);
                buf >> key;
                buf >> value;
                if (key.compare("[PlayList]") == 0) ListName = remove_sub(line, "[PlayList] ");
                if (ListName.compare(LoadName) == 0) {
                    if (key.compare("[File]") == 0) {
                        fileName = remove_sub(line, "[File] ");
                    } else if (key.compare("[LoopPointL]") == 0) {
                        lPointL = std::stoi(value);
                    } else if (key.compare("[LoopPointR]") == 0) {
                        lPointR = std::stoi(value);
                        Play_list.push_back(std::tuple<std::string, std::string, uint32_t, uint32_t>(
                            std::string(basename((char*)fileName.c_str())), fileName, lPointL, lPointR));
                    }
                }
                key.clear();
                value.clear();
            }
        }
        infile.close();
    }

    // get the Play List names form file
    void readPlayList() {
        std::ifstream infile(config_file);
        std::string line;
        std::string key;
        std::string value;
        std::string ListName;
        if (infile.is_open()) {
            PlayListNames.clear();
            while (std::getline(infile, line)) {
                std::istringstream buf(line);
                buf >> key;
                buf >> value;
                if (key.compare("[PlayList]") == 0) PlayListNames.push_back(remove_sub(line, "[PlayList] "));
                key.clear();
                value.clear();
            }
        }
        infile.close();   
    }

private:
    std::string config_file;

};

#endif
