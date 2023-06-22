A redirecting WinMM wrapper which allows configurable renaming and modifying other properties of MIDI input and output devices.
In other words, with this DLL you can make MIDI devices appear to particular applications to have a different name, ID or driver version.

Use-cases include:
- Making devices appear in WINE (on Linux) as they would appear on Windows natively. Usually, MIDI devices get a name from ALSA and this is passed on to WINE, but does not match what Windows would usually come up with. It can be helpful because some applications expect a device by a certain name or ID to appear.
- Installing a proxy between the device and an automatically connecting application. E.g. in combination with something like JACK, you could route MIDI through other applications before appearing on a virtual loopback device parading as the original device.

# Installation

Download or build **winmm.dll** and place it in the same folder as your executable. Also provide a config file (see below). You are set to go.

# Configuration

By default, the DLL looks for its configuration in **executable_path/midi_rename_config.json**. The example below serves to make the Joue Play MIDI controller work in WINE:

```
{
  "log": midi_rename.log,
  "rules": [
    {
      "match_name": "Joue - Joue Play",
      "replace_name": "Joue",
      "replace_man_id": 65535,
      "replace_prod_id": 65535,
      "replace_driver_version": 0
    },
    {
      "match_name": "Joue - Joue Edit",
      "match_direction": "in",
      "replace_name": "MIDIIN2 (Joue)",
      "replace_man_id": 65535,
      "replace_prod_id": 65535,
      "replace_driver_version": 0
    },
    {
      "match_name": "Joue - Joue Edit",
      "match_direction": "out",
      "replace_name": "MIDIOUT2 (Joue)",
      "replace_man_id": 65535,
      "replace_prod_id": 65535,
      "replace_driver_version": 0
    }
  ]
}
```
Walkthrough of the config:
- "log": sets a file to log to. This is optional but can be helpful when debugging rules (and confirming that the DLL was indeed loaded).
- "rules": an array of rule objects which determine which devices should be modified and how:
  - "match_name", "match_direction" (in/out, referring to whether it's an input or output device), "match_man_id" (manufacturer ID), "match_prod_id" (product ID), "match_driver_version" will compare the given properties (as in the midiXXXGetDeviceCaps structure). In a single rule, matching on all of the given keys (they are ANDed, not ORed) will result in a match.
  - "replace_XXX" for the same properties (except direction of course) will then overwrite said property with a particular value.

So the example above will, among other things, modify the "Joue - Joue Play" device as named by ALSA to "Joue" as the Joue Play app expects.

# Environment variables

Apart from the config, the following env vars are supported:

- MIDI_REPLACE_LOGFILE sets the logfile, overriding the "log" setting in the config if any.
- MIDI_REPLACE_CONFIGFILE sets the config filename.

## WINE setup

For this to work in Wine, set the winmm library to "native then builtin" on the Libraries setting of winecfg. You can use the logging settings above to generate a log file, which confirms that the correct DLL was loaded (this is not reported by WINEDEBUG=+loaddll for some reason).

# License
See [LICENSE](LICENSE).
