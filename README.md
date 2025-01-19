# aloop

<p align="center">
    <img src="https://github.com/brummer10/aloop/blob/main/alooper.png?raw=true" />
</p>

aloop is a audio file looper for JackAudioConnectionKit. It support all file formates supported by
libsndfile. Files could be load by drag n' drop them into the aloop GUI, or by select them with the
included file browser. 


## Dependencies

- libsndfile1-dev
- libcairo2-dev
- libx11-dev

## Building from source code

```shell
git clone https://github.com/brummer10/aloop.git
cd aloop
git submodule update --init --recursive
make
sudo make install # will install into /usr/bin
```
