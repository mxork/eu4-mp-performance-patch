# EU4 MP Performance Patch

This mod contains:

1. a custom version of the nakama-cpp client library
2. binary patches to reduce the frequency of the game checksum code, which bottlenecks on a single cpu.
3. a small custom mp server (`runk`) which emulates the necessary parts of a Nakama server, but with no tick latency for sending match data.

Windows and Linux versions are posted. Both were written and tested against the Steam versions of the game, but I think they would work on other versions. Mac users, sorry. Ping me if you want to write a port.

## Installation

To try it out, you need to place the two libraries (nakama-cpp and libpatcher) into your game folder, then start the game with Nakama MP enabled:

```
# or you can select Nakama MP from the steam launcher
eu4.exe -nakamamp
```

You should probably back up the original version of the nakama-cpp library.

The patch could in theory work with the official servers, but I didn't want to get anyone in trouble or accidentally spam Paradox if the patch misbehaves. So, by default, it will connect to an instance of the custom MP server I have running in The Cloud.

If you want to run a local version of the server, the `runk.exe` binary binds to port 7350 by default. To get the game to connect to it:

```
# replace "localhost" with your local IP if it's not recognized.
# gotcha: you must supply *all* of these arguments for eu4 to use them. you can't omit defaults
eu4.exe  -nakamamp -nakama_host='localhost' -nakama_key='defaultkey' -nakama_port=7350 -nakama_ssl=0
```

If you want to run the server on the internet, I *strongly* recommend putting a TLS-capable reverse proxy in front of runk. I used Caddy. You can find a copy of my Caddyfile in the `deploy` folder.

## Details

EU4 multiplayer performance is mostly determined by three factors:

1. Network latency (duh). This isn't just "ordinary" network ping; it includes (arguably unnecessary) processing latency introduced by the multiplayer sever.
2. Single-core cpu performance. EU4 is actually quite multi-threaded in single player, but multiplayer runs a checksum on the game state every single day. This is bottle-necked on just one CPU, and is a major reason why multiplayer runs slower.
3. The settings `DAYS_BEHIND_LOWER_SPEED` and `DAYS_BEHIND_PAUSE` from defines.lua. These have reasonable defaults, but if you have a fast machine or higher network latency to other players in your game, the defaults essentially guarantees you can't run at higher game speeds.

`runk` eliminates most processing latency from the server by immediately forwarding match data when received. The DLL patch reduces the frequency of checksums to once a week, rather than every day. An optional steam mod (linked below) adjusts the `DAYS_BEHIND*` settings to be more forgiving.

### Note on the public server

The cloud instance is running in some north-eastern North American Google cloud region. Latency improvements won't be as impressive if you are on another continent, but Western Europe
should still be better-than-official. Eliminating the process latency compensates for the longer round-trip.

If you have a group in another region and want to try it out, let me know (I'm monitoring the eu4-modding discord). Deploying another server is cheap, quick, easy.

### Building

As much as possible, I've tried to make building everything straight-forward. `runk` and `libpatcher` both have Docker build files included, mostly as a reference, but also as a check to make sure my machine didn't have any magical state tainting the process.

That said, building the `nakama-cpp` library is a real pain, particularly on windows. I can't remember all the dark twists and turns I went through to get my toolchains set up properly, so if you need assistance, messaging me is probably more useful than any documentation I can write here.

## Limitations

`runk` does the bare minimum to emulate the official Nakama servers. Currently, it does *not* support:

1. game passwords
2. hot join
3. any out-of-sync detection

And probably a lot else besides starting a game, other players joining (in the lobby), and sending data between players.

On out-of-sync detection: I'm still figuring out how this works in vanilla, but it seems to be driven by the server. The official Nakama server states that it can drop match data messages if its buffers fill up, and I suspect (with low confidence) that this is the main cause of out-of-sync's on official servers.

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
