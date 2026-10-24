# Input devices

How a player's hands reach a game: keyboards, mice, gamepads, joysticks and HOTAS, steering
wheels with pedals and shifters, touch screens, and what the game tells them back (rumble,
lights, force feedback). This page is the design. The procedures are the `input` skill's, and
the milestone rows are [`goals.md`](goals.md) M19a–M19e.

The engine is genre-agnostic, so input is too. A skiing game needs keys that ramp like a hand
and a pad's sticks. A first-person game needs raw mouse deltas, every key bindable and a
console's text. A racing game needs a wheel's lock, three pedals, a shifter and a motor that
pushes back. A flight game needs a stick, a twist, a throttle and its hat. All of these go
through one layer, and a game only names the ACTIONS it wants.

## The layers

```
games/<game>                its actions (ActionDef) and its default bindings file
        │
engine/play/devices         a person's actions become the play contract's axes, per step
        │
engine/input                the device model, events, the state, shapes, actions and contexts,
        │                   rebinding, the device database, touch controls, players, feedback,
        │                   the event log, the simulated backend. No platform: pure C++
        ▲
backends                    feed it events and play its feedback:
  engine/platform (SDL3)    keyboards, raw mice, gamepads, joysticks, wheels, haptics, touch,
                            on macOS, Linux and Windows (M19b; the phones with M11):
                            platform/devices.hpp
  engine/input/sim          simulated devices on a virtual clock: tests and labs (M19a)
  an event log              a recorded session played back (M19a)
  Steam Input               a backend of its own, from the Steam integration work
```

`engine/input` (`ge_input`, namespace `ge::input`) depends on `engine/core` alone. A backend is
anything that writes a frame's events into a `Queue`. The platform's backend is the only code
that includes SDL; the simulated one runs anywhere, which is why every test and lab of this
layer runs in the GPU-less Linux container with no device plugged in.

## The model

| Piece | File | What |
| --- | --- | --- |
| Device | `device.hpp` | `DeviceInfo`: its kind (keyboard, mouse, gamepad, joystick, wheel, flight stick, throttle, pedals, touch), its family (how its face buttons are labelled), USB ids, a stable model key, counts and `Feature` bits (rumble, trigger rumble, light, player lights, gyro, accelerometer, touchpad, force, battery, raw motion). A gamepad's controls are POSITIONAL (`south`, `left_x`), so one binding works on every pad |
| Event | `event.hpp` | One 32-byte POD for everything a device does: keys (HID usages), text, raw mouse motion, buttons, the wheel, pad and joystick buttons and axes, hats, the gyro and accelerometer, fingers on a screen or a pad's touchpad, the battery, plug and unplug |
| State | `state.hpp` | A frame's events folded into held, pressed and released per device. A tap shorter than a frame is pressed and released in that frame. Keys are pooled into one keyboard, motion into one mouse (summed in doubles, so a high-rate mouse loses nothing); every other device has a slot. The last used device is tracked for prompts |
| Names | `names.hpp` | Every key, button, axis, kind and event type spelled both ways; `pad_label` says what each family prints on a button (south is "A", "cross" or "B") |
| Shapes | `shape.hpp` | Dead zones (axial, radial, scaled radial, sloped), the outer edge, a response curve, the anti-dead zone, thresholds with hysteresis, and the key ramp (a held key eased toward full like a hand) |
| Actions | `actions.hpp` | A game's actions (button, axis, stick, delta), bound in CONTEXTS to sources; `evaluate` reads the state through them once a frame |
| Bindings file | `bindings_text.hpp` | The map as text: what a player's rebinding saves and what a game ships as its defaults |
| Rebinding | `rebind.hpp` | Capture the next deliberate input as a source; name its conflicts; put it on an action |
| Device database | `device_db.hpp` | What a wheel's, pedals', stick's or throttle's raw axes MEAN: roles (steer, throttle, brake, clutch, handbrake, rudder, toe brakes, twist, slider, the thumb stick) with their ranges, and the buttons that are gears and paddles; learned by moving each control |
| Touch controls | `touch.hpp` | Zones of the screen as floating or fixed sticks, buttons, levers and drags, read by bindings as `touch:<name>` |
| Players | `players.hpp` | Which device is whose: join on press, a keyboard brings its mouse, an unplugged device leaves its player waiting and comes back to him |
| Feedback | `feedback.hpp` | Rumble, trigger rumble, light, player lights and force-feedback effects (constant, ramp, the periodic waves, spring, damper, friction, inertia, with envelopes), as a desired state per device |
| Event log | `event_log.hpp` | A session's device events as text, frame by frame: recorded and replayed to the same state |
| Simulated backend | `sim.hpp` | Devices in code on a virtual clock, and a simulated wheel rim that the feedback turns |

## Actions, not keys

A game states its actions once in code and never reads a key:

| Kind | Value | Typical sources |
| --- | --- | --- |
| `button` | held, pressed, released | a key, a pad button, a trigger past its threshold, a touch button, a wheel's paddle |
| `axis` | -1 .. 1, or 0 .. 1 for a lever | a stick's axis through its shape, a trigger, a pedal's role, two keys ramped |
| `stick` | two values in the unit disc | a pad's stick through its stick shape, four keys, a touch stick |
| `delta` | two unbounded values a frame | the raw mouse, the gyro (rad/s over the frame), a stick or a key at a rate, a touch drag |

**Every source of an action is read, all the time.** Buttons are OR'd. On an axis or a stick the
keys' pushes are summed (left and right held together cancel) and ramped, the strongest analog
source wins, and between the two the larger push wins. Deltas add. So a keyboard and a pad
steer together with no switch, the way a player expects.

**Contexts** bundle bindings a game turns on and off (`ride`, `menu`, `console`). A `blocking`
context takes the sources it binds from those after it: while the console is open, a typed `a`
does not steer. **Chords** read a binding only while another source is held (`with=key:left_shift`).

**Per player**, `evaluate` reads only the devices `players.hpp` gives him; with one player
every device is his.

### The bindings file

```
ge-bindings 1
context ride
bind steer key:a value=-1 ramp=6,9
bind steer key:d ramp=6,9
bind steer pad:left_x deadzone=0.12,0.97
bind steer role:steer
bind tuck pad:right_trigger
context play
bind look mouse:x value=0.022
bind look pad:right_stick zone=scaled_radial curve=1.6 rate=180
bind look gyro:y value=-57.29578
bind fire mouse:left
bind fire pad:right_trigger threshold=0.3,0.2
bind sprint key:w with=key:left_shift
context drive
bind clutch role:clutch
bind shift_up role:shift_up
bind shift_up joy:button4 device=046d:c24f
```

A source is one word: `key:<name>`, `mouse:left|…|x|y|wheel_y`, `pad:<button>|<axis>|left_stick`,
`joy:button<N>|axis<N>|hat<N>_up`, `gyro:x|y|z`, `touch:<control>` or `role:<role>`. A trailing
`+` or `-` reads one half of an axis. A mistake names its line and what to write instead.

## Raw mouse, for a first-person game

A first-person look needs the device's own counts: no pointer acceleration, sub-pixel, every
report. The event carries the delta as the device gave it (`aux = 1`: raw), the state sums a
frame's deltas in doubles, and a `delta` action scales them (a sensitivity in degrees per
count). The backend asks for relative mode when the game captures the pointer (M19b, SDL3's
relative mouse mode, which reads raw input on every desktop platform). Because each event is
stamped with its time on the backend's clock, a game that wants mouse input sampled at its own
rate (a usercmd per server tick) can split the frame's events by time; the log keeps the stamps.

## Wheels, pedals, shifters and flight controls

A wheel reports raw axes and buttons, and nothing in the platform says which axis is the clutch
or which end of it is at rest. The device database gives each model (keyed by its USB ids) a row:
each axis's role and range (`lo` is the reading at the role's 0, so a pedal resting at +1 is
`lo = 1, hi = -1`), the buttons that are paddles and gears, and the wheel's lock-to-lock degrees.
A binding then says `role:clutch` and works on every wheel that has a row. A device with no row
gets only what cannot be wrong (a wheel's axis 0 steers; a flight stick's axes 0 and 1 roll and
pitch), and the player TEACHES the rest. For each role in turn, the game asks him to move the
control through its travel; the axis that travelled furthest is the role's, and its rest says
which end is `lo`. The learned row is saved in the same text the shipped rows are. Separate
pedals, a separate shifter, or a throttle quadrant beside a stick are devices of their own with
rows of their own, and one player holds all of them.

**Force feedback** is a desired state per device: up to eight playing effects, a gain and an
autocentre spring. The effects are the classic set (constant, ramp, sine, square, triangle,
saws, spring, damper, friction, inertia) with envelopes, delays and durations, and they are
pure functions of time and the axis's position, speed and acceleration (`effect_force`,
`force_at`, on `core::libm`). A racing game keeps a spring and a damper playing and updates
their numbers with the car's self-aligning torque each step (`update_effect`), so the motor is
never restarted. The platform's backend (`engine/platform/devices.hpp`) plays the sum as ONE
constant force on the device, updated every frame from the wheel's steering axis (its position,
and its speed and acceleration taken frame to frame), with the device's own autocentre switched
off; the simulated wheel plays the same sum on a rim with inertia and friction under a scripted
hand, so a lab charts what the player would feel (`input-ffb`).

## Gamepads: rumble, triggers, light, gyro, touchpad

Gamepads are read in the standard layout (SDL3's mapping database covers every common pad; M19b),
labelled by family for prompts, and told back through `Feedback`: two-motor rumble with a
duration, the triggers' motors, the light's colour and the player-number lights. The gyro reads
in rad/s as a `delta` source (gyro aiming), the accelerometer in m/s², a touchpad's fingers as a
pad's own fingers, and the battery's charge for a HUD.

## Touch

A phone or a tablet has no buttons, so the screen becomes controls (`touch.hpp`). A floating
stick is centred where the thumb lands. A lever starts at its `start` value under the thumb and
moves as the thumb slides, so a skier's tuck can be held full by default and eased off. Buttons
and drags (a look) complete the set. The controls are read by bindings like any other source,
so a touch layout and a pad drive the same actions.

## Players and hot-plug

A device that comes out is not an error. Its slot is freed, whatever it held is released, and
the player who owned it is told (`lost`). The game pauses and asks for it back. A device of the
same model plugged in again returns to him (`returned`) before anyone else may take it. With
several players on one screen, a device joins the first free player when a button on it is
pressed, and a keyboard brings the mouse with it.

## The platform backend

`engine/platform/devices.hpp` (`open_devices`, over SDL3) is the backend a shipped game runs on.
With a window, `poll_input(window, snapshot, devices, queue)` reads the window's events and every
device's in one pump; with none, `pump_devices` does. It plugs one pooled keyboard and mouse,
opens every gamepad SDL maps (positional buttons and axes, which are SDL's numbering; the family
from the pad's type; gyro, accelerometer, touchpad and battery when present) and every other
joystick by SDL's joystick type (wheel, flight stick, throttle, or plain joystick, with raw axes,
buttons and hats), plugs a touch screen when it first reports a finger, and unplugs a device the
moment SDL does: a pad plugged in again is a new device with a new id. A device's `key` is its
USB ids (`vvvv:pppp`), so the device database's rows match the same wheel on every OS.
`set_relative_mouse` captures the pointer for a first-person look (the motion is then raw counts,
SDL's system scale off) and `set_text_input` turns text events on. `play_feedback` sends each
frame's rumble, trigger rumble, light and player lights when they change (a held rumble is resent
before SDL's duration runs out) and streams the force to a force-feedback device.

**Headless**, SDL's virtual joystick driver plugs devices SDL treats as hardware
(`attach_virtual`, then `virtual_axis`, `virtual_button`, `virtual_hat`, `virtual_sensor`,
`virtual_touch`), and what feedback reached them is read back (`virtual_feedback`); `virtual_key`
and `virtual_mouse_motion` push a key and a motion through SDL's own queue for a window's pump.
`tests/platform_devices_test.cpp` and the `input-devices` lab run the whole path that way in the
GPU-less container. Force feedback on a real wheel cannot be exercised there (SDL has no virtual
haptic device); `haptic_level` is the part that can, and M19d measures real wheels.

## Recordable, replayable, testable

There are two records, at two levels:

- **The event log** (`event_log.hpp`, `ge-events 1`) records the DEVICES, frame by frame. A
  session on a real wheel is reproduced on any machine with no wheel, through new bindings or a
  new dead zone. An agent can also write one by hand as a script.
- **The input log** (`play/inputs.hpp`, `ge-inputs 1`) records the AXES a run was stepped on.
  It is what `ge replay` and `ge diverge` read.

A person playing through `engine/play/devices.hpp` writes the second from the first. Their
actions are paired with the spec's axes by name, the values are snapped onto the axes' grid as
an agent's are, and a press reaches exactly one step, however many steps the frame grants.
Everything above the backend is deterministic: the shapes, the ramps and the effects use
`core::libm`, and no clock is read but the events' stamps. So the same events give the same
actions on every machine, and the labs' digests hold them.

## Fall Line played by hand

`games/fall_line/controls` is the first game on the layer, and the shape a game repeats:

- **The actions and the defaults** (`bindings.hpp`). The `ride` context's actions carry the play
  contract's axis names (`steer`, `tuck`, `brake`, `jump`, `reset`), so `play::bind_axes` pairs
  them with no table between. The `app` context holds what only the window does (`camera`,
  `rebind`). The defaults are a bindings file in the source (`kDefaultBindings`): A and D or the
  arrows steer through a ramp (a tap is a small turn), W tucks, S brakes, SPACE jumps, R resets,
  C the camera, F5 rebinds; the left stick, the triggers, south and north on a pad; a wheel's
  steer, throttle and brake roles. A keyboard, a pad and a wheel play at once.
- **The player's file** is `bindings.txt` in the platform's per-user directory
  (`platform::prefs_dir`), the defaults until it exists. A file that does not read keeps the
  defaults and names the line.
- **The rebinding walk** (`rebinding.hpp`): F5 asks for each action in turn (steer left, steer
  right, tuck, brake, jump, reset, camera), captures the next deliberate input, keeps the slot's
  push and ramp, names any other action the source is also on, and saves the file. Escape stops
  it. The menu's keys page (M22d) will draw the same walk.
- **What is felt** (`felt.hpp`): a step's events pick a rumble pulse for every pad with motors (a
  landing by its load, a wipeout, a trunk brushed, a gate missed, another racer met, an injury,
  the finish). A pulse never cuts a stronger one short.
- **The run played by hand** (`by_hand.hpp`) is the play contract's own run, so a person's run is
  stepped, hashed and written down exactly as an agent's is. The `fall_line` window writes it as
  an input log when it closes (`runs/fall_line-<seed>.inputs` in the per-user directory, or
  `--inputs`), and `ge replay` plays it back to its digest. `--bot` hands the skis to the bot
  through the same loop. A sky the `sv_` cvars pick is skied too (the run opened on the map under
  it, `sim::open_run_on`), and that log is not one `ge replay` rebuilds from the seed.
- **A scripted session** (`script.hpp`): `--events=<ge-events file>` plays a device event log
  into the window's loop in place of the devices, and `--record-events` writes one. A scripted
  frame is granted the steps due by its time, counted in whole nanoseconds (`steps_due`), so a
  session steps the same run on every machine at any frame rate. `pad_session` holds an input
  log's axes on a simulated pad; through `kExactPadBindings` (no dead zone, no curve) every step
  reads back the log's own axes, so the bot's run held on a pad finishes the race on the bot's
  digest. `tests/fall_line_controls_test.cpp` and the `by-hand` lab hold that.

## Labs

| Lab | Kind | What it shows |
| --- | --- | --- |
| `input-shape` | picture | Each dead zone's output against the push on a diagonal and near a cardinal; the response curves; the key ramps over time |
| `input-session` | scenario | A scripted session on a simulated keyboard, mouse, pad, wheel with pedals and touch screen, read through a skier's, a shooter's and a racer's action maps at once; written as an event log, read back and replayed to the bit; two players joining and a pad unplugged and returned; rebinding a shooter's jump by pressing |
| `input-ffb` | scenario | Force-feedback effects on the simulated rim under a hand: a spring and a damper bringing it back (the settle time and the overshoot), a spring ringing, a constant pull held straight, a road's rumble |
| `by-hand` | drive | Fall Line's bot's run held on a simulated pad as a device session and played back by hand through the bindings: the steps that read other axes than the bot's (0), the finish and the digest, the log replayed; the pad's stick and trigger against the steer and the tuck; the rumble motors over the run; the default bindings as a table. Writes the session, the run's log and the defaults for the window and `ge replay` |
| `input-devices` | scenario | One script on a pad and a wheel played through SDL's own virtual devices (the platform's backend, headless) and through the simulated backend: a skier's and a racer's actions held to each other frame by frame (the largest gap per action, the 16-bit step); rumble, its end, the light and the player lights reaching the pad; the pad unplugged and plugged in again as a new device |

## Decisions

- **SDL3 is the platform backend** on every platform the engine ships to. It already reads
  keyboards, raw mice, every common gamepad (with its mapping database, rumble, trigger rumble,
  the light, the gyro and the touchpad), joysticks, wheels (its haptic API drives force feedback
  through evdev, DirectInput and IOKit), touch and pens, on macOS, Linux, Windows, iOS, iPadOS
  and Android, and it is already the engine's window. A backend of our own per OS would repeat
  that work for no gain. SDL stays behind `engine/platform`, and nothing above it includes it.
- **The model is ours, not the backend's.** Positional pad buttons, pooled keyboards, the
  32-byte event and the desired-state feedback are this engine's, so a second backend (Steam
  Input, a console's SDK) plugs in by writing events and reading feedback.
- **Actions combine every source.** There is no "active device" switch to get wrong. The last
  used device exists only to choose which prompts to draw.
- **Rows, not guesses, for wheels.** Their axis layouts vary by model and by driver. A row is
  either shipped from a measured device or learned by the player, and is never invented.
- **The force is a pure function, streamed.** Effects are evaluated in the engine, on our libm,
  so the simulated wheel, the charts and the real wheel all get the same force, and the backend
  streams it as one constant effect the driver holds between updates. The cost is the update
  rate: a spring evaluated at the frame rate (or the step's 120 Hz once a game updates it per
  step, M19d) instead of the driver's own loop. Uploading the conditions (spring, damper,
  friction, inertia) to the driver as SDL condition effects is the fallback if a measured wheel
  shows a stiff spring notching or oscillating at that rate (M19d measures it on real wheels).
