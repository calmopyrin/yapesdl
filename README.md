
<br>

<div align = center>

[![Badge License]][License]   
![Badge Version]

<br>

# YapeSDL

This is a **SDL** port of the **[YAPE]** emulator, <br>
developed by [`Attila Grósz`][Attilia] since the<br>
the year **2000** till the present day.  

<br>
<br>

[![Button Demo]][Demo]   
[![Button Usage]][Usage]   
[![Button Building]][Building]   
[![Button Changelog]][Changelog]

</div>

<br>
<br>

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

<br>
<br>

## Supported Chips

<br>

#### CPU Emulation

*These chips are emulated fully / cycle exact.*

<kbd>  6502  </kbd>  
<kbd>  6510  </kbd>  
<kbd>  7501  </kbd>  
<kbd>  8501  </kbd>

<br>

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

<br>
<br>

## Supported Modules

- **Joystick**

    Via **Mouse** / **Gamepad**

- **Drive**

    `1541` \| Read / Writes

- **Tape**

    *Near Full Emulation*

- **CRT**

    *Partial Emulation*

<br>
<br>

## Related

##### [+4World]

Games / Tape Images / Scans

##### [C64]

FTP Archive

<br>
<br>

## License

The `ROM` file header used belong to <br>
**Commodore Business Machines**.

Everything else falls under **[GPLv2][License]**.

<br>


<!----------------------------------------------------------------------------->

[Attilia]: http://gaia.atilia.eu/
[+4World]: http://plus4world.powweb.com/
[Demo]: http://gaia.atilia.eu/download/wip/YapeSDL.html
[YAPE]: http://yape.homeserver.hu/
[C64]: http://c64.rulez.org

[Changelog]: Documentation/Changelog.md
[Building]: Documentation/Build.md
[License]: LICENSE
[Usage]: Documentation/Usage.md


<!----------------------------------[ Badges ]--------------------------------->

[Badge License]: https://img.shields.io/badge/License-GPL2-015d93.svg?style=for-the-badge&labelColor=blue
[Badge Version]: https://img.shields.io/github/v/release/calmopyrin/yapesdl?style=for-the-badge&labelColor=569A31&color=407225


<!---------------------------------[ Buttons ]--------------------------------->

[Button Changelog]: https://img.shields.io/badge/Changelog-389EAC?style=for-the-badge&logoColor=white&logo=AzureArtifacts
[Button Building]: https://img.shields.io/badge/Building-734F96?style=for-the-badge&logoColor=white&logo=GoogleSearchConsole
[Button Usage]: https://img.shields.io/badge/Usage-blue?style=for-the-badge&logoColor=white&logo=GitBook
[Button Demo]: https://img.shields.io/badge/Demo-569A31?style=for-the-badge&logoColor=white&logo=AppleArcade

