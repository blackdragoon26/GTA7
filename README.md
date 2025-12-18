![Build](https://github.com/yourname/GTA7/workflows/Build%20macOS/badge.svg)

something I always wanted to do, raw game , inspired by [OG](https://youtube.com/@tokyospliff?si=MAT-5_LDeemosoXZ)

## specs
- Language: C++17
Graphics API: OpenGL 3.3 Core Profile
- Windowing & Input: GLFW 3.4 (bundled via CMake FetchContent)
- OpenGL Loader: GLAD (pre-generated, mx=ON, loader=ON)
- Math Library: GLM 1.0.1 (bundled via CMake FetchContent)
- Audio Engine: miniaudio (single-header, MP3 support enabled)
- Build System: CMake (fully self-contained, no system dependencies)


### running it

```
cd GTA7
```

```
mkdir build && cd build
```

```
cmake ..
```

```
cmake --build . --parallel
```

```
./GTA7        # macOS/Linux
GTA7.exe      # Windows
```

### game stuff

- Infinite procedural terrain
- Perlin-like noise (3 octaves)
- Textured by height (sand, grass, rock)
- Chunked loading/unloading (32×32 tiles, 2.0f scale)
- Driveable car physics
- Acceleration, braking, friction, max speed
- Smooth steering with rotation
- Terrain-following (y = terrain height + 0.5)
- Dynamic third-person camera
- Follows car with offset
- Adjustable pitch/yaw (currently fixed, but extensible)
- Engine sound
- enginesound.mp3 plays on W/S press
- Smooth volume fade-in/out (exponential easing)
- Loops seamlessly