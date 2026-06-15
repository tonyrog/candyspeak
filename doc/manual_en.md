---
title: "CandySpeak Manual"
author: "Tony Rogvall"
date: 2026-05-31
geometry: margin=2.5cm
fontsize: 11pt
documentclass: article
header-includes:
  - \usepackage{fancyhdr}
  - \pagestyle{fancy}
  - \fancyhead[L]{CandySpeak}
  - \fancyhead[R]{\thepage}
---

# Introduction

CandySpeak is a reactive programming language designed for embedded systems and microcontrollers. It combines simplicity with powerful abstractions for handling sensors, timers, and I/O.

The language is rule-based - each line describes WHAT should happen and WHEN, not HOW. The system evaluates all rules every cycle and updates outputs based on inputs.

## Core Concepts

- **Variables** - store values between cycles
- **Digital I/O** - buttons, LEDs, relays
- **Analog I/O** - sensors, PWM
- **Timers** - time-based events
- **Modules** - reusable components

# Language Reference

## Declarations

Declarations start with `#` and define program resources.

### Variables

```
#variable <name>[:<bits>] [<type>] [= <value>]
```

Examples:
```
#variable Counter:8 integer = 0
#variable Temperature float = 20.0
#variable Flag = 0
```

### Digital I/O

```
#digital <name> [in|out|inout] [pullup|pulldown] [<port>:]<pin>
```

Port is optional (for microcontrollers with multiple I/O ports).

Examples:
```
#digital Button in pullup 2
#digital Led out 13
#digital Relay out B:7
#digital Status inout C:4
```

### Analog I/O

```
#analog <name>[:<resolution>] [in|out] [pwm] [<port>:]<pin>
```

Resolution is number of bits (default 10).

Examples:
```
#analog Sensor in A0
#analog Dimmer:8 out pwm 9
#analog HighRes:12 in A:1
```

### Timers

```
#timer <name> <period_ms> [= 1]
```

`= 1` starts the timer automatically at program start.

Examples:
```
#timer Blink 500 = 1
#timer Debounce 50
#timer Timeout 5000
```

### Buffers

```
#buffer <name>:<bits> [<type>]
```

A buffer is a block of storage that variables can map into. A regular variable
owns its own value; a buffer is *shared* — several variables can bind to
different bit-fields of the same buffer, and the buffer can also be read and
written directly by byte. Buffers are the building block for frames (CAN), bit
packing, and overlapping data.

```
#buffer Frame:64           // 8 bytes of storage
#buffer Word:16
```

A buffer can be used directly like a variable. The whole buffer is its value
(up to 32 bits — larger frames are accessed by byte or through bound fields):

```
#buffer Status:8
Status = 200 ? 1
```

**Byte access.** Indexing a buffer selects whole bytes:

```
Frame[0] = 52 ? 1          // first byte
Frame[1] = 18 ? 1          // second byte
X = Frame[0..1] ? 1        // bytes 0..1 as one value (little-endian)
```

**Binding variables (bit-fields).** Map a variable onto a *bit* range of a
buffer with `bind`. Writing or reading a bound variable reads/writes the
underlying buffer bits — they alias the same storage. The buffer must be
declared before it is bound.

```
#buffer Frame:16
#variable Speed:8     bind Frame[0..7]    // bits 0..7
#variable Rpm:10 big  bind Frame[8..17]   // bits 8..17, big-endian

Speed = 50 ? 1             // writes into Frame's bits 0..7
```

> **Note:** when indexing a *buffer* directly the index is a **byte**
> (`Frame[1]`), while a `bind` range is in **bits** (`Frame[8..17]`). Direct
> indexing is for convenient whole-byte access; `bind` is for packing
> sub-byte bit-fields.

### CAN frames

A CAN frame is modelled as a buffer that carries a frame id. The signals inside
a frame are ordinary variables bound to bit-fields, so the same `bind`
mechanism describes a frame layout:

```
#buffer Engine:64          // an 8-byte frame layout
#variable Speed:8     bind Engine[0..7]
#variable Rpm:10 big  bind Engine[8..17]
#variable Temp:8      bind Engine[24..31]

Speed = 90 ? 1             // updates the frame
```

Reading a signal decodes its bits from the frame; writing one encodes back into
the frame. *(Attaching a frame id and bus direction to the buffer — so the
frame is transmitted/received automatically — is in development.)*

### Modules

Modules group related functionality for reuse. A module is a template that can be instantiated multiple times with different configurations.

```
#module <name>
  <declarations>
  <rules>
#end
```

#### Module Instantiation

```
#<ModuleName> <instance> [<init>]*
```

Where `<init>` can be:

| Form | Meaning |
|------|---------|
| `field = value` | Set initial value (static) |
| `field <- expr` | Reactive connection (updates when expr changes) |

Init expressions can be mixed freely. Fields marked `in` must be initialized.

#### Example: Full Adder

```
#module Add
#variable A:1 in
#variable B:1 in
#variable Cin:1 in
#variable S:1 out
#variable Cout:1 out

S = A ^ B ^ Cin ? 1
Cout = (A & B) | (Cin & (A ^ B)) ? 1
#end

#Add a0 A=1 B=1 Cin=0
#Add a1 A=0 B=0 Cin <- a0.Cout
#Add a2 A=0 B=1 Cin <- a1.Cout
```

Here `a0` has static inputs, while `a1` and `a2` chain their carry input reactively from the previous adder's carry output.

#### Pin Assignment for I/O

When a module contains `#digital` or `#analog` declarations, the pin number can be omitted in the module and assigned at instantiation:

```
#module Button
#digital Pin in pullup       // no pin number
#variable Out:1 out = 0
#timer T 50

T = 1 ? Pin != Out
Out = Pin ? timeout(T)
#end

#Button btn1 Pin=2           // assign pin at instantiation
#Button btn2 Pin=3
#Button btn3 Pin=4
```

This makes modules reusable across different hardware configurations.

Pin assignment can also override a default:

```
#module Led
#digital Out out 13          // default pin 13
...
#end

#Led led1                    // uses default pin 13
#Led led2 Out=12             // override to pin 12
```

#### Accessing Module Fields

Use dot notation to access a module instance's fields:

```
MainLed = btn1.Out ? 1       // read debounced output
```

## Rules

Rules have the form:

```
<actions> ? <condition>
```

Actions are executed when the condition is true.

### Assignment

Regular assignment - evaluated every cycle:
```
Led = 1 ? Button == 0
Counter = Counter + 1 ? Timer
```

### Reactive Assignment

With `<-` the rule only runs when a variable on the right-hand side changes:

```
Output <- Input * 2
```

The rule above runs only when `Input` changes, not every cycle.

With condition - the rule runs when RHS variable changes AND condition is true:

```
Filtered <- RawValue ? RawValue > Threshold
```

### Multiple Actions

Separate with comma:

```
Led = 1, Counter = Counter + 1 ? Button == 0
```

### Conditions

Conditions can be:

- Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&` (and), `||` (or), `!` (not)
- Timer functions: `timeout(T)`, `elapsed(T)`, `progress(T)`
- Special functions: `changed(x)`, `rising(x)`, `falling(x)`

### Examples

```
Led = ~Led ? timeout(Blink)
```

Toggle LED at each timeout.

```
Output = 1, T = 1 ? Input && !Output
```

Set Output and start timer T when Input goes high.

## Built-in Functions

| Function | Description |
|----------|-------------|
| `timeout(T)` | True for one cycle when timer T expires |
| `elapsed(T)` | Milliseconds since T started |
| `progress(T)` | 0-100, percentage of timer period elapsed |
| `changed(x)` | True if x changed this cycle |
| `rising(x)` | True on 0→1 transition in digital input |
| `falling(x)` | True on 1→0 transition in digital input |
| `cycle()` | Current cycle number |
| `abs(x)` | Absolute value |
| `min(a,b)` | Minimum value |
| `max(a,b)` | Maximum value |
| `print(x)` | Print value |
| `println(x)` | Print with newline |
| `latch(b)` | Hold outputs (1) or release (0) |

## Operators

| Priority | Operator | Description |
|----------|----------|-------------|
| Highest | `~` `!` `-` | Bitwise NOT, logical NOT, negation |
| | `*` `/` `%` | Multiplication, division, modulo |
| | `+` `-` | Addition, subtraction |
| | `<<` `>>` | Bit shift |
| | `<` `<=` `>` `>=` | Comparison |
| | `==` `!=` | Equality |
| | `&` | Bitwise AND |
| | `^` | Bitwise XOR |
| | `|` | Bitwise OR |
| | `&&` | Logical AND |
| Lowest | `||` | Logical OR |

# Linux Tool

## Installation

```bash
git clone <repo>
cd candyspeak
make
```

## Usage

```bash
./csp [options] [file.csp]
```

### Options

| Option | Description |
|--------|-------------|
| `-h` | Show help |
| `-i` | Interactive mode |
| `-n` | Parse only, don't execute |
| `-C` | Show compiled C code |
| `-c N` | Max number of cycles |
| `-T MS` | Max runtime in milliseconds |
| `-t[=0\|1]` | Transaction mode (default on) |
| `-r[=0\|1]` | Reactive mode (default off) |
| `-Q` | Trace variable values |
| `-P` | Debug parser |
| `-L erlang` | Output trace in Erlang format |
| `-I <file>` | Feed inputs from a file (real time, cycle-stamped rows) |
| `-F <file>` | Feed inputs with **simulated time** (see below) |

### Examples

Run a program:
```bash
./csp program.csp
```

Show compiled code:
```bash
./csp -C -n program.csp
```

Run max 100 cycles with tracing:
```bash
./csp -c 100 -Q program.csp
```

Interactive mode:
```bash
./csp -i
```

## Simulated Time (`-F`)

With `-F` the program runs against a **virtual clock** instead of the wall
clock, which makes timer and input timing fully deterministic and instant (no
real waiting). Each input row is stamped with an absolute virtual time in
milliseconds:

```
<time_ms>  <var>=<value>  [<var>=<value> ...]
```

A row is applied once the virtual clock reaches its time. Between rows the clock
jumps straight to the next event (the next input row or the next timer
timeout), so a 1-second timer fires after one step rather than a thousand
cycles — while still advancing at least one tick per cycle so `cycle()` and
time-reading logic always see time move forward.

Example — feed a sensor at two virtual times and let a timer expire:

```
# stimulus.dat
30   Sensor=10
70   Sensor=21
```

```bash
./csp -c 100 -s trace.txt -F stimulus.dat program.csp
```

`csp_time_ms()` returns the virtual clock in this mode, so `timeout(T)`,
`elapsed(T)` and `progress(T)` all behave deterministically. This is the
recommended way to write repeatable tests for timers and analog/digital input.

## Generate Embedded Code

To generate C code for Arduino or other microcontrollers:

```bash
./csp -C -n program.csp > program_code.h
```

Then include this header in your Arduino project along with `csp.h` and the runtime files.

# Advanced Concepts

## Execution Modes

CandySpeak supports different execution modes that trade memory for performance. The default mode is safe and predictable, but for resource-constrained systems or large programs, other modes may be more efficient.

### Transaction Mode (`-t`)

**Default: ON** (`-t1`)

In transaction mode, all changes within a cycle are collected and applied atomically at the end of the cycle. This ensures:

- Rules see consistent state (no partial updates)
- Order of rule evaluation doesn't matter
- Predictable behavior

Disable with `-t0` for lower memory usage, but rules then see intermediate values as they're written.

```bash
./csp -t0 program.csp    # non-transactional mode
./csp -t1 program.csp    # transactional (default)
```

### Reactive Mode (`-r`)

**Default: OFF** (`-r0`)

In reactive mode, only rules whose inputs have changed are evaluated. This is more efficient when:

- Few variables change each cycle
- Program has many rules
- System has limited CPU

The result should be identical to evaluating all rules, but uses more memory for dependency tracking.

```bash
./csp -r1 program.csp    # reactive mode
./csp -r0 program.csp    # evaluate all rules (default)
```

### Combining Modes

The modes can be combined:

| Command | Transaction | Reactive | Use case |
|---------|-------------|----------|----------|
| `./csp` | ON | OFF | Default, safe |
| `./csp -r1` | ON | ON | Many rules, few changes |
| `./csp -t0` | OFF | OFF | Minimal memory |
| `./csp -t0 -r1` | OFF | ON | Low memory + efficiency |

## Api

The support configuration and default values for transaction and
reactive modes are contained in csp_config.h

```
#include "csp_config.h"
```

The configuaration paramters include, note that the parameters
must be defined to either 1 or 0.

```
TRANSACTION_DEFAULT
SUPPORT_TRANSACTION

REACTIVE_DEFAULT
SUPPORT_REACTIVE

USE_STATISTICS
USE_FIXPOINT

```

The programmers api to change during set in for example arudion sketch
during setup

```
extern int     csp_set_transaction(csp_rt_t*, int onoff);
extern int     csp_set_reactive(csp_rt_t*, int onoff);
extern int     csp_set_latch(csp_rt_t*, int onoff);

```

## Interactive Mode

Start interactive mode with `-i`:

```bash
./csp -i
./csp -i program.csp    # load program first
```

### Interactive Commands

| Command | Description |
|---------|-------------|
| `/help` | Show commands |
| `/list` | List all declarations |
| `/state` | Show current values |
| `/reset` | Reset to initial values |
| `/latch on` | Hold outputs (freeze current values) |
| `/latch off` | Release outputs (normal operation) |
| `/commit` | Commit pending values |
| `/save` | Save state to EEPROM file |
| `/load` | Load state from EEPROM file |
| `/quit` | Exit |

### Direct Evaluation

In interactive mode you can:

```
> X + 1              # evaluate expression
> X = 5              # assign value directly
#variable Y = 10     # add new declaration
Y = X * 2 ? 1        # add new rule
```

### Output Latch

The latch controls whether outputs are written to hardware. Like an electronic latch:

- `/latch on` - Hold (latch) current output values. Outputs are calculated internally but not written to pins.
- `/latch off` - Release the latch. Outputs flow through to hardware normally.

**Interactive mode starts with latch ON** (outputs frozen). This is a safety feature - you can experiment without affecting hardware. Use `/latch off` when ready to activate outputs.

For programmatic control in running code, use the `latch()` function:

```
latch(1) ? Fault       // hold outputs on fault condition
latch(0) ? !Fault      // release when fault clears
```

# Arduino Examples

## Blink - Blinking LED

The classic Arduino example in CandySpeak:

```
#digital Led out 13
#timer Blink 500 = 1

Led = ~Led, Blink = 1 ? timeout(Blink)
```

LED toggles every 500 milliseconds. `Blink = 1` restarts the timer.

## Button with LED

Light LED when button is pressed:

```
#digital Button in pullup 2
#digital Led out 13

Led = !Button ? 1
```

Button is active-low (pullup), so `!Button` is true when pressed.

## Debouncer

Filter out contact bounce from a button:

```
#module Debouncer
#timer T 50
#variable Raw in integer
#variable Out:1 out integer = 0
#variable Prev:1 integer = 0

T = 1 ? Raw != Prev
Prev = Raw, Out = Raw ? timeout(T)
#end

#digital Button in pullup 2
#digital Led out 13
#Debouncer db Raw <- !Button

Led = db.Out ? 1
```

The module waits 50ms after a change before accepting the new value.

## Toggle - Toggle with Button

Press to toggle LED:

```
#module Debouncer
#timer T 50
#variable Raw in integer
#variable Out:1 out integer = 0
#variable Prev:1 integer = 0

T = 1 ? Raw != Prev
Prev = Raw, Out = Raw ? timeout(T)
#end

#digital Button in pullup 2
#digital Led out 13
#variable LedState:1 = 0
#Debouncer db Raw <- !Button

LedState = ~LedState ? rising(db.Out)
Led = LedState ? 1
```

## Dimmer - PWM Control

Adjust brightness with two buttons:

```
#digital BtnUp in pullup 2
#digital BtnDown in pullup 3
#analog Dimmer:8 out pwm 9
#variable Brightness:8 = 128
#timer Rep 100

Brightness = Brightness + 10 ? !BtnUp && Brightness < 245
Brightness = Brightness - 10 ? !BtnDown && Brightness > 10
Rep = 1 ? !BtnUp || !BtnDown
Dimmer = Brightness ? 1
```

## Temperature Control

Simple thermostat with hysteresis:

```
#analog Sensor in A0
#digital Heater out 7
#variable Setpoint = 200
#variable Hysteresis = 10

Heater = 1 ? Sensor < Setpoint - Hysteresis
Heater = 0 ? Sensor > Setpoint + Hysteresis
```

The value 200 corresponds to approximately 20C with a typical NTC sensor.

## Running Lights - Knight Rider

LEDs that move back and forth:

```
#digital Led0 out 2
#digital Led1 out 3
#digital Led2 out 4
#digital Led3 out 5
#digital Led4 out 6
#digital Led5 out 7
#digital Led6 out 8
#digital Led7 out 9

#timer Step 100 = 1
#variable Pos:4 = 0
#variable Dir:1 = 0

Pos = Pos + 1, Dir = 1 ? timeout(Step) && !Dir && Pos < 7
Pos = Pos - 1, Dir = 0 ? timeout(Step) && Dir && Pos > 0
Dir = 1 ? Pos == 7
Dir = 0 ? Pos == 0
Step = 1 ? timeout(Step)

Led0 = (Pos == 0) ? 1
Led1 = (Pos == 1) ? 1
Led2 = (Pos == 2) ? 1
Led3 = (Pos == 3) ? 1
Led4 = (Pos == 4) ? 1
Led5 = (Pos == 5) ? 1
Led6 = (Pos == 6) ? 1
Led7 = (Pos == 7) ? 1
```

## Breathing LED

Smooth pulsing with PWM:

```
#analog Led:8 out pwm 9
#timer Step 20 = 1
#variable Brightness:8 = 0
#variable Dir:1 = 0

Brightness = Brightness + 5 ? timeout(Step) && !Dir
Brightness = Brightness - 5 ? timeout(Step) && Dir
Dir = 1 ? Brightness >= 250
Dir = 0 ? Brightness <= 5
Step = 1 ? timeout(Step)
Led = Brightness ? 1
```

## Traffic Light

Sequential traffic light:

```
#digital Red out 2
#digital Yellow out 3
#digital Green out 4

#timer Phase 1000 = 1
#variable State:2 = 0

State = 1 ? timeout(Phase) && State == 0
State = 2 ? timeout(Phase) && State == 1
State = 3 ? timeout(Phase) && State == 2
State = 0 ? timeout(Phase) && State == 3
Phase = 1 ? timeout(Phase)

Red = (State == 0 || State == 1) ? 1
Yellow = (State == 1 || State == 3) ? 1
Green = (State == 2) ? 1
```

Sequence: Red -> Red+Yellow -> Green -> Yellow -> Red...

# Compile to Embedded Code

For production use, CandySpeak can be compiled to C code that is included directly in firmware.

## Step 1: Generate Code

```bash
./csp -C -n program.csp > program_rom.h
```

## Step 2: Arduino Project

Create an Arduino project with the following structure:

```
project/
  project.ino
  csp.h
  csp_rt.c
  csp_arduino.c
  program_rom.h
```

## Step 3: Main Program

```cpp
#include "csp.h"
#include "program_rom.h"

void setup() {
    csp_setup();
}

void loop() {
    csp_loop();
}
```

## Benefits of Compiled Code

- Faster execution
- Less memory usage
- No parser needed
- Constant ROM size

# PDF Generation

This manual can be converted to PDF with pandoc:

```bash
# Install pandoc and LaTeX
sudo apt install pandoc texlive-xetex fonts-dejavu

# Generate PDF
pandoc doc/manual_en.md -o doc/manual_en.pdf \
  --pdf-engine=xelatex \
  --toc \
  --toc-depth=2 \
  -V colorlinks=true \
  -V linkcolor=blue
```

# Appendix: Quick Reference

## Declarations
```
#variable <name>[:<bits>] [type] [= value]
#variable <name>:<bits> [big] bind <buffer>[<a>..<b>]   // bit-field view
#digital <name> [in|out|inout] [pullup|pulldown] [<port>:]<pin>
#analog <name>[:<resolution>] [in|out] [pwm] [<port>:]<pin>
#timer <name> <period_ms> [= 1]
#constant <name> = <value>
#buffer <name>:<bits> [type]            // shared storage / frame layout
#module <name> ... #end
```

## Buffers
```
#buffer Buf:16              // 2 bytes of shared storage
Buf[0] = 52                 // byte access (index = byte)
X = Buf[0..1]               // byte range -> one value (little-endian)

#variable F:8     bind Buf[0..7]    // bind range = bits
#variable G:10 big bind Buf[8..17]  // big-endian bit-field
```

## Module Instantiation
```
#<Module> <instance> [<init>]*

Init forms (can be mixed):
  field = value      // static init
  field <- expr      // reactive connection
  Pin = number       // pin assignment for #digital/#analog
```

## Rules
```
<actions> ? <condition>
action1, action2 ? condition
variable = expression ? condition   // regular assignment
variable <- expression              // reactive (runs on change)
variable <- expression ? condition  // reactive with condition
Timer = 1 ? condition               // start timer
```

## Timer Functions
```
timeout(T)    // true one cycle at timeout
elapsed(T)    // ms since start
progress(T)   // 0-100% of period
T = 1         // start/restart timer
```

## Edge Detection
```
changed(x)    // value changed
rising(x)     // 0 -> 1
falling(x)    // 1 -> 0
```
