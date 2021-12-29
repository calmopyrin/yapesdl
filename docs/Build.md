
# Compile

If your system supports both **[SDL]** and **[GCC]**, <br>
**Yape** should be able to compile and run.

*Even porting to `Android` is possible*

<br>

---

**⸢ [Source Code] ⸥ ⸢ [Releases] ⸥**

---

<br>

## Flags

Define `AUDIO_CALLBACK` to use the **Old Audio API**.

  *Pre `2.0.4` versions of `SDL2` need this*

Define `ZIP_SUPPORT` for **Preliminary ZIP File** support.

  *Only needed for `Compressed D64` files*

<br>
<br>

## Linux

You will require the **[SDL]** package.

```sh
sudo apt install libsdl2-dev
```

The `makefile` is written for <br>
**GCC** but can easily be adjusted.

**Extract** the project files and <br>
***move*** into the it's root folder.

Compile **Yape** with:

```sh
make
```

**or**

Utilize the `Code::Blocks` project <br>
file to setup and compile **Yape**.

If the compilation finished without <br>
errors you can start **Yape** with:

```sh
./yapesdl
```

Check out what **[Arguments]** you can pass.

<br>
<br>

## Mac OS X

1. Download the **[SDL]** library.

2. Copy the **SDL2** framework file to `/Library/Frameworks/`

4. Use the provided `XCode` workspace.

<br>
<br>

## Windows

Both `32 / 64 bit` builds are possible.

##### SDL

Install either the `Mingw` or <br>
`Visual Studio` version of <br>
the **[SDL]** library.

##### SDL 2.0.4

If you use version `2.0.4` of the Windows **SDL** <br>
library, please consider upgrading to `2.0.5+` <br>
to avoid crashes caused by the **DX9** driver.

##### Microsoft Visual Studio

*It is recommended to use the `MVS 14` solution file.* <br>
*The `2015` / `2017` community versions of Visual* <br>
*Studio are free and have an excellent debugger.*

<br>
<br>

## Emscripten

- **[Install][Emscripten Installation]** **Emscripten**.

- For **Visual Studio**, install the **[Toolchain Extension]**

- Under **Windows** it's best to use the solution config provided.

  *Compiling* ***Emscripten*** *manually* <br>
  *requires a lot of manual workarounds.*

- You **won't** need to download any **SDL2** headers as they <br>
  are automatically installed during the initial compilation.

  ***Reference:*** [`1`][Emscripten A] [`2`][Emscripten B]

- **Non-VS** solutions require the prebuilt **[SDL2][Emscripten Prebuilt]** files.

<!----------------------------------------------------------------------------->

[Source Code]: https://github.com/calmopyrin/yapesdl
[Releases]: https://github.com/calmopyrin/yapesdl/releases

[Arguments]: Usage.md

[SDL]: http://libsdl.org
[GCC]: https://gcc.gnu.org/

[Emscripten Installation]: https://emscripten.org/docs/getting_started/downloads.html
[Toolchain Extension]: https://github.com/nokotan/VSExtForEmscripten
[Emscripten A]: https://github.com/kripken/emscripten
[Emscripten B]: https://github.com/emscripten-ports/SDL2
[Emscripten Prebuilt]: https://buildbot.libsdl.org/sdl-builds/sdl-emscripten/?C=M;O=D
