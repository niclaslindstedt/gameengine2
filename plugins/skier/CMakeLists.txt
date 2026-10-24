# The skier on the snow: the snow under a pair of skis, the edge, the carve, the skid, the tuck,
# the drive, the air and the landing, the trunks and the wipeout, and the hand-built slopes and
# scenarios that stage him. Any skiing game on a generated mountain links it (skill: skier).
# Its app/ half (ge_skier_app) is the sound a skier makes (the wind and the snow beds) and his
# figure: the rig, its pose and the skinned model posed by it (links assets).
ge_add_plugin(skier ENGINE core ai wind water PLUGINS mountain APP audio assets)
