# The brush world: maps in the classic BSP families (version 29 and BSP2, version 38, version 46)
# read lump by lump into one view, and the fixture writers that make test maps of each. Generic to
# any game on these map formats (skill: brush).
# Its app/ half (ge_brush_app, with the GPU) is the map as drawn: the BSP renderer, classic or v2
# (our sky, our water, lava and slime, real-time lights).
ge_add_plugin(brush ENGINE core APP render gpu water wind assets lab)
if(TARGET ge_brush_app)
  ge_embed_wgsl(ge_brush_app app/bsp.wgsl app/bsp_fix.wgsl app/bsp_shadow.wgsl)
endif()
