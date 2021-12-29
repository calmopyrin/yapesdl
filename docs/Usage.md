
# Usage

---

## Command Line Arguments

You can specify optional settings to start **Yape** with.

<br>

##### Load File

Start **Yape** with a specific file being loaded.

```sh
yapesdl <Format> <FilePath>
```

**Formats:** `PRG` `P00` `T64` `TAP` `D64` `ZIP`

```sh
yapesdl PRG ~/Desktop/Game.PRG
```

<br>

##### C64 Mode

Start **Yape** with `C64` emulation.

```sh
yapesdl -c64
```

---

## Shortcuts

| Keys | Function |
|:----:|:---------|
| <kbd>F12</kbd> / <kbd>Escape</kbd> | **Exit** the emulator |
| <kbd>F11</kbd> | Soft **Reset** |
| <kbd>Left Control</kbd> + <kbd>F11</kbd> | Forced **Reset** \| Memory remains intact |
| <kbd>Shift</kbd> + <kbd>F11</kbd> | Hard **Reset** \| Clears Memory \| Reloads ROMs \| Rewinds Tape |
| <kbd>F5</kbd> | Presses `Play` on tape |
| <kbd>F6</kbd> | Presses `Stop` on tape |
| <kbd>F7</kbd> | Save `BMP` screenshot |
| <kbd>F8</kbd> | Toggles the **User Interface**.<br><table><tr align = 'center' ><td rowspan = '3' ><b>User Interface<br>Options</b></td><td><kbd>Escape</kbd></td><td> Exit</tr><tr align = 'center' ><td><kbd>Arrow</kbd></td><td>Move</td></tr><tr align = 'center' ><td><kbd>Enter</kbd></td><td>Select</td></tr></table> |
| <kbd>F9</kbd> | Quick **Debugger** |
| <kbd>F10</kbd> | Save **Settings** ➞ Home Directory |
| <kbd>Left Alt</kbd> + <kbd>1 - 3</kbd> | **Window** Size |
| <kbd>Left Alt</kbd> + <kbd>L</kbd> | **Emulator** Selection <ul><li><code>Commodore Plus / 4</code> \| `Cycle Based`</li><li><code>Commodore Plus / 4</code> \| `Line Based`</li><li><code>Commodore 64</code> \| `Cycle Based`</li></ul> |
| <kbd>Left Alt</kbd> + <kbd>I</kbd> | Switch Emulated **Joystick** Port |
| <kbd>Left Alt</kbd> + <kbd>M</kbd> | Enter **External Console** Based Monitor / Disassembler |
| <kbd>Left Alt</kbd> + <kbd>P</kbd> | Toggle **CRT** Emulation |
| <kbd>Left Alt</kbd> + <kbd>R</kbd> | Force **Reset** Machine |
| <kbd>Left Alt</kbd> + <kbd>S</kbd> | Toggle **FPS** |
| <kbd>Left Alt</kbd> + <kbd>W</kbd> | Toggle `Unlimited` ⬌ `50Hz` |
| <kbd>Left Alt</kbd> + <kbd>Enter</kbd> | Toggle **Fullscreen** |
| <kbd>Left Alt</kbd> + <kbd>F5</kbd> | **Save** Emulator State|
| <kbd>Left Alt</kbd> + <kbd>F6</kbd> | **Load** Emulator State |
| <kbd>Left Alt</kbd> + <kbd>F8</kbd> | **Save** Memory of `$2B/$2C` & `$2D/$2E` |
| <kbd>Left Alt</kbd> + <kbd>Keypad +</kbd> | Collate **Replay** frequencies |

---

## Files

Internal disk `Load` / `Save` operations are supported and will <br>
use the file system directory, usually `/home/<User>/yape`.

`PRG` files, you wish to load, should also go there.

It is also possible to ***browse the file tree*** from within the **User Interface**.

---

## ROMs

Full **ROM** banking is supported on the `Plus / 4`, <br>
for this use the `Attach / Detach ROM` menu.

<br>

##### Supported Default ROMs

<table>
    <tr>
        <th align = 'center' ><code>BASIC</code></th>
        <td><b>ROM</b> with the <b>BASIC</b> interpreter</td></tr>
    <tr>
        <th align = 'center' ><code>KERNEL</code></th>
        <td><b>ROM</b> with the <b>System Kernel</b></td></tr>
    <tr>
        <th align = 'center' ><code>3PLUS1LOW</code></th>
        <td><b>Low</b> bank <b>ROM</b> of the <code>Plus / 4</code> software</td></tr>
    <tr>
        <th align = 'center' ><code>3PLUS1HIGH</code></th>
        <td><b>High</b> bank <b>ROM</b> of the <code>Plus / 4</code> software</td></tr>
</table>

<br>

##### C64 Mode

In this mode, a handful of cartridges can be <br>
loaded using the `Attach / Detach ROM` option.

---

## Keyboard Mapping

Some keys need to be mapped differently <br>
in the emulator and are dependent on <br>
the **SDL**, as such *your mileage may vary*.

| Commodore | Emulator |
|:---------:|:--------:|
| <kbd>£</kbd> | <kbd>End</kbd> |
| <kbd>Clr / Home</kbd> | <kbd>Home</kbd> |
| <kbd>Restore</kbd> | <kbd>Print Screen</kbd> |
