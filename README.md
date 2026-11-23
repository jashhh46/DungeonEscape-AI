# DungeonEscape

A C++17 dungeon game built with SFML 3.1.0 and CMake. CMake downloads SFML with FetchContent when the project is configured.

## Requirements

- CMake 3.28 or newer
- Git
- A C++17 compiler (MSVC x64 on Windows)
- VS Code with the CMake Tools extension, or another CMake-capable IDE

## Configure and build

Run these commands from the project folder:

```sh
cmake -B build
cmake --build build --config Debug
```

On Windows with the Visual Studio generator, run the game from `build/bin/Debug/DungeonEscape.exe`.
The build copies `Assets/` next to the executable.

## Run the Behaviour Tree tests

```sh
cmake --build build --config Debug --target BehaviourTreeTests
ctest --test-dir build -C Debug --output-on-failure
```

## Assets

Assets are by Nika Studio. See [the Fantasy Dungeon asset page](https://nikastudio.itch.io/fantasy-dungeon-top-down-pixel-rpg-asset-pack-unity-6-urp), `Assets/README_IMPORT.txt`, and `Assets/LICENSE.txt` for credit and license terms.

## SFML version

The project fetches SFML 3.1.0. To change the version, update the `GIT_TAG` value in `CMakeLists.txt`.

## Project license

The SFML CMake template source files retain their original Public Domain/MIT license terms in `LICENSE.md`. The game assets have their own license in `Assets/LICENSE.txt`.
