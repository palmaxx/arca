# Requierments

My goal is to build a cross platform (macos and windows initially) 4k DV capable player, based on more projects i've begun and want to solidify. 

## References

Here are my current projects and what they mean for this new one:

### Streamxs

#### Status

"C:\DEV\ai-dev\projects\streamxs" is a windows electron+rust(napi+mpv ffi) player using an mpv host and dcomp. It uses complex indexing mechanism that separates local media (home video, phone videos etc) from indexable and searchable real media with tmdb and tvdb api with automatic media detection, indexing, probing, grouping and with personalized per folder and per user settings. It is the most complete but arguably the most flawed (in terms of architecture) , while electron is bypassed in terms of rendering and hdr, the overall size and footprint of the app is bery heavy.

#### What to take

The core ux is to take from this, it's the one with the features that match my expectations the most and are almost fully implemented. (Menu, indexing,etc). The architecture is verydifferent though so it may not be useful to actively copy from this, but rather use this as an inspiration or as a reference only from time to time.

### Fluss

#### Status

"C:\DEV\VS2026\Fluss" is a winui3 player with similar specs to streamxs though less mature in terms of logic (still under development), engine(uses windows engine so depends on ms store hevc or atmos codecs for example), and ui, as it is very minimal and not polished yet, though still at a good point

#### What to take

The app shell for windows is basically ready, potentially some logic as explained later

### mpv

#### Status

"C:\DEV\ai-dev\mpv-src" , this is one of the more ambitious parts, as it implements the dreaded missing d3d11, vulkan , and metal in the future, backends for libmpv's render api via new gpu-next render api i made so i can embed render context directly into the new player. 

#### What to take

The core render api change is what makes this project even possible so we'll have to develop this and also debug the new render api in parallel as we build the player.

### players brainstorm folder

- "C:\DEV\ai-dev\projects\players"

- This is a place where i experimented a bit and i was figuring out some stuff about player specs, use as very low priority source , as it contains many stale information, different architectures and so on, but DO check during sanity checks or if something related to more abstract architecture stuff there are many useful findings in there that took a lot of digging even if they are unrelated (e.g. libvlc 4.0 already does what my libmpv integration does- more or less- BUT is unreleased) 

## Chosen Stack + Arch

- shared c++ core that includes engine, db ops, library probing, fetching , indexing and grouping. 
- OR (IF FLUSS C# core (some parts of indexing and other stuff) can  be used and compiled also for mac , evaluate possible fully C# core if simpler to write engine into its core, and compile the c# core separate from the winui3 layer, so it can be reused on mac, and the windows winui3 app has even  less friction, however i would have to write p/invoke for mpv in c#) ( c# core<->libmpv p/invoke )-> winui3(direct integration) / swiftui(other translation layer- still required even with c++ core) so only real advantage is no windows friction.


- Per platform thin ui shell (plan 4)

## Requirements

### Day 0 
- cross platform app monorepo structure (organize in a useful way)
- working 4k hdr playback on windows
- keyboard controls and scrubbing 
- Basic Single file loading as an option in taskbar 
- Database set up
- Basic library management (import,view, delete) and viewing with playback to single files in folder(no queue, no online metadata, no probing and no media detail yet), include scaffold for hard separation of offline media and online media(with tmdb fetching) in both viewing (offline is basically file explorer, for online all items in a folder should be indexed according to what they are so tv shows are grouped etc) and behavior (no online queries at all for offline media just probing and thumbnails)

### Long term (check streamxs for long term parity)
- Media Detail Page
- File Probing and thumbnails(more than one per media to get active preview on hover)
- 
- Gamepad support

