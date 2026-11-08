# Third-Party Notices

This project contains or uses the following third-party components. The
notices below apply to those components; the project itself remains under the
MIT license in `LICENSE`.

## Bundled source

### ImGuiColorTextEdit

Files: `editor/src/TextEditor/TextEditor.h`, `TextEditor.cpp`

MIT License

Copyright (c) 2017 BalazsJako

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### miniaudio

File: `engine/include/GaleshiEngine/Audio/miniaudio.h`

miniaudio by David Reid is available under either the Unlicense (public
domain) or MIT-0. The complete license text is embedded at the end of the
header.

### stb_image

File: fetched as part of the bgfx/bimg dependency tree and included by the
engine for image decoding.

stb_image is released into the public domain, with an optional MIT license.
The complete notice is embedded in the upstream `stb_image.h`.

### DejaVu Sans

See `engine/assets/fonts/LICENSE-DejaVu.txt`.

## Build-time dependencies

The following libraries are fetched by CMake and retain their own licenses in
their source distributions:

| Component | License |
| --- | --- |
| SDL3 | zlib |
| bgfx, bimg, bx | BSD-2-Clause |
| Dear ImGui | MIT |
| ImGuizmo | MIT |
| GLM | MIT |
| Assimp | BSD-3-Clause |
| Jolt Physics | MIT |
| SPlisHSPlasH | MIT |
| RecastNavigation | zlib |
| EnTT | MIT |
| spdlog | MIT |
| nlohmann/json | MIT |
| sol2 | MIT |
| Lua | MIT |
| FreeType | FreeType License / GPLv2 |

The ACES tone-mapping fit is by Krzysztof Narkowicz and is available under
CC0 or MIT.
