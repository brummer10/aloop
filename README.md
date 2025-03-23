# aloop

<p align="center">
    <img src="https://github.com/brummer10/aloop/blob/main/alooper.png?raw=true" />
</p>

aloop is a audio file looper for Linux using PortAudio as backend (jack, pulse, alsa), 
libsndfile to load sound files and zita-resampler to resample the files when needed.
The GUI is created with libxputty.

## Features

- support all file formats supported by libsndfile.
- resample files on load to match session Sample Rate
- file loading by drag n' drop
- included file browser
- open file directly in a desktop file browser
- open file on command-line
- create, sort, save and load playlists
- select to loop over a single file or over the play list
- move play-head to mouse position in wave view
- set loop points for start/end loop
- save loop points in play list
- save selected loop as wav file
- play backwards
- volume control
- endless looping
- break playback (keyboard support space bar)
- reset play-head to start position (keyboard support courser left)

## Dependencies

- libsndfile1-dev
- portaudio19-dev
- libcairo2-dev
- libx11-dev
- librubberband-dev

## Building from source code

```shell
git clone https://github.com/brummer10/aloop.git
cd aloop
git submodule update --init --recursive
make
sudo make install # will install into /usr/bin
```
