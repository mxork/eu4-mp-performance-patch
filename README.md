# EU4 MP Performance Patch

This mod contains:

1. a custom version of the nakama-cpp client library
2. binary patches to reduce the frequency of the game checksum code, which bottlenecks on a single cpu. (`--fastmp`)
3. binary patches which dynamically adjust the host player's game speed to prevent the endlessly annoying "Player X is lagging behind". (`--speedcontrol`)
4. a small custom mp server (`runk`) which emulates the necessary parts of a Nakama server, but with no tick latency for sending match data.

Windows and Linux versions are posted. Both were written and tested against the Steam versions of the game, but I think they would work on other versions. Mac users, sorry. Ping me if you want to write a port.

## Installation

To try it out, you need to place the two libraries (nakama-cpp and libpatcher) into your game folder, then start the game with Nakama MP enabled and:

```
# gotcha: if you use the paradox launcher, it won't pass arguments to the game.
#         make sure you're calling eu4.exe directly.
> eu4.exe -steammp -fastmp -speedcontrol
```

You should probably back up the original version of the nakama-cpp library.

If you want to run a local version of the server, the `runk.exe` binary binds to port 7350 by default. To get the game to connect to it:

```
# replace "localhost" with your local IP if it's not recognized.
# gotcha: you must supply *all* of these arguments for eu4 to use them. you can't omit defaults.
> eu4.exe  -nakamamp -unofficialmp -nakama_host='localhost' -nakama_key='defaultkey' -nakama_port=7350 -nakama_ssl=0
```

If you want to run the server on the internet, I *strongly* recommend putting a TLS-capable reverse proxy in front of runk. I used Caddy. You can find a copy of my Caddyfile in the `deploy` folder.

### Version compatibility

Written against `EU4 v1.37.5.0 Inca`, steam version.

MD5 of executables:

```
ba1e6957fff90d306e3f40a5388fc925  eu4     linux
5af10c50faebd12408149a3a27deffcb  eu4.exe windows
```

The patcher performs sanity checks before applying the patch. If it fails, it will log `refusing to apply patch`. Logs go to stderr on linux and `patcher.log` on windows.

## Details

EU4 multiplayer performance is mostly determined by three factors:

1. Network latency (duh). This isn't just "ordinary" network ping; it includes (arguably unnecessary) processing latency introduced by the multiplayer sever.
3. Single-core cpu performance. EU4 is actually quite multi-threaded in single player, but multiplayer runs a checksum on the game state every single day. This is bottle-necked on just one CPU, and is a major reason why multiplayer runs slower.
4. The settings `DAYS_BEHIND_LOWER_SPEED` and `DAYS_BEHIND_PAUSE` from defines.lua. These have reasonable defaults, but if you have a fast machine or higher network latency to other players in your game, the defaults essentially guarantees you can't run at higher game speeds.

`runk` eliminates most processing latency from the server by immediately forwarding match data when received. The DLL patch turns off the daily checksum. An optional steam mod (linked below) adjusts the `DAYS_BEHIND*` settings to be more forgiving.

### Speed controller

For the most part, I don't think the EU4 codebase is all that bad, and I'm reluctant to bash code without understanding the context behind it.
That said, I can say with moderate confidence that the built-in lag handler is not good. It's not good in an abstract code-design sense. It's not
good in that it leads to bad UX: annoying pop-ups and bad debouncing (it can trigger multiple times, going straight from speed 5 to speed 2 when speed 4 would have been enough).
It's not good in that it seems oblivious about how the underlying network stack actually runs.

The speed controller included in this mod treats the current game speed as a maximum. If clients begin to lag behind by more than 5 days, it will start slowing down the
speed at which the host is processing turns *without* changing the speed setting (if you are on speed 5, it will stay on speed 5). More lag will reduce the speed even more.
If the clients catch up, the speed goes up again.

This approach makes it a lot easier to run at a consistently higher speed. The vanilla speed settings are discrete:

1. speed 1 runs at one day per 2 seconds
2. speed 2 at one day per 1 second
3. speed 3 at one day per 500ms
4. speed 4 at one day per 250ms
5. speed 5 as fast as the host can go

But what if everyone in the game can run at one day every 600ms? Well, in the base game, speed 3 is too fast, so you're stuck running on speed 2,
which is a massive slowdown. With this mod, you can run at whatever speed your players can keep up with.

The speed controller only needs to be run by the host, and should be completely compatible with all versions of the game.

### Building

As much as possible, I've tried to make building everything straight-forward. `runk` and `libpatcher` both have Docker build files included, mostly as a reference, but also as a check to make sure my machine didn't have any magical state tainting the process.

That said, building the `nakama-cpp` library is a real pain, particularly on windows. I can't remember all the dark twists and turns I went through to get my toolchains set up properly, so if you need assistance, messaging me is probably more useful than any documentation I can write here.

## Limitations

`runk` does the bare minimum to emulate the official Nakama servers. Currently, it does *not* support:

1. game passwords
2. out-of-sync detection

And probably a lot else besides starting a game, other players joining (in the lobby), and sending data between players.

## OOS (out-of-sync) detection

I'm still figuring out how this works in vanilla, but it seems to be driven by the server. The official Nakama server states that it can drop match data messages if its buffers fill up, and I suspect (with low confidence) that this is the main cause of OOS on official servers.

It's worth mentioning that the daily checksums are seem to be never (rarely?) used during normal play. I've tried running a vanilla host and a guest with deliberately corrupted checksums on the official servers, and never triggered an OOS. As I said, still figuring out how this is supposed to work.

## Security

Running DLLs from the internet is inherently a little sketchy. I encourage you to read the code if you're able, or scan the released binaries with any malware detection tool you have available.

Additionally, `runk` does, out of necessity, receive Paradox multiplayer session tokens as part of emulating a Nakama server. These tokens are not logged, stored, or used in any way after being exchanged for runk-specific session tokens. Patching the game to not send these tokens is possible, but not yet implemented.

## Legal

AFAICT, this patch is in compliance with the EU4 EULA. If anyone from Paradox tells me otherwise, I will, of course, take it all down.

As for what license this code is released under... I don't know what is appropriate. I disclaim all liability, in line with the MIT license, but you inherit all relevant responsibilities and restrictions from the relevant Paradox agreements, and those from use of a modified nakama-cpp client library (released under the Apache license, here: https://github.com/heroiclabs/nakama-cpp). By using or modifying the code, you agree to be bound by those terms.

The publicly-available runk server is deployed on a temporary basis for users to experiment with. I make no guarantees that it will be available indefinitely, and you agree to not hold me liable for anything that goes wrong if you use it.

## Steam Mod

This mod changes the default lag-behind thresholds, which you might want if running at high speeds:

- https://steamcommunity.com/sharedfiles/filedetails/?id=3436026874
- https://mods.paradoxplaza.com/mods/103262/Any
