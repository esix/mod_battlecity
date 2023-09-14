# mod_battlecity

Battle City game as FreeSWITCH module. Players make video call to extension 9999 and play game using DTMF keys.

## How it works

1. User calls extension 9999 (video call)
2. FreeSWITCH starts game module
3. Game rendered to 320x240 RGB canvas
4. GStreamer encodes to H.264 and sends as video
5. Player controls tank with DTMF (2=up, 8=down, 4=left, 6=right, 5=fire)

## Build

Need FreeSWITCH source tree and GStreamer 1.0 dev libraries.

Copy mod_battlecity into `src/mod/applications/mod_battlecity` in FreeSWITCH source, add to `build/modules.conf.in`:
```
applications/mod_battlecity
```

Build FreeSWITCH as usual, module will be compiled.

## Docker

```bash
docker build -t battlecity .
docker run --rm --network host battlecity
```

## Standalone test

```bash
g++ -std=c++11 -o standalone_test standalone_test.cpp renderer.cpp world.cpp \
    $(pkg-config --cflags --libs gstreamer-1.0) -lpthread
./standalone_test
```

Controls: wasd = move, space = fire, q = quit

## License

MIT
