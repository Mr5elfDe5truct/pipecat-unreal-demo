# Pipecat Unreal Engine Demo

This demo shows how to bring a [Pipecat](https://github.com/pipecat-ai/pipecat)
voice agent into an Unreal Engine game. Talk to a glowing orb, and it shapes
the world around you.

You start alone on an empty, flat grid under a clear midday sky. Ask the orb
for hills, grass, mountains, a lake, a river that flows into it, a forest,
some fog or a sunset, and the world changes around you as it answers.

The orb is a Pipecat bot, a voice agent that hears you with Deepgram, answers
with OpenAI and speaks with Cartesia. The game talks to it over a WebSocket
with the [Pipecat C++ client](https://github.com/pipecat-ai/pipecat-client-cxx).

## 📋 Requirements

- Linux or Windows, on x86_64.
- Unreal Engine 5.8: Epic's
  [binary build for Linux](https://www.unrealengine.com/en-US/linux), or, on
  Windows, the one the Epic Games Launcher installs.
- The Pipecat C++ client's source:
  ```
  git clone https://github.com/pipecat-ai/pipecat-client-cxx
  ```
- CMake, Ninja and Git, to build the client.
- On Windows, [Visual Studio](https://visualstudio.microsoft.com/downloads/)
  or its Build Tools, with the "Desktop development with C++" workload, and
  these components of it:
  - "C++ CMake tools for Windows", which come with CMake and Ninja.
  - ".NET Framework 4.8 SDK", which Unreal Engine needs to build its editor.
- [uv](https://docs.astral.sh/uv/), to run the bot.
- API keys for [Deepgram](https://console.deepgram.com),
  [OpenAI](https://platform.openai.com/api-keys) and
  [Cartesia](https://play.cartesia.ai/keys), for the bot.
- A microphone. Headphones work best: the game doesn't cancel the orb's echo,
  so with speakers the orb can hear itself.

## 🔨 Build

Get the demo:

```
git clone https://github.com/pipecat-ai/pipecat-unreal-demo
cd pipecat-unreal-demo
```

Then follow the steps for [Linux](#linux) or [Windows](#windows).

### Linux

Tell the scripts where Unreal Engine and the Pipecat C++ client are:

```
export UE_ROOT=/path/to/UnrealEngine
export PIPECAT_CLIENT_CXX=/path/to/pipecat-client-cxx
```

Then, from this directory:

1. Build the Pipecat C++ client and its WebSocket transport with Unreal's
   toolchain. They're installed in `Plugins/Pipecat/ThirdParty/Linux`, for the
   Pipecat plugin.

   ```
   ./Plugins/Pipecat/ThirdParty/build-linux.sh
   ```

2. Build the demo:

   ```
   "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" PipecatDemoEditor Linux Development -Project="$PWD/PipecatDemo.uproject"
   ```

3. Set up its content. This copies Unreal Engine's Third Person template (the
   character, its animations and the input setup) into `Content/`, and creates
   the demo's materials in the editor, which is why the demo needs to be built
   first.

   ```
   ./setup.sh
   ```

### Windows

In PowerShell, tell the scripts where Unreal Engine and the Pipecat C++ client
are:

```
$env:UE_ROOT = "C:\Program Files\Epic Games\UE_5.8"
$env:PIPECAT_CLIENT_CXX = "C:\path\to\pipecat-client-cxx"
```

If PowerShell doesn't let you run scripts, allow them in this terminal first:

```
Set-ExecutionPolicy -Scope Process Bypass
```

Then, from this directory:

1. Build the Pipecat C++ client and its WebSocket transport with Visual
   Studio's compiler, which the script finds. They're installed in
   `Plugins\Pipecat\ThirdParty\Win64`, for the Pipecat plugin.

   ```
   .\Plugins\Pipecat\ThirdParty\build-windows.ps1
   ```

2. Build the demo:

   ```
   & "$env:UE_ROOT\Engine\Build\BatchFiles\Build.bat" PipecatDemoEditor Win64 Development -Project="$PWD\PipecatDemo.uproject"
   ```

3. Set up its content, as on Linux, once the demo is built:

   ```
   .\setup.ps1
   ```

## 🚀 Run

1. Start the bot, with your API keys in `pipecat/.env`:

   ```
   cd pipecat
   cp env.example .env  # Then set the API keys in it.
   uv run bot.py -t websocket
   ```

   It waits for the game at `http://localhost:7860`.

2. In another terminal, from this directory, with `UE_ROOT` set as for the
   build, open the demo in the editor, and press Play. On Linux:

   ```
   "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/PipecatDemo.uproject"
   ```

   On Windows, in PowerShell:

   ```
   & "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor.exe" "$PWD\PipecatDemo.uproject"
   ```

   Or run it as a game, without the editor, by adding `-game`. On Linux:

   ```
   "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/PipecatDemo.uproject" -game
   ```

   On Windows:

   ```
   & "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor.exe" "$PWD\PipecatDemo.uproject" -game
   ```

The orb connects to the bot, greets you, and listens. Then talk to it, e.g.:

- "Make it hilly, and add some grass."
- "We need mountains."
- "This is a perfect place for a lake."
- "Add a river that flows into the lake."
- "Grow a forest."
- "Make it sunset, with some fog."

Each change takes a few seconds, and plants grow out from where you are. This
is what the orb can change:

| What        | How                                    |
| ----------- | -------------------------------------- |
| Time of day | Dawn, noon, sunset or night            |
| Fog         | None, light or thick                   |
| Clouds      | Clear, some or cloudy                  |
| Terrain     | Flat, hilly or mountains               |
| Grass       | None, some or lush                     |
| Trees       | None, a few or a forest                |
| Water       | A pond, a lake and a river, or none    |

## ⚙️ How it works

- The game, in `Source/PipecatDemo`, puts the orb next to the player. The orb
  has a voice component from the Pipecat plugin, in `Plugins/Pipecat`, which
  starts the bot and talks to it with the Pipecat C++ client: it sends the
  bot the microphone, and plays the bot's voice from the orb.
- The bot, in `pipecat/bot.py`, has one function, `set_environment`, which
  the LLM calls with what to change. The bot answers the call itself, and
  Pipecat tells the game about it, with what to change. The game's world then
  changes to the new look.
- The orb glows as it speaks, you glow as you speak, and subtitles show what
  you both say.

## 🎛️ Options

The game takes these on the command line, with the editor too:

- `-PipecatStartUrl=URL`: the bot's start endpoint. It's
  `http://localhost:7860/start` by default, in `Config/DefaultGame.ini`.
- `-PipecatApiKey=KEY`, or the `PIPECAT_API_KEY` environment variable: an API
  key for starting the bot, e.g. a Pipecat Cloud public API key.
- `-PipecatNoMicrophone`: doesn't use the microphone.
- `-PipecatSay="..."`: sends the bot a message once it's ready, e.g. to try it
  without talking.

To change the world without the bot, open the console (with the `` ` `` key)
and use `Pipecat.SetEnvironment`, e.g.:

```
Pipecat.SetEnvironment time_of_day=sunset fog=thick terrain=mountains lake=true
```

The trees are the low-poly ones from the engine's PCG biome sample, and there's
no grass mesh, so grass only colors the ground. For a more realistic look, use
other meshes, e.g. from [Fab](https://www.fab.com), by listing them as
`GrassMeshes` and `TreeMeshes` in `Config/DefaultGame.ini`.

## 🗂️ What's where

- `Plugins/Pipecat`: the Pipecat plugin, with its voice component, and the
  scripts that build the Pipecat C++ client for it.
- `Source/PipecatDemo`: the demo: the orb, the world and the subtitles.
- `pipecat`: the bot.
- `Scripts/create_materials.py`: creates the demo's materials, for `setup.sh`
  and `setup.ps1`.

## 📄 License

The demo is licensed under the [BSD 2-Clause License](LICENSE).
