# The generated ski mountain: the world generator (mapgen) and the rule book it is held to
# (analysis), and the map drawn from above on the CPU (picture, on the lab's raster). Any game on
# a generated snow mountain links it (skill: mountain).
# Its app/ half (ge_mountain_app, with the GPU) is the mountain as drawn: the snow's material.
ge_add_plugin(mountain ENGINE core ai wind prof lab APP render gpu)
if(TARGET ge_mountain_app)
  ge_embed_wgsl(ge_mountain_app app/snow.wgsl app/trails.wgsl)
endif()
