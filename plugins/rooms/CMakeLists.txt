# The engine's own level format: rooms of plain geometry that take little disk, each carrying the
# seed its generated surfaces (rock relief, caves) are grown from. Generic to any game built of
# rooms (skill: rooms).
# Its lab draws grown rooms with the labs' model view, so the labs (APP) reach assets.
ge_add_plugin(rooms ENGINE core ai move APP assets lab)
