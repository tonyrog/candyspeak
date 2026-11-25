# Safety examples

Six small controllers, each after a requirement from a safety standard. Each
one has a test in `tests/unit/` (the stimulus in a `.dat`, the checks in an
`.expect`), and properties that varp proves. They are written AFTER the
standards' ideas, as demonstrations; none of them claims compliance.

| Example | After | The requirement |
|---|---|---|
| `throttle.csp` | ISO 26262 (`examples/throttle_26262.c`) | two redundant pedal sensors cross-checked; debounced faults; limp home for good |
| `brake_override.csp` | NHTSA's proposed amendment of FMVSS No. 124 (2012) | the brake beats the accelerator, with no surge when it lifts |
| `two_hand.csp` | ISO 13851 (was EN 574), type III | both hands within 0.5 s, a release stops, both released before the next stroke |
| `reversing.csp` | IEC 60204-1 | forward and reverse contactors interlocked, a dead time between |
| `heartbeat.csp` | CiA 301 heartbeat consumer, IEC 61508-7 watchdog | a peer that stops beating is lost, and its output goes safe |
| `lift_door.csp` | EN 81-20 | moves only with the doors closed, stops when one opens, opens only level at a landing |

Run one:

    ./csp -c 60 examples/safety/throttle.csp
    escript tests/run_tests.escript tests/unit        # all the tests

## In candid's solver

Open the example, open the solver, paste its lines and press prove. A
`reach` that is *reached* shows a trace (the program can do it). A `reach`
that is *never* is proved impossible, for every input, forever, not just
for the inputs a test tried. A proved `invariant` is assumed by the lines
after it, which is why some examples prove their invariants first.

A name in a `reach` is the value before the cycle, `X'` the value after it.

**throttle.csp**

    invariant Faults <= MAX_FAULTS
    invariant State != FAILSAFE
    reach State' == limp                                            // reached, cycle 5
    reach State == limp && State' != limp                           // never: limp home is for good
    reach State == normal && State' == limp && Faults < MAX_FAULTS  // never: only through the faults

**brake_override.csp**

    reach Throttle' == 100                    // reached, cycle 1
    reach Brake && Throttle' != 0             // never: the brake always wins
    reach Bto && Pedal > 0 && Throttle' != 0  // never: no surge when the brake lifts

**two_hand.csp**

    reach Run'                         // reached, cycle 2
    reach Run' && !(Left && Right)     // never: a release always stops the stroke
    reach !Run && Run' && !Armed       // never: both released before the next
    reach !Run && Run' && Late         // never: no stroke after a late second hand

**reversing.csp**

    invariant !KFwd || State == fwd
    invariant !KRev || State == rev
    invariant !(KFwd && KRev)                  // the interlock
    reach State == stopped && State' == rev    // reached, cycle 2
    reach Wait && State' != stopped            // never: the dead time is waited out
    reach State == fwd && State' == rev        // never: no direct reversal

**heartbeat.csp**

    invariant Good <= BELIEVE
    reach Enable'                       // reached, cycle 6: three beats
    reach Lost && Enable'               // never: lost is never enabled
    reach timeout(Watch) && !Lost'      // never: a missed beat is always lost

**lift_door.csp**

    invariant !Up || State == up
    invariant !Down || State == down
    invariant !Open || State == opening
    invariant !(Up && Down)
    invariant !(Open && (Up || Down))
    reach State == up && State' == opening     // reached, cycle 3
    reach !DoorClosed && (Up' || Down')        // never: not with a door open
    reach !Open && Open' && !InZone            // never: opens only level

## Plant a bug

The point of the demo: break the program a little and the solver finds the
way in, with a trace.

- `reversing.csp`: drop `!Wait &&` from the `State = rev` rule. Then
  `reach Wait && State' != stopped` is reached in cycle 4: reverse closes
  inside the dead time.
- `two_hand.csp`: drop `Armed &&` from the `Run = 1` rule. Then
  `reach !Run && Run' && !Armed` is reached in cycle 4: hold one button, tap
  the other, and it strokes again.
- `lift_door.csp`: delete the `State = idle, Up = 0 ? !DoorClosed` line.
  Then `reach !DoorClosed && (Up' || Down')` is reached in cycle 3: the car
  keeps going with a door open.

## What varp does not see

`throttle.csp` computes the throttle with `/`, which the translation does
not model, so `Throttle` is free there: nothing can be proved about its
value, only about the states and the fault count. In the other examples no
variable is free.
