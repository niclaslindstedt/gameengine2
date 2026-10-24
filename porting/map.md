# Porting map

A WORKING DOCUMENT (`porting/` is deleted when porting ends). One row per engine file that was
ported: the reference module it reproduces and the reference identifiers it covers, so a search
for a reference name (`make find Q=generateLevel`) lands on the engine file. The engine's own
files never name their reference; this table is the only place the link is kept.

`make agents-check` holds this table: every engine file named exists, rows are sorted by path,
and no engine file appears twice. Add a row in the same PR that adds a ported file, and move
the row when the file moves.

| Engine file | Reference module | Reference identifiers |
| --- | --- | --- |
| `engine/audio/dsp.cpp` | `oss-game-framework/src/audio/synth.ts` | fillNoise shaperCurve |
| `engine/audio/dsp.hpp` | `oss-game-framework/src/audio/synth.ts` | NOISE_POOL_S SHAPER_STEPS |
| `engine/audio/rack.cpp` | `oss-game-framework/src/audio/rack.ts` | createRack |
| `engine/audio/rack.hpp` | `oss-game-framework/src/audio/rack.ts` | Rack createRack |
| `engine/audio/sound.cpp` | `oss-game-framework/src/audio/play.ts` | playDef playSound shaped |
| `engine/audio/sound.hpp` | `oss-game-framework/src/audio/types.ts`, `play.ts` | SoundVoice SoundDef SoundBank PlayShape playDef playSound |
| `engine/audio/synth.cpp` | `oss-game-framework/src/audio/synth.ts` | createSynth envelope steer layer tone noise |
| `engine/audio/synth.hpp` | `oss-game-framework/src/audio/synth.ts`, `view.ts` | createSynth EchoOptions SynthOptions scaledView LIMITER_THRESHOLD_DB |
| `engine/audio/voice.hpp` | `oss-game-framework/src/audio/voice.ts` | ToneOptions NoiseOptions LayerSpec LayerTarget MIN_ATTACK_MS MAX_CUTOFF_RATIO safeCutoff shaperSteepness shaperPush envelopeShape |
| `engine/core/heightfield.hpp` | `oss-game-framework/src/core/heightfield.ts` | createHeightfield fillField sampleField sampleFieldGradient fieldGradient fieldCell |
| `engine/core/math.hpp` | `oss-game-framework/src/core/math.ts` | angleDiff |
| `engine/core/noise.hpp` | `oss-game-framework/src/core/noise.ts` | valueNoise noiseField sampleNoise |
| `engine/core/num.hpp` | JavaScript number semantics | Math.round Math.imul Math.max Math.min Math.sign `\|0` `>>>0` `%` |
| `engine/core/num_text.cpp` | JavaScript `Number.prototype` | toFixed toString |
| `engine/core/num_text.hpp` | JavaScript `Number.prototype` | toFixed toString |
| `engine/core/output.cpp` | `oss-game-framework/src/core/output.ts` |  |
| `engine/core/output.hpp` | `oss-game-framework/src/core/output.ts` | recentLogs setOutputSink |
| `engine/core/prng.hpp` | `oss-game-framework/src/core/prng.ts` | pickIndex createRng |
| `engine/core/quat.hpp` | `oss-game-framework/src/core/quat.ts` | fromEuler toEuler fromAxisAngle |
| `engine/core/solar.cpp` | `oss-game-framework/src/core/solar.ts` |  |
| `engine/core/solar.hpp` | `oss-game-framework/src/core/solar.ts` | sunAt moonAt daylightWindow sunOnDate hourOfElevation |
| `engine/loop/run_clock.hpp` | `oss-game-framework/src/loop/run-clock.ts` | createRunClock MAX_FRAME_SECONDS |
| `games/fall_line/labs/army_flight_lab.cpp` | `game5/scripts/army-flight.mjs` | the army's helicopters shown (here measured: the helicopters table and every machine's path, per scenario) |
| `games/fall_line/labs/army_lab.cpp` | `game5/scripts/army-ground.mjs` | the ground army shown (here measured: the army table and every soldier's path, per scenario) |
| `games/fall_line/labs/army_para_lab.cpp` | `game5/scripts/army-flight.mjs` | the army's paramotors shown (here measured: the paramotors table, every rig's path and height, per scenario) |
| `games/fall_line/labs/army_sled_lab.cpp` | `game5/scripts/army-ground.mjs` | the army's snowmobiles shown (here measured: the sleds table and every machine's path, per scenario) |
| `games/fall_line/labs/gunfire_lab.cpp` | `game5/scripts/gunfire-table.mjs` | the gunfire's table (here the range table by weapon, range, rules and target, and a seed-38 hunt's fire) |
| `games/fall_line/labs/lift_ride_lab.cpp` | `game5/scripts/lift-ride-preview.mjs` | the lift ride shown (here measured as a table, every lift ridden from its load zone to its top) |
| `games/fall_line/sim/army.cpp` | `game5/engine/game/army-ground.ts`, `army-lift.ts`, `army-wave.ts` | stepGround chase tryArrest writeTroops sendLiftSquads stepLiftTroops liftFor wayDown sendWave sendWaves retireWaves climbWithWave (the landings, the fire, the cover ring and the men on foot not carried) |
| `games/fall_line/sim/army.hpp` | `game5/engine/game/army-ground.ts`, `army-lift.ts`, `army-wave.ts` | stepGround liftFor wayDown armyTroops |
| `games/fall_line/sim/army_door.cpp` | `game5/engine/game/army.ts` | gunId man fight (the door guns; the gunners as pursuers not carried, the crab in echelon added) |
| `games/fall_line/sim/army_door.hpp` | `game5/engine/game/army.ts` | man fight |
| `games/fall_line/sim/army_drive.cpp` | `game5/engine/game/army-drive.ts` | wayCost chooseWay tunnelWithin |
| `games/fall_line/sim/army_drive.hpp` | `game5/engine/game/army-drive.ts` | DriveRead TerrainRead wayCost chooseWay tunnelWithin |
| `games/fall_line/sim/army_heli.cpp` | `game5/engine/game/army.ts`, `army-ground.ts` | stepArmy call arrive circleWay huntWant homeWant fly settle levelOut slideDoors writePursuers askLandings workLanding (the guns not carried; the ram, the fall and the wreck in `army_steal.cpp`) |
| `games/fall_line/sim/army_heli.hpp` | `game5/engine/game/army.ts` | stepArmy landing armyHelis |
| `games/fall_line/sim/army_heli_rules.hpp` | `game5/engine/game/defs/army.ts`, `defs/army-heli.ts`, `defs/army-ground.ts` | ARMY ARMY_HELI ARMY_GROUND (the landing, the rope, the approach, the dismount, the leapfrog) |
| `games/fall_line/sim/army_land.cpp` | `game5/engine/game/army-land.ts`, `army.ts` | landingSpot armyFloor ropeHover |
| `games/fall_line/sim/army_land.hpp` | `game5/engine/game/army-land.ts` | LandSpot landingSpot |
| `games/fall_line/sim/army_para.cpp` | `game5/engine/game/army-para.ts` | freshPara launchPara flyPara huntPara trackRound heightFor handsFor leavePara fireRifle downPara fallPara (handsFor's crab and stall margin left to `paramotor::para_pilot`, the wing flown by the free ride's own physics; meetPara, the stolen helicopter's ram and wash, not carried) |
| `games/fall_line/sim/army_para.hpp` | `game5/engine/game/army-para.ts` | freshPara launchPara huntPara leavePara |
| `games/fall_line/sim/army_para_rules.hpp` | `game5/engine/game/defs/army-air.ts` | ARMY_AIR (the paramotors' rows) |
| `games/fall_line/sim/army_rules.hpp` | `game5/engine/game/defs/army-ground.ts`, `defs/army-wave.ts` | ARMY_GROUND ARMY_WAVE |
| `games/fall_line/sim/army_sled.cpp` | `game5/engine/game/army-sled.ts` | stepArmySleds sledPad setOff chase halt home dismount watch writeCrews |
| `games/fall_line/sim/army_sled.hpp` | `game5/engine/game/army-sled.ts` | stepArmySleds sledPad crewed |
| `games/fall_line/sim/army_sled_rules.hpp` | `game5/engine/game/defs/army-sled.ts` | ARMY_SLED |
| `games/fall_line/sim/army_state.hpp` | `game5/engine/game/army-state.ts` | Soldier ArmyState |
| `games/fall_line/sim/army_steal.cpp` | `game5/engine/game/army-steal.ts`, `army.ts` | ARMY_PILOT_DOOR armyHeliDoor ARMY_HELI_SOURCE (taken at the pilot's door) and the ram, the fall and the wreck |
| `games/fall_line/sim/army_steal.hpp` | `game5/engine/game/army-steal.ts` | armyHeliDoor ARMY_HELI_SOURCE |
| `games/fall_line/sim/army_steal_rules.hpp` | `game5/engine/game/defs/army.ts` | ARMY (the ram and the fall) |
| `games/fall_line/sim/army_troop.cpp` | `game5/engine/game/army-troop.ts` | goToward stopHere watchProgress slopeAlong wayToward |
| `games/fall_line/sim/army_troop.hpp` | `game5/engine/game/army-troop.ts` | goToward stopHere watchProgress slopeAlong |
| `games/fall_line/sim/free.cpp` | `game5/engine/game/skied.ts`, `course.ts` | runUnder noteRun noteSkied leftRunPoint skiedResetPoint freeSpawn |
| `games/fall_line/sim/free.hpp` | `game5/engine/game/skied.ts`, `course.ts` | runUnder noteRun noteSkied leftRunPoint skiedResetPoint freeSpawn |
| `games/fall_line/sim/gunfire.cpp` | `game5/engine/game/gunfire.ts` | gunDraw armGun disarmGun rulesOf gunReady fireAt flightTime speedAt energyAt aimPoint stepGunfire fireRound flyRound wound (the rockets and shells, the machines' damage, the cabin share and the player's own gun not carried) |
| `games/fall_line/sim/gunfire.hpp` | `game5/engine/game/gunfire.ts` | armGun fireAt stepGunfire flightTime speedAt energyAt |
| `games/fall_line/sim/gunfire_body.cpp` | `game5/engine/game/gunfire-body.ts` | bodyCentre capsuleHit BONES fromBehind bodyHit passDistance (the points off the standing stance here, not the thrown body's) |
| `games/fall_line/sim/gunfire_body.hpp` | `game5/engine/game/gunfire-body.ts` | bodyHit passDistance |
| `games/fall_line/sim/gunfire_state.hpp` | `game5/engine/game/gunfire-state.ts` | Gun Round GunfireState AimTarget FireRules |
| `games/fall_line/sim/heli_outing.cpp` | `game5/engine/sim/bot.ts`, `game5/engine/game/heli-pilot.ts` | the bot on the skid (pilotControls to an aim); the outing to a run's head and down on skis |
| `games/fall_line/sim/heli_outing.hpp` | `game5/engine/sim/bot.ts` | the bot on the skid |
| `games/fall_line/sim/heli_ride.cpp` | `game5/engine/game/heli.ts`, `heli-pad.ts` | stepHeli controlsFor crash park hold release drop heliWithin board startAgain openSpotNear leanOver clearAt summitward |
| `games/fall_line/sim/heli_ride.hpp` | `game5/engine/game/heli.ts`, `heli-pad.ts`, `heli-state.ts`, `defs/heli.ts` | stepHeli heliWithin heliDown HeliPhaseEvent HELI.pad HELI.board HELI.drop HELI.home HELI.crash |
| `games/fall_line/sim/lift_ride.cpp` | `game5/engine/game/lift-ride.ts`, `lift-board.ts` | stepLiftRide stepChairWait carrierPassing letGo setOff (the boarding ring, the queue, the gondola's platform, the hands, the swing and the skip not carried) |
| `games/fall_line/sim/lift_ride.hpp` | `game5/engine/game/lift-ride.ts`, `lift-board.ts` | stepLiftRide stepChairWait letGo |
| `games/fall_line/sim/para_outing.cpp` | `game5/engine/game/para-pilot.ts`, `game5/engine/sim/bot.ts` | paraPilot (the outing's stretch, spiral and final are the engine's own) |
| `games/fall_line/sim/para_outing.hpp` | `game5/engine/game/para-pilot.ts` | paraPilot |
| `games/fall_line/sim/para_ride.cpp` | `game5/engine/game/para.ts`, `step.ts` | paraStartOf freshPara startPara paraControls paraHeld paraPress (the free ride's para option) |
| `games/fall_line/sim/para_ride.hpp` | `game5/engine/game/para.ts` | paraStartOf startPara paraRigged paraHeld paraPress |
| `games/fall_line/sim/sled_outing.cpp` | `game5/engine/sim/bot.ts`, `game5/engine/game/sled-pilot.ts` | the bot on the boards (sledPilot to an aim); the outing up a run and down on skis is the engine's own |
| `games/fall_line/sim/sled_outing.hpp` | `game5/engine/sim/bot.ts` | the bot on the boards |
| `games/fall_line/sim/sled_ride.cpp` | `game5/engine/game/sled.ts`, `sled-pad.ts` | stepSled board hop throwOff leave remount mount hold letGo sledWithin sledControls parkedAt freshSled sledSpotOf findSpot clearAt leanOver (the fire, the wreck, the stolen machines, the rocks, the posts, the grace and the log buck not carried) |
| `games/fall_line/sim/sled_ride.hpp` | `game5/engine/game/sled.ts`, `sled-pad.ts`, `sled-state.ts`, `defs/sled.ts` | stepSled sledWithin sledControls SledMode SledPhaseEvent SLED.pad SLED.board SLED.hop SLED.idleFor |
| `games/fall_line/sim/wanted.cpp` | `game5/engine/game/wanted.ts` | raiseHeat setWanted arrestSkier seesQuarry throughWoods underCover stepWanted groundWave wavesSent |
| `games/fall_line/sim/wanted.hpp` | `game5/engine/game/wanted.ts` | quarryOf raiseHeat setWanted arrestSkier arrestedOf survivedOf rosterOf groundWave wavesSent seesQuarry throughWoods underCover stepWanted |
| `games/fall_line/sim/wanted_rules.hpp` | `game5/engine/game/defs/wanted.ts` | WANTED WantedRoster WaveMix GroundWaves PursuerKind WantedCause |
| `games/fall_line/sim/wanted_state.hpp` | `game5/engine/game/wanted-state.ts` | Pursuer WantedEnd WantedState WantedEvent |
| `games/fall_line/sim/weapons_rules.hpp` | `game5/engine/game/defs/weapons.ts` | WEAPONS GUNFIRE (the guns: rifle, lmg, doorGun) |
| `games/fall_line/sound/bank.cpp` | `game5/pwa/src/game/audio/bank.ts` | RUN_BANK |
| `games/fall_line/sound/bank.hpp` | `game5/pwa/src/game/audio/bank.ts` | RUN_BANK |
| `games/fall_line/sound/listener.hpp` | `game5/pwa/src/game/audio/listener.ts` | Listener LISTENERS listenerFor |
| `games/fall_line/sound/mix.hpp` | `game5/pwa/src/game/settings.ts`, `audio/bus.ts` | mixOf setAudioVolumes |
| `games/fall_line/sound/route.cpp` | `game5/pwa/src/game/audio/route.ts` | soundForEvent soundsForStep |
| `games/fall_line/sound/route.hpp` | `game5/pwa/src/game/audio/route.ts` | soundForEvent soundsForStep |
| `plugins/helicopter/heli.cpp` | `game5/engine/game/heli.ts`, `heli-rotor.ts` | stepHeli crashHeli heliDown heliWithin freshHeli thrustMost inducedFlow |
| `plugins/helicopter/heli.hpp` | `game5/engine/game/heli-state.ts`, `heli.ts` | HeliState HeliControls heliQuat discQuat heliPoint heliMass |
| `plugins/helicopter/pilot.cpp` | `game5/engine/game/heli-pilot.ts` | pilotControls pilotInput |
| `plugins/helicopter/pilot.hpp` | `game5/engine/game/heli-pilot.ts` | pilotControls HeliAim |
| `plugins/helicopter/spec.hpp` | `game5/engine/game/defs/heli.ts` | HELI |
| `plugins/helicopter/wash.cpp` | `game5/engine/game/heli-wash.ts` | washAt inducedOf |
| `plugins/helicopter/wash.hpp` | `game5/engine/game/heli-wash.ts` | washAt |
| `plugins/mountain/analysis/cliffs.cpp` | `game5/engine/analysis/cliffs.ts` | checkCliffs checkDrops |
| `plugins/mountain/analysis/cliffs.hpp` | `game5/engine/analysis/cliffs.ts` | checkCliffs checkDrops |
| `plugins/mountain/analysis/course.cpp` | `game5/engine/analysis/index.ts` | analyzeLevel (a resort's course, `network: false`; the engine's own checks) |
| `plugins/mountain/analysis/cross_course.cpp` | `game5/engine/mapgen/ski-cross.ts` | the course's rule (R35) as its own analysis; the reference has no ski-cross assertions |
| `plugins/mountain/analysis/crossings.cpp` | `game5/engine/analysis/crossings.ts` | selfCrossings |
| `plugins/mountain/analysis/crossings.hpp` | `game5/engine/analysis/crossings.ts` | selfCrossings |
| `plugins/mountain/analysis/index.cpp` | `game5/engine/analysis/index.ts` | analyzeLevel |
| `plugins/mountain/analysis/index.hpp` | `game5/engine/analysis/index.ts` | analyzeLevel |
| `plugins/mountain/analysis/park.cpp` | `game5/engine/analysis/trick-field.ts` | checkTrickField (the park's count, built shape, landing drop, pitch, lead, gaps, gates and run-in) |
| `plugins/mountain/analysis/race.cpp` | `game5/tests/slalom_test.ts` | the slalom course's assertions, as R31's analysis |
| `plugins/mountain/analysis/race.hpp` | `game5/engine/mapgen/race-course.ts` | raceCourseOf |
| `plugins/mountain/analysis/race_checks.hpp` | `game5/tests/downhill_test.ts` | the course checks the speed events share with the slalom |
| `plugins/mountain/analysis/resort.cpp` | `game5/engine/analysis/resort.ts` | analyzeResort (the engine's own subset of its checks) |
| `plugins/mountain/analysis/resort.hpp` | `game5/engine/analysis/resort.ts` | analyzeResort |
| `plugins/mountain/analysis/routes.cpp` | `game5/engine/mapgen/ski-routes.ts` | the R42 judging, re-held on the published routes (the reference has no route analysis) |
| `plugins/mountain/analysis/routes.hpp` | `game5/engine/mapgen/ski-routes.ts` | judge |
| `plugins/mountain/analysis/shared.cpp` | `game5/engine/analysis/index.ts` | analyzeLevel (R13, R15, R19) |
| `plugins/mountain/analysis/shared.hpp` | `game5/engine/analysis/index.ts` | analyzeLevel (R13, R15, R19) |
| `plugins/mountain/analysis/speed_race.cpp` | `game5/tests/downhill_test.ts` | the downhill, super-G and giant slalom assertions (also super_g_test.ts, giant_slalom_test.ts), as R32, R33 and R36's analysis |
| `plugins/mountain/analysis/speed_track.cpp` | `game5/tests/speed_ski_test.ts` | the speed track's assertions, as R34's analysis |
| `plugins/mountain/labs/dem_tiles.cpp` | `game5/scripts/lib/dem-tiles.mjs` | openTiff createDem tileName fetchTile heightAt |
| `plugins/mountain/labs/dem_tiles.hpp` | `game5/scripts/lib/dem-tiles.mjs` | createDem tileName fetchTile heightAt |
| `plugins/mountain/labs/fetch.cpp` | `game5/scripts/real-hints.mjs`, `game5/scripts/real-faces.mjs` | a download to a file through curl |
| `plugins/mountain/labs/junctions_lab.cpp` | `game5/scripts/junctions-lab.mjs` | the steps by kind, the sheet (read down each run's line, not every packed cell) |
| `plugins/mountain/labs/osm_data.cpp` | `game5/scripts/real-hints.mjs` | the map API's tiles and relations, read and cached (the XML read by the lab's own scanner) |
| `plugins/mountain/labs/park_lab.cpp` | `game5/scripts/level-map.mjs` | the park drawn (the close-up and the detrended profile are the engine's own) |
| `plugins/mountain/labs/real_faces_lab.cpp` | `game5/scripts/real-faces.mjs`, `game5/scripts/lib/real-face-files.mjs` | crop smooth encode measure writeFaceFile writeFaceIndex (the crop search not carried) |
| `plugins/mountain/labs/real_hints_lab.cpp` | `game5/scripts/real-hints.mjs`, `game5/scripts/lib/real-face-forest.mjs`, `game5/scripts/lib/real-face-files.mjs` | fetch parse lifts pistes woods encode writeHintFile writeHintIndex (houses, streets, the town and water not carried) |
| `plugins/mountain/labs/real_maps_lab.cpp` | `game5/scripts/resort-map.mjs`, `game5/tests/real_faces_test.ts` | every face built on a seed: the attempt, the build time, what it carries |
| `plugins/mountain/labs/resort_lab.cpp` | `game5/scripts/resort-map.mjs` | the plan, the listing (no panorama or charts yet) |
| `plugins/mountain/labs/resort_map.cpp` | `game5/scripts/lib/resort-draw.mjs` | renderResortPlan |
| `plugins/mountain/labs/resort_map.hpp` | `game5/scripts/lib/resort-draw.mjs` | renderResortPlan |
| `plugins/mountain/labs/routes_lab.cpp` | `game5/scripts/level-map.mjs` | the routes drawn (the close-up and the side view are the engine's own) |
| `plugins/mountain/labs/ski_cross_lab.cpp` | `game5/scripts/level-map.mjs` | the course drawn (the close-up and the detrended profile are the engine's own) |
| `plugins/mountain/labs/skijump_hills_lab.cpp` | `game5/scripts/skijump-hills-lab.mjs` | measure (tower raised dig fill cut rms arena drop); the pictures are the engine's own |
| `plugins/mountain/labs/slalom_lab.cpp` | `game5/scripts/level-map.mjs` | the course drawn (the straightened panel is the engine's own) |
| `plugins/mountain/labs/speed_events_lab.cpp` | `game5/scripts/level-map.mjs` | the course drawn (the straightened panel is the engine's own) |
| `plugins/mountain/labs/speed_ski_lab.cpp` | `game5/scripts/level-map.mjs` | the track drawn (the side profile is the engine's own) |
| `plugins/mountain/lifts/lift_loop.cpp` | `game5/engine/game/lift-line.ts`, `lift-loop.ts` | liftPlans carrierAt carrierSpeedAt carrierClock carrierLoop carrierCount carrierPlace loadAt offAt inLoadZone boardAt nextCarrierIn |
| `plugins/mountain/lifts/lift_loop.hpp` | `game5/engine/game/lift-line.ts`, `lift-loop.ts` | LIFT_LOOK LiftPlan carrierAt carrierPlace inLoadZone |
| `plugins/mountain/mapgen/access_build.cpp` | `game5/engine/mapgen/access-build.ts` |  |
| `plugins/mountain/mapgen/access_build.hpp` | `game5/engine/mapgen/access-build.ts` |  |
| `plugins/mountain/mapgen/berm.hpp` | `game5/engine/mapgen/berm.ts` | bermProfile bermCrest |
| `plugins/mountain/mapgen/cliffs.cpp` | `game5/engine/mapgen/cliffs.ts` | layCliffs stampCliff |
| `plugins/mountain/mapgen/cliffs.hpp` | `game5/engine/mapgen/cliffs.ts` | cliffProfile cliffFootprint onCliff layCliffs stampCliff |
| `plugins/mountain/mapgen/course_day.cpp` | `game5/engine/mapgen/course-day.ts` | courseDay DAY_SALT |
| `plugins/mountain/mapgen/course_day.hpp` | `game5/engine/mapgen/course-day.ts` | courseDay |
| `plugins/mountain/mapgen/course_gates.cpp` | `game5/engine/mapgen/course-gates.ts` | courseGates hashPick chooseCourse |
| `plugins/mountain/mapgen/course_gates.hpp` | `game5/engine/mapgen/course-gates.ts` | courseGates hashPick chooseCourse |
| `plugins/mountain/mapgen/course_prep.cpp` | `game5/engine/mapgen/course-prep.ts` | prepareCourse startDrop combStretch groomStretch shaveCrests clearedTrees |
| `plugins/mountain/mapgen/course_prep.hpp` | `game5/engine/mapgen/course-prep.ts` | Stretch CoursePrep prepareCourse clearedTrees |
| `plugins/mountain/mapgen/courses.cpp` | `game5/engine/mapgen/courses.ts` | composeCourse courseArc runAt nearestOn |
| `plugins/mountain/mapgen/courses.hpp` | `game5/engine/mapgen/courses.ts` | Piece CoursePlan composeCourse courseArc |
| `plugins/mountain/mapgen/digest.cpp` | `game5/engine/mapgen/digest.ts` | levelDigest |
| `plugins/mountain/mapgen/digest.hpp` | `game5/engine/mapgen/digest.ts` | levelDigest |
| `plugins/mountain/mapgen/discipline_rules.hpp` | `game5/engine/mapgen/discipline-rules.ts` | DISCIPLINE_RULES.slalom .downhill .superG .giantSlalom .speedSki |
| `plugins/mountain/mapgen/drags.cpp` | `game5/engine/mapgen/drags.ts` |  |
| `plugins/mountain/mapgen/drags.hpp` | `game5/engine/mapgen/drags.ts` |  |
| `plugins/mountain/mapgen/drift.cpp` | `game5/engine/mapgen/drift.ts` | dealDrifts stampDrifts trimDrifts |
| `plugins/mountain/mapgen/drift.hpp` | `game5/engine/mapgen/drift.ts` | dealDrifts driftAt stampDrifts trimDrifts |
| `plugins/mountain/mapgen/drops.cpp` | `game5/engine/mapgen/drops.ts` | layDrops publishDrops |
| `plugins/mountain/mapgen/drops.hpp` | `game5/engine/mapgen/drops.ts` | layDrops publishDrops |
| `plugins/mountain/mapgen/forest.cpp` | `game5/engine/mapgen/forest.ts` | growForest (and its resort options) |
| `plugins/mountain/mapgen/forest.hpp` | `game5/engine/mapgen/forest.ts` | growForest treeAge trunkRadius ForestOptions |
| `plugins/mountain/mapgen/generate.cpp` | `game5/engine/mapgen/generate.ts` | attemptLevel searchLevel compileLevel normalAt generateResortLevel |
| `plugins/mountain/mapgen/generate.hpp` | `game5/engine/mapgen/generate.ts` | generateLevel subSeed compileLevel |
| `plugins/mountain/mapgen/grades.cpp` | `game5/engine/mapgen/grades.ts` | GRADES UNGRADED steepestSpan dealGrade GRADE_SALT |
| `plugins/mountain/mapgen/grades.hpp` | `game5/engine/mapgen/grades.ts` | GRADES UNGRADED gradeRow pisteGradeOf steepestSpan verticalBand |
| `plugins/mountain/mapgen/hub.cpp` | `game5/engine/mapgen/hub.ts` | planHub hubClear groomHub layTunnels bedOf pressBed hubAt outsideHub EDGE_SCALE BED_EASE |
| `plugins/mountain/mapgen/hub.hpp` | `game5/engine/mapgen/hub.ts` | planHub hubClear groomHub layTunnels hubAt outsideHub HubPlan FloorPoint Hub WindTunnel |
| `plugins/mountain/mapgen/kickers.cpp` | `game5/engine/mapgen/kickers.ts` | layTrackKickers layOffKickers stampKicker |
| `plugins/mountain/mapgen/kickers.hpp` | `game5/engine/mapgen/kickers.ts` | layTrackKickers publishTrackKickers layOffKickers stampKicker onKicker |
| `plugins/mountain/mapgen/lanes.cpp` | `game5/engine/mapgen/lanes.ts` |  |
| `plugins/mountain/mapgen/lanes.hpp` | `game5/engine/mapgen/lanes.ts` |  |
| `plugins/mountain/mapgen/lift_chain.cpp` | `game5/engine/mapgen/lift-chain.ts` | chainBottom chainWay wayTarget wayLevels rectOff pressWay pressBox regradeLeg leaves RAMP_FLANK searchAims AIM_MARGIN AIM_STEP (the real face's `most`, `hold`) |
| `plugins/mountain/mapgen/lift_chain.hpp` | `game5/engine/mapgen/lift-chain.ts` | chainBottom chainWay layChain wayFalls keepsChain chainOf offWay offQueue groomWay pressWay regradeLeg |
| `plugins/mountain/mapgen/lift_line.hpp` | `game5/engine/game/lift-line.ts` | ringFrame letGoOf queueLane LIFT_LOOK CORRAL_TAIL DRAG_ARM |
| `plugins/mountain/mapgen/links.cpp` | `game5/engine/mapgen/links.ts` |  |
| `plugins/mountain/mapgen/links.hpp` | `game5/engine/mapgen/links.ts` |  |
| `plugins/mountain/mapgen/massif.cpp` | `game5/engine/mapgen/massif.ts` | planMassif bakeMassif ridgeShare steepShare benchShare massifRow heightAt readFace faceLift (the town and the hints wait for M4d3) |
| `plugins/mountain/mapgen/massif.hpp` | `game5/engine/mapgen/massif.ts` | planMassif bakeMassif ridgeShare |
| `plugins/mountain/mapgen/net_index.cpp` | `game5/engine/mapgen/net-index.ts` |  |
| `plugins/mountain/mapgen/net_index.hpp` | `game5/engine/mapgen/net-index.ts` |  |
| `plugins/mountain/mapgen/network.cpp` | `game5/engine/mapgen/network.ts` |  |
| `plugins/mountain/mapgen/network.hpp` | `game5/engine/mapgen/network.ts` |  |
| `plugins/mountain/mapgen/network_build.cpp` | `game5/engine/mapgen/network-build.ts` |  |
| `plugins/mountain/mapgen/network_build.hpp` | `game5/engine/mapgen/network-build.ts` |  |
| `plugins/mountain/mapgen/network_stamp.cpp` | `game5/engine/mapgen/network-build.ts` | networkStamp onCore stampRun pastJunction |
| `plugins/mountain/mapgen/network_stamp.hpp` | `game5/engine/mapgen/network-build.ts` | networkStamp onCore stampRun NetworkStamp |
| `plugins/mountain/mapgen/packed.cpp` | `game5/engine/mapgen/real-face.ts`, `game5/engine/mapgen/real-hints.ts` | the zig-zag varint and base64 codec both decoders shared |
| `plugins/mountain/mapgen/park.cpp` | `game5/engine/mapgen/trick-field.ts`, `game5/engine/mapgen/resort-park.ts` | trickKicker planTrickField stampTrickField parkCourse stampPark |
| `plugins/mountain/mapgen/park.hpp` | `game5/engine/mapgen/trick-field.ts` | ParkAsk ParkLine planTrickField stampTrickField |
| `plugins/mountain/mapgen/query.cpp` | `game5/engine/mapgen/query.ts` | nearestTrackPoint nearestWithin |
| `plugins/mountain/mapgen/query.hpp` | `game5/engine/mapgen/query.ts` | nearestTrackPoint nearestWithin trackPointAt |
| `plugins/mountain/mapgen/real_face.cpp` | `game5/engine/mapgen/real-face.ts`, `game5/scripts/lib/real-face-crops.mjs` | decode faceHeight faceMean faceExtreme globeAt mapAt; encode from `game5/scripts/real-faces.mjs` |
| `plugins/mountain/mapgen/real_face.hpp` | `game5/engine/mapgen/real-face.ts`, `game5/engine/mapgen/real-faces-index.ts` | RealFace FaceData FACE_GRID realFace faceHeight globeAt mapAt |
| `plugins/mountain/mapgen/real_face_crops.hpp` | `game5/scripts/lib/real-face-crops.mjs` | FACES SIZE (scale 1 dropped) |
| `plugins/mountain/mapgen/real_faces/` | `game5/engine/mapgen/real-faces/`, `game5/engine/mapgen/real-faces-index.ts` | the faces' heights, baked again by the engine's own bake, the same bits |
| `plugins/mountain/mapgen/real_hints.cpp` | `game5/engine/mapgen/real-hints.ts`, `game5/engine/mapgen/real-face.ts` | decode realHints faceHasHints realFaceTrees faceCoverAt (houses, streets, the town and water not carried) |
| `plugins/mountain/mapgen/real_hints/` | `game5/engine/mapgen/real-hints/`, `game5/engine/mapgen/real-hints-index.ts` | the faces' hints, baked again by the engine's own bake |
| `plugins/mountain/mapgen/real_lean.cpp` | `game5/engine/mapgen/real-hints.ts`, `game5/engine/mapgen/resort.ts`, `game5/engine/mapgen/resort-woods.ts` | leanStation leanLift pisteFor pisteVia pisteAhead alongPiste pisteWidth billedColour |
| `plugins/mountain/mapgen/regions.cpp` | `game5/engine/mapgen/regions.ts` | REGIONS treeKindAt scaleCount |
| `plugins/mountain/mapgen/regions.hpp` | `game5/engine/mapgen/regions.ts` | regionRow treeKindAt |
| `plugins/mountain/mapgen/resort.cpp` | `game5/engine/mapgen/resort.ts` | planResort placeStations SLOTS TILT TALL_TILT ROAD_ROW |
| `plugins/mountain/mapgen/resort.hpp` | `game5/engine/mapgen/resort.ts` | planResort placeStations ROAD_ROW LiftPlan |
| `plugins/mountain/mapgen/resort_access.cpp` | `game5/engine/mapgen/resort-build.ts` | attemptResort planRuns settleDrags clearStations dragTopsOf |
| `plugins/mountain/mapgen/resort_build.cpp` | `game5/engine/mapgen/resort-build.ts` | attemptResort layChain pressPads |
| `plugins/mountain/mapgen/resort_build.hpp` | `game5/engine/mapgen/resort-build.ts` | attemptResort buildResort ResortAttempt |
| `plugins/mountain/mapgen/resort_courses.cpp` | `game5/engine/mapgen/resort-build.ts` | attemptResort (the woods, the day's bearings, the courses) |
| `plugins/mountain/mapgen/resort_features.cpp` | `game5/engine/mapgen/resort-build.ts` | attemptResort distanceTo RAMP_GROOM |
| `plugins/mountain/mapgen/resort_graded.cpp` | `game5/engine/mapgen/resort-build.ts` | attemptResort scaledRow |
| `plugins/mountain/mapgen/resort_level.cpp` | `game5/engine/mapgen/resort-level.ts` | resortLevel |
| `plugins/mountain/mapgen/resort_level.hpp` | `game5/engine/mapgen/resort-level.ts` | resortLevel |
| `plugins/mountain/mapgen/resort_rules.hpp` | `game5/engine/mapgen/resort-rules.ts` | RESORT_RULES TREE_LINE_MASSIF |
| `plugins/mountain/mapgen/resort_woods.cpp` | `game5/engine/mapgen/resort-woods.ts` | WOODS woodsAtDepth tallAtDepth |
| `plugins/mountain/mapgen/resort_woods.hpp` | `game5/engine/mapgen/resort-woods.ts` | WOODS woodsAtDepth tallAtDepth |
| `plugins/mountain/mapgen/route_features.cpp` | `game5/engine/mapgen/route-features.ts` | turnOf stationAt stampGap fellOnRoutes layRouteJumps |
| `plugins/mountain/mapgen/route_features.hpp` | `game5/engine/mapgen/route-features.ts` | fellOnRoutes layRouteJumps |
| `plugins/mountain/mapgen/rules.hpp` | `game5/engine/mapgen/rules.ts` | LEVEL_RULES inBand withinBand bendFloor |
| `plugins/mountain/mapgen/run_start.cpp` | `game5/engine/mapgen/run-start.ts` |  |
| `plugins/mountain/mapgen/run_start.hpp` | `game5/engine/mapgen/run-start.ts` |  |
| `plugins/mountain/mapgen/ski_cross.cpp` | `game5/engine/mapgen/ski-cross.ts` | pickStretch corners lay poseAt baseProfile cutStart rollers jump stepDown layFeatures bankAt across skiCrossCourseOf setSkiCross courseOver |
| `plugins/mountain/mapgen/ski_cross.hpp` | `game5/engine/mapgen/ski-cross.ts` | setSkiCross skiCrossCourseOf SkiCrossCourse CrossFeature |
| `plugins/mountain/mapgen/ski_jump.cpp` | `game5/engine/mapgen/ski-jump.ts` | hillOf buildProfile edgeSpeed designGate jumpDistanceAt measuredDistance setSkiJump cutUnder |
| `plugins/mountain/mapgen/ski_jump.hpp` | `game5/engine/mapgen/ski-jump.ts`, `ski-jump-types.ts` | HillElements SkiJumpProfile skiJumpProfile setSkiJump skiJumpHeightAt |
| `plugins/mountain/mapgen/ski_routes.cpp` | `game5/engine/mapgen/ski-routes.ts` | STEPS DROPS Heap laySkiRoutes threadTrees dropsOn stationsOf steepestAlong |
| `plugins/mountain/mapgen/ski_routes.hpp` | `game5/engine/mapgen/ski-routes.ts` | RouteGround DROP_SPAN steepestAlong laySkiRoutes |
| `plugins/mountain/mapgen/slalom.cpp` | `game5/engine/mapgen/slalom.ts` | slalomStretch layGates layAt dealCombos comboSize fitBends courseOver setSlalom slalomLineAt |
| `plugins/mountain/mapgen/slalom.hpp` | `game5/engine/mapgen/slalom.ts` | SlalomStretch slalomStretch setSlalom slalomLineAt SlalomCourse |
| `plugins/mountain/mapgen/spawn.cpp` | `game5/engine/mapgen/spawn.ts` | chooseStart gridOnTrack layCheckpoints |
| `plugins/mountain/mapgen/spawn.hpp` | `game5/engine/mapgen/spawn.ts` | chooseStart gridOnTrack layCheckpoints startGateArc |
| `plugins/mountain/mapgen/speed_course.cpp` | `game5/engine/mapgen/speed-course.ts` | pisteBendAt trapArc racingLine lineOffset gateArcs speedLineAt |
| `plugins/mountain/mapgen/speed_course.hpp` | `game5/engine/mapgen/speed-course.ts` | LINE_STEP speedLineAt |
| `plugins/mountain/mapgen/speed_events.cpp` | `game5/engine/mapgen/turn-course.ts` | mostVerticalCourseOf loweredStart gateArcs crestsOf turnCourse; downhill.ts downhillCourseOf setDownhill courseOver; super-g.ts superGStart setSuperG; giant-slalom.ts giantSlalomStart setGiantSlalom (clearTowers waits for the lifts' towers on the map) |
| `plugins/mountain/mapgen/speed_events.hpp` | `game5/engine/mapgen/downhill.ts` | downhillCourseOf setDownhill; super-g.ts superGCourseOf superGStart setSuperG; giant-slalom.ts giantSlalomCourseOf giantSlalomStart setGiantSlalom |
| `plugins/mountain/mapgen/speed_ski.cpp` | `game5/engine/mapgen/speed-ski.ts` | hullOf roundProfile readLine tuckDown stopOn sampleAt lerpAt clearOf findTrack arcAlong heightAt zoneSpeed gradeTrack across setSpeedSki runOver speedSkiAim speedSkiLines |
| `plugins/mountain/mapgen/speed_ski.hpp` | `game5/engine/mapgen/speed-ski.ts` | setSpeedSki speedSkiAim speedSkiLines hullOf SpeedSkiCourse |
| `plugins/mountain/mapgen/station_clear.cpp` | `game5/engine/mapgen/station-clear.ts` | bottomFootprint clearStations clearBottom clearDrag candidates standsLevel SLIDES DRAG_TOP DRAG_FOOT |
| `plugins/mountain/mapgen/station_clear.hpp` | `game5/engine/mapgen/station-clear.ts` | bottomFootprint clearStations standsLevel StationGround StationGround.level |
| `plugins/mountain/mapgen/station_pad.cpp` | `game5/engine/mapgen/station-pad.ts` | pressPads levelPad cutApproach unloadRise relevelPads groomPads dragTop |
| `plugins/mountain/mapgen/station_pad.hpp` | `game5/engine/mapgen/station-pad.ts` | padShape pressPads relevelPads groomPads padSurface dragTop dragTopsOf unloadPoint cutApproach |
| `plugins/mountain/mapgen/straight_venue.cpp` | `game5/engine/mapgen/straight-venue.ts` | findLine gradeVenue originalOf nearStations |
| `plugins/mountain/mapgen/straight_venue.hpp` | `game5/engine/mapgen/straight-venue.ts` | VenueProfile VenueRule VenueLine findLine gradeVenue (the pen, the landing shaper and the mogul lines not carried) |
| `plugins/mountain/mapgen/summit_ramps.cpp` | `game5/engine/mapgen/summit-ramps.ts` | rampRoom roomBlocked nearRoom evenFall rampTo planRamps betterRamp overlaps eachCell headIndex layRamps pressRamps groomRamps offOwn nearLine rankRamps chooseRamps JOINT_KEEP JOINT_APART JOINT_TRIES |
| `plugins/mountain/mapgen/summit_ramps.hpp` | `game5/engine/mapgen/summit-ramps.ts` | rampRoom roomBlocked nearRoom RampFit RampRoom RunCover rampFrame rampHeight offRamp layRamps groomRamps nearLine |
| `plugins/mountain/mapgen/sun.cpp` | `game5/engine/mapgen/sun.ts` | dealSun declinationOf faceTheSun sunOffFace |
| `plugins/mountain/mapgen/sun.hpp` | `game5/engine/mapgen/sun.ts` | declinationOf sunWindow dealSun faceTheSun |
| `plugins/mountain/mapgen/surface.cpp` | `game5/engine/mapgen/surface.ts` | layCrust foldSurface |
| `plugins/mountain/mapgen/surface.hpp` | `game5/engine/mapgen/surface.ts` | layCrust foldSurface |
| `plugins/mountain/mapgen/terrain.cpp` | `game5/engine/mapgen/terrain.ts` | planTerrain bakeCountry |
| `plugins/mountain/mapgen/terrain.hpp` | `game5/engine/mapgen/terrain.ts` | planTerrain bakeCountry descentAt flankAt |
| `plugins/mountain/mapgen/track.cpp` | `game5/engine/mapgen/track.ts` | drawPiste gradePiste stampCorridor bendRoom |
| `plugins/mountain/mapgen/track.hpp` | `game5/engine/mapgen/track.ts` | drawPiste gradePiste stampCorridor tightestBend windowGrades minSeparation |
| `plugins/mountain/mapgen/types.hpp` | `game5/engine/mapgen/types.ts` | Level TrackPoint Checkpoint Kicker Cliff TreeDef Weather Sun GenerateOptions SkiJumpHill VenueSurface |
| `plugins/mountain/mapgen/venue_rules.hpp` | `game5/engine/mapgen/trick-rules.ts` | R37 R43 SKI_JUMP_RULE |
| `plugins/mountain/mapgen/versions.hpp` | `game5/engine/mapgen/versions.ts` | generatorTraits GENERATOR_VERSIONS CURRENT_GENERATOR_VERSION singlePiste |
| `plugins/mountain/mapgen/weather.cpp` | `game5/engine/mapgen/weather.ts` | dealWeather windIn |
| `plugins/mountain/mapgen/weather.hpp` | `game5/engine/mapgen/weather.ts` | dealWeather sunsetOf WEATHER_KINDS |
| `plugins/paramotor/acro.cpp` | `game5/engine/game/para-acro.ts`, `para.ts` | keptWay riserRig riserLift biteRears reflexOf heldOf heldShare holdPitch countSwing fallenIn readLoad stepTangled (hangOnLines left to the renderer) |
| `plugins/paramotor/acro.hpp` | `game5/engine/game/para-acro.ts`, `defs/para.ts` | PARA.acro |
| `plugins/paramotor/air.cpp` | `game5/engine/game/para-air.ts` | paraAirAt eddyUp levelWind |
| `plugins/paramotor/air.hpp` | `game5/engine/game/para-air.ts` | paraAirAt ParaAir |
| `plugins/paramotor/para.cpp` | `game5/engine/game/para.ts`, `canopy.ts` | stepPara dropPara readyPara (the wing's polar, folds and lines restated) |
| `plugins/paramotor/para.hpp` | `game5/engine/game/para-state.ts`, `para.ts` | ParaState ParaControls |
| `plugins/paramotor/pilot.cpp` | `game5/engine/game/para-pilot.ts` | paraPilot |
| `plugins/paramotor/pilot.hpp` | `game5/engine/game/para-pilot.ts` | paraPilot ParaAim |
| `plugins/paramotor/spec.hpp` | `game5/engine/game/defs/para.ts` | PARA |
| `plugins/racing/records.hpp` | `oss-game-framework/src/racing/records.ts` | splitGap noteRecord |
| `plugins/racing/standings.hpp` | `oss-game-framework/src/racing/standings.ts` | legProgress fieldOrder placeAmong |
| `plugins/racing/tape.cpp` | `oss-game-framework/src/racing/tape.ts` | encodeStream decodeStream |
| `plugins/racing/tape.hpp` | `oss-game-framework/src/racing/tape.ts` | snapAxis ControlTape |
| `plugins/skier/anatomy.cpp` | `game5/engine/game/defs/anatomy.ts` | BODY_PARTS INJURIES (the blunt, load, drawer, twist and bend rows) |
| `plugins/skier/anatomy.hpp` | `game5/engine/game/defs/anatomy.ts` | BodyPart Mechanism Facing InjuryDef BONE_KINDS ORGAN_KINDS INJURY |
| `plugins/skier/app/ride_bed.cpp` | `game5/pwa/src/game/audio/ride-bed.ts` | createRideBed |
| `plugins/skier/app/ride_bed.hpp` | `game5/pwa/src/game/audio/ride-bed.ts` | RideBed createRideBed |
| `plugins/skier/app/snow_bed.cpp` | `game5/pwa/src/game/audio/snow-voice.ts` | skiVoiceOf snowTargets |
| `plugins/skier/app/snow_bed.hpp` | `game5/pwa/src/game/audio/snow-voice.ts` | SkiVoice SnowVoice SnowMix SnowLayer SNOW_LAYERS SNOW_GLIDE snowTargets |
| `plugins/skier/app/wind_bed.cpp` | `game5/pwa/src/game/audio/wind-voice.ts` | windTargets WIND_FULL WIND_TOP |
| `plugins/skier/app/wind_bed.hpp` | `game5/pwa/src/game/audio/wind-voice.ts` | WindVoice WindMix WindLayer WIND_LAYERS WIND_GLIDE windTargets |
| `plugins/skier/body.cpp` | `game5/engine/game/body.ts`, `game5/engine/game/body-bones.ts`, `game5/engine/game/gore.ts` | blowOf riskOf energyOver trunkOnSkis landing fall judge struckOn sideOf severityOf diesOf mortality |
| `plugins/skier/body.hpp` | `game5/engine/game/body.ts`, `game5/engine/game/state.ts` | BodyState Injury Impact stepBody mendBody |
| `plugins/snowmobile/body.cpp` | `game5/engine/game/sled-body.ts` | rideSled hullContacts landingPitch deriveSled sledLockAt |
| `plugins/snowmobile/body.hpp` | `game5/engine/game/sled-body.ts` | rideSled SledStepReport deriveSled |
| `plugins/snowmobile/drive.cpp` | `game5/engine/game/sled-drive.ts` | powerShare rpmAtTop maxDriveForce driveForce rpmGoal stepBelt stepRpm |
| `plugins/snowmobile/drive.hpp` | `game5/engine/game/sled-drive.ts` | powerShare driveForce stepBelt stepRpm |
| `plugins/snowmobile/pilot.cpp` | `game5/engine/game/sled-pilot.ts` | sledPilot dodge |
| `plugins/snowmobile/pilot.hpp` | `game5/engine/game/sled-pilot.ts` | sledPilot SledAim |
| `plugins/snowmobile/sled.cpp` | `game5/engine/game/sled.ts` | sledSolids sledBounds judgeLanding standSled riderFrame |
| `plugins/snowmobile/sled.hpp` | `game5/engine/game/sled-state.ts`, `sled.ts` | SledState SledControls SledContact standSled |
| `plugins/snowmobile/spec.hpp` | `game5/engine/game/defs/sled.ts` | SLED SLED_PROBES sledMass sledInertia SledProbe SledSuspension |
| `tests/fall_line_army_door_test.cpp` | `game5/tests/army_test.ts` | the door guns by the rules of fire |
| `tests/fall_line_army_steal_test.cpp` | `game5/tests/army_test.ts` | a machine taken at its pilot's door and one rammed down |
| `tests/mountain_generate_test.cpp` |  | generateLevel levelDigest analyzeLevel |
| `tests/mountain_park_test.cpp` | `game5/engine/mapgen/trick-field.ts` | the terrain park (R20) |
| `tests/mountain_real_face_test.cpp` | `game5/tests/real_faces_test.ts` | every face decodes to a mountain over its floor; a face builds a clean map |
| `tests/mountain_real_hints_test.cpp` | `game5/tests/real_faces_test.ts` | every face's hints decode onto its map; the codec round trips |
| `tests/mountain_ski_cross_test.cpp` | `game5/engine/mapgen/ski-cross.ts` | the ski-cross course (R35) |
| `tests/mountain_ski_jump_test.cpp` | `game5/tests/ski_jump_test.ts` | the ski jumping hill (R43) |
| `tests/mountain_ski_routes_test.cpp` | `game5/engine/mapgen/ski-routes.ts` | the ski routes (R42) |
| `tests/mountain_slalom_test.cpp` | `game5/tests/slalom_test.ts` | the slalom course (R31) |
| `tests/mountain_speed_events_test.cpp` | `game5/tests/downhill_test.ts` | the speed events' courses (also super_g_test.ts, giant_slalom_test.ts) |
| `tests/mountain_speed_ski_test.cpp` | `game5/tests/speed_ski_test.ts` | the speed track (R34) |
