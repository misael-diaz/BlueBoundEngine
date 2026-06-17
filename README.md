# BlueBoundEngine
Real-time Detection of Sonic The Hedgehog Retro Games.

**Development Status**: Under active development, currently writing exploratory code to reach a proof of concept level.

### Backstory and Motivation

During the Caribe Tech Hackathon I discovered that despite the rise of AI generated code there are developers that still have a genuine interest in learning how computers work. They are as eager to follow the latest trends in development with AI while having great interest to learn low-level programming.

I was talking to one of the mentors of the event about game development and he talked about his experience in hackathons at a time where you are expected to write the code yourself. That eventually brought me to talk about my own game engine and he said that it would be nice if I could prepare a talk about interfacing low-level code written in C/C++ with Python. I gladly accepted the request to give a talk about that in the next local Python conference.

So instead of preparing a talk about pointers that probably has been done many times I decided to first build an engaging demo, and there's nothing more relatable and engaging than a video game. Now instead of talking about building a game engine that can be called from Python, it is more practical to write a Computer Vision engine that detects where the player is. Even though everyone enjoys games, it is better to showcase a demo that has broader applications such as text and pattern recognition.

## Architecture Considerations

Why talking about interfacing with C/C++ instead of Rust? Because I like to have a good idea of how the code I am writing maps to assembly and that is starkly difficult with Rust but maneable otherwise with C/C++ (as long you don't overdo it with templates). And why interface with C/C++ at all? For many reasons really. If you are developing code in a zero trust environment, you cannot just pull code from any open-source project without jeopardizing the security of the application and the organization. You are expected to write it yourself. Now the moment that you have to introduce branching logic and/or loops you know that the code won't scale unless you can express it entirely with numpy arrays, which is the way of writing performant code natively from Python without having to worry about managing memory yourself which is the core strength of Python.

When you know that you can write code that executes close to the bare metal you can leverage that skill to boost your Python application. I know this is not new many developers have done this and there are probably many approaches you could take but my opinion is if you can avoid altogether the passing of data at the boundary entirely do that. But if you have and you own the C/C++ code you can do it so that all that you need is a pointer to the base memory address where the engine will work on. And this is what I am going to focus on this project. Now this repository will only host the C/C++ engine code, it does not matter from where you call it because all that it needs is a memory address.

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
