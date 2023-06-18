# bmflat (Bm♭)

**bmflat** is a BMS (Be-Music Source) parser library in plain C. BMS is a musical track format for rhythm games and bmflat can be useful when implementing games or analysis tools.

## Usage Examples

[BGA Compo](https://gist.github.com/50764b32880710f9ec8b95de353a18fb) is a utility that exports background animation videos from music tracks.

See `examples/flattest.c` for another simplistic example which dumps all metadata and content of a given file.

`examples/flatspin.c` is a playback and visualisation tool for BMS music tracks. Build the program with GLEW and GLFW libraries, or simply use [xmake](https://xmake.io/). Use the arrow keys and the Shift key for navigation, and the Space key for playback. **⚠️ Substantial amounts of imagery that may trigger photosensitive epilepsy will be displayed during playback.** If you are affected, please be cautious with experimenting.

## License

bmflat is provided under [Mulan PSL v2](https://opensource.org/licenses/MulanPSL-2.0), a BSD-like permissive license.
