# BlueBoundEngine
Real-time Bounding of Sonic The Hedgehog Retro Games.

## Build

To obtain a development build use the following command-line string:

```sh
g++ -Wall -Wformat -O0 -g main.cpp -o test.bin -lX11
```

## Run

Before doing this step you need to be running a retro sonic game and gets the resource
ID of the window where it is running it.

```sh
xwininfo
```

```90177537
xwininfo: Please select the window about which you
          would like information by clicking the
          mouse in that window.

xwininfo: Window id: 0x5600001 "GNU/Linux-Xlib-Game-Dev"

  Absolute upper-left X:  0
  Absolute upper-left Y:  12
  Relative upper-left X:  0
  Relative upper-left Y:  24
  Width: 2560
  Height: 1440
  Depth: 24
  Visual: 0x21
  Visual Class: TrueColor
  Border width: 0
  Class: InputOutput
  Colormap: 0x20 (installed)
  Bit Gravity State: ForgetGravity
  Window Gravity State: NorthWestGravity
  Backing Store State: NotUseful
  Save Under State: no
  Map State: IsViewable
  Override Redirect State: no
  Corners:  +0+12  -0+12  -0--12  +0--12
  -geometry 2560x1440+0+-12
```

based on this we know that the window resource ID is `90177537` (or `0x5600001`)

now you can run the engine by providing this way:

```sh
./test.bin --window 90177537
```

In a future revision we could modify the engine so that you won't need to convert
the resource id to decimal.
