<p align="center">
  Wrap WinMM in order to enable remapping MIDI device properties (such as their name).
</p>

# F.A.Q.

## What does this do?
Based on <a href='https://github.com/KeppySoftware/WinMMWRP/tree/master/winmmwrp'>WinMMWRP</a>, this project offers a **winmm.dll** replacement to be placed with specific applications.
It will provide access to the regular **winmm.dll** while offering the possibility to modify MIDI device properties as they are presented to the application.

The intended use-case is for use with MIDI devices from WINE on Linux environments, where ALSA often assigns different properties to devices than the Windows MIDI implementation would.
Some applications expect a compatible device to have a particular name or property.

## Status
WIP (not finished)

# License
See [LICENSE](LICENSE).
