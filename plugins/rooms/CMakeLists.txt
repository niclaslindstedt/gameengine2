# The engine's own level format: rooms of plain geometry that take little disk, each carrying the
# seed its generated surfaces (rock relief, caves) are grown from. Generic to any game built of
# rooms (skill: rooms).
# It clones a classic map (clone.hpp) off the brush plugin's reader.
# Its labs draw with the labs' model view (APP assets, lab) and read packs (APP vfs).
ge_add_plugin(rooms ENGINE core ai move PLUGINS brush APP assets lab vfs)
