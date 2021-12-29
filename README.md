# YapeSDL

**Version:** `0.71.1`

This is a **SDL** port of the **[YAPE]** emulator, <br>
developed by [`Attila Grósz`][Atilia] since the<br>
the year **2000** till the present day.  

---

**⸢ [How To Use] ⸥ ⸢ [Build From Source] ⸥**

---

## Features

-   **Supported File Formats**

    `PRG`  `P00`  `T64`  `D64`  `TAP`
    
-   **Memory Expansion Support**

    `REU 1700 / 1750 / 1764`  `Hannes 256kB`

-   **Save / Load Disk**

    `Serial IEC`  ⬌  `File System`

-   **ROM Banking**

    Fully supported on  `+4`

-   **Snapshots** / **Saves**

---

## Supported Chips

#### CPU Emulation

*These chips are emulated fully / cycle exact.*

<kbd>  6502  </kbd>  
<kbd>  6510  </kbd>  
<kbd>  7501  </kbd>  
<kbd>  8501  </kbd>

<br>

#### Chip Emulation

| Group | Chip | Replication |
|:-----:|:----:|:-----------:|
| `TED` | `7360` | `Near Full` |
| `TED` | `8360` | `Near Full` |
| `VIC-II` | `6569` | `Near Full` |
| `SID` | `6581` | `Reasonable` |
| `SID` | `8580` | `Reasonable` |
| `CIA` | `6526` | `Partially`<br>`Incomplete` |

---

## Supported Modules

- **Joystick**

    Via **Mouse** / **Gamepad**

- **Drive**

    `1541` \| Read / Writes

- **Tape**

    *Near Full Emulation*

- **CRT**

    *Partial Emulation*

---

## Related

##### [+4World]

Games / Tape Images / Scans

##### [C64]

FTP Archive

---

## License

The `ROM` file header used belong to <br>
**Commodore Business Machines**.

Everything else falls under **[GPLv2]**.

<!----------------------------------------------------------------------------->

[GPLv2]: LICENSE

[YAPE]: http://yape.homeserver.hu/
[Atilia]: http://gaia.atilia.eu/

[Build From Source]: docs/Build.md
[How To Use]: docs/Usage.md

[+4World]: http://plus4world.powweb.com/
[C64]: http://c64.rulez.org
