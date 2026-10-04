# Panel widgets

The panel (`tools/panel`, see its README for how to run it) builds a widget for
every `#digital`, `#analog` and `#field` in a program. The declaration decides
the widget. `#annotate panel` can change how it looks, and it can switch the
widget to another kind, but only to one the declaration allows.

    #analog IndoorTemp:10 in 0
    #annotate panel IndoorTemp kind=dial min=-200 max=400 scale=0.1 unit="°C"

`#annotate` is inert: the compiler skips it and nothing of it reaches a ROM.
candyspeak itself only checks that the target exists. The panel checks the keys
and **warns on stdout** about anything it does not use, whether that is an
unknown key, a key that means nothing on that widget, or a value it does not
understand. Nothing is dropped silently.

## Syntax

    #annotate panel <signal> key=value key=value key ...

| value form       | example                 | arrives as                 |
|------------------|-------------------------|----------------------------|
| bare word        | `kind=dial`             | the word                   |
| integer          | `min=-200`, `max=0x3FF` | integer, hex allowed       |
| float            | `scale=0.000976`        | float                      |
| quoted string    | `unit="°C"`, `color="#f80"` | the text inside        |
| key alone        | `hidden`                | true                       |

Several lines for the same signal are merged; a later key wins.

`*` in place of a signal means **the panel itself**:

    #annotate panel * trace=off          // no logic trace at all
    #annotate panel * glow=off beam=1    // every plot drawn thin and sharp
    #annotate panel * hidden             // hide everything...
    #annotate panel Speed hidden=0       // ...except what you bring back

A key on `*` is a default for every widget it applies to, and a widget's own key
wins over it. Defaults are not checked per widget, so `* min=0` does not warn
on every lamp. `kind`, `label` and `axis` describe one signal, so they are not
inherited from `*`, and the panel warns if you put them there. Anything that
is not a word or a number has to be quoted. That includes `#rrggbb` colours,
units with spaces or symbols, and labels with spaces.

## Which widget a declaration gets

| declaration                       | default widget | `kind=` may also be       |
|-----------------------------------|----------------|---------------------------|
| `#digital ... in`                 | `toggle`       | `push`                    |
| `#digital ... out` / `inout`      | `lamp`         | `action`                  |
| `#analog ... in`                  | `slider`       | `dial`, `value`, `plot`   |
| `#analog ... out`                 | `meter`        | `value`, `plot`           |
| `#analog` on **port 9**           | `pixel`        | (none)                    |
| `#field` in a `#buffer ... in` with no transport | `slider` | as analog in      |
| `#field` in any other buffer      | `meter`        | as analog out             |

The direction is kept on purpose. An input can only become another input
widget, because a panel that draws a dial for a digital pin cannot drive the
pin. If you ask for an impossible kind you get a warning, and the derived
widget is used instead.

A field in a buffer that has a transport (`can`, `tcp`, `udp` ...) is
read-only. The wire fills it, and the panel must not compete with the wire.

Arrays (`#analog P[10]:16 ...`) give one widget per element: `P[0]`..`P[9]`.

Variables, constants and rules do not get widgets yet.

## The widgets

| kind      | shows                                | you can                         | sends                 |
|-----------|--------------------------------------|---------------------------------|-----------------------|
| `toggle`  | a button reading 0/1, lit when 1     | click: flip against csp's value | `X = 0` / `X = 1`     |
| `push`    | same button                          | hold: 1 while held, 0 on release| `X = 1`, `X = 0`      |
| `lamp`    | a round lamp                         | (none)                          | (none)                |
| `action`  | an `off` / `ON` tag that lights up   | (none)                          | (none)                |
| `slider`  | a range slider and a readout         | drag                            | `X = <count>`         |
| `dial`    | a needle (-135°..+135°), slider, readout | drag the slider             | `X = <count>`         |
| `value` (in) | an editable text field            | type a number, Enter            | `X = <what you typed>`|
| `value` (out)| the number                        | (none)                          | (none)                |
| `meter`   | a horizontal bar and a readout       | (none)                          | (none)                |
| `pixel`   | a colour swatch, decoded as RGB565   | hover for the name              | (none)                |
| `plot`    | one channel of an XY picture         | drag on the canvas (inputs only)| `X = <count>` per axis|

`action` is for outputs that *do* something, like a siren, a pump or a valve. A
small dot does not tell you "the pump is running"; a word does.

Pixels share one row of swatches labelled `port 9`, and the channels of a plot
are drawn as one canvas.

### The logic trace

Below the widgets is a logic trace. Each signal gets a row, with its label on
the left in the signal's colour. Each tick adds one column, and the trace
starts over from the left when it reaches the right edge:

* a digital signal is drawn as two levels in its colour,
* a numeric signal is drawn as a height between its `min` and `max`,
* a pixel is drawn as its colour.

`trace=off` on a signal removes its row. `#annotate panel * trace=off` removes
the whole trace. Use that when the panel is just the GUI for a program.

The panel never sets an input widget from csp's value. The control stays where
you left it, and only the readout follows the program. Otherwise the cycle that
writes the value back would fight you while you drag or type.

## Keys

| key      | toggle push | lamp action | slider dial value meter | plot | pixel |
|----------|:-----------:|:-----------:|:-----------------------:|:----:|:-----:|
| `kind`   | ✓           | ✓           | ✓                       | ✓    | ✓     |
| `label`  | ✓           | ✓           | ✓                       | ✓    | ✓     |
| `hidden` | ✓           | ✓           | ✓                       | ✓    | ✓     |
| `trace`  | ✓           | ✓           | ✓                       | ✓    | ✓     |
| `color`  | ✓           | ✓           | ✓                       | ✓    |       |
| `min` `max` |          |             | ✓                       | ✓    |       |
| `scale` `unit` |       |             | ✓                       | ✓    |       |
| `axis` `id` `persist` `shape` `clip` | | |                     | ✓    |       |
| `beam` `dot` `glow` `line` |  |       |                         | ✓    |       |

If you use a key on a widget whose column has no ✓, the panel says so:

    panel: Led: min= unit= means nothing on a lamp, ignored

### `label`

This is the text in the row. It is also the name next to a plot channel and the
hover text on a pixel. By default the label is the signal's name, minus any
`__Colour` suffix. The label is display-only: csp, rules and the dump keep
using the real name.

    #annotate panel Lt3 label="Kitchen"

UTF-8 is decoded, but only up to Latin-1. Characters such as `ö`, `°` and `µ`
work, but `Ω` comes out as its raw bytes, because wse would send it as an array
of numbers otherwise.

### `hidden`

`hidden` on its own, or `hidden=1`, removes the widget completely: it gets no
row and no trace, and it is dropped from any plot it belonged to. If the hidden
channel was a plot's x, the plot sweeps instead. `hidden=0` brings the widget
back without deleting the line. csp is not affected and still has the signal.

### `color`

`color` sets the colour of whatever lights up:

| widget          | what gets the colour                                    |
|-----------------|---------------------------------------------------------|
| toggle, push    | the button's background when it is 1                    |
| lamp            | the lamp when lit, plus a dimmed version when unlit     |
| action          | the tag when ON, plus the dimmed version when off       |
| slider          | the slider (`accent-color`)                             |
| dial            | the needle and the slider                               |
| meter           | the bar                                                 |
| value           | the number                                              |
| plot            | the beam                                                |
| every widget    | its line in the trace                                   |

The value can take three forms:

* a name: `red orange amber yellow green cyan blue purple pink white`
* an HTML colour, quoted: `color="#f80"`, `color="#ff8800"`
* a hex number: `color=0xff8800`

For the named colours, the unlit shade is picked by hand. For hex colours it is
the same hue at about a seventh of the brightness. If the panel does not
recognise the colour, it warns and uses the default.

Defaults when no `color` is given:

* **digital** widgets take the colour from the name. If the name contains
  `green`/`gron`/`grn`, `yellow`/`amber`/`gul`, `blue`/`bla` or
  `white`/`vit`, that colour is used; anything else is red. A `__Colour`
  suffix (`Heater__Orange`) names the colour outright and is removed from the
  label.
* **plots** use phosphor green (`#5f9`). A beam does not guess from the name,
  so `yin` is not yellow.
* **value** widgets use white.
* everything else uses the panel's blue (`#3af`).

Only the American spelling `color` is accepted; `colour=` is warned about as an
unknown key.

### `min`, `max`

These set the range a numeric widget covers, in **counts** (the values csp
holds):

* the travel of a slider or dial,
* where a meter bar is empty and where it is full,
* the needle's sweep,
* the height of the trace line,
* the edges of a plot, and the values you get by dragging on the plot canvas.

The default is the declared swing. An `unsigned` analog or field covers
`0..2^W-1`. A signed one covers `-2^(W-1)..2^(W-1)-1`, so without `min`/`max`
a signed channel at zero sits in the middle and does not hit the floor. A
one-bit field is always 0..1.

Narrow the range when a signal only uses part of it:

    #annotate panel Pot min=300 max=700

Values outside the range are clamped **on screen only**. The needle stops at
the end, and the readout still shows the real number. A slider cannot send
anything outside the range, but csp itself can still hold such a value.

### `scale`, `unit`

These say what a count means in the real world. The readout shows the count and
then the converted value:

    #annotate panel Batt scale=0.000976 unit="V"       // 600 -> "600  0.586 V"
    #annotate panel Rpm  unit="rpm"                    // 600 -> "600 rpm"

* with `scale` and `unit`: `count  value unit`, with three decimals,
* with `scale` only: `count  value`,
* with `unit` only: `count unit` (the count already is the unit),
* with neither: just the count.

Both keys only change what is **displayed**. csp always receives counts. A
slider with `scale=0.1` still sends the count, and a `value` input takes the
count you type, not the converted value.

### Plot keys

Channels with `kind=plot` and the same `id` are drawn as one picture.

| key       | values            | default        | on        |
|-----------|-------------------|----------------|-----------|
| `id`      | any word          | `plot`         | channel   |
| `axis`    | `x`, `y`          | guessed        | channel   |
| `shape`   | `square`, `round` | `square`       | picture   |
| `clip`    | `on`, `off`       | follows shape  | picture   |
| `persist` | 0.0 .. 0.999      | 0.90           | picture   |
| `beam`    | px, 0 = no line   | 1.6            | picture   |
| `dot`     | px, 0 = no dot    | 1.5 × beam     | picture   |
| `glow`    | `on`, `off`       | `on`           | picture   |
| `line`    | `straight`, `step`| `straight`     | picture   |

* `axis`: a plot has one x and any number of y channels. If you do not set it,
  the first channel becomes x and the panel prints that guess. If there is no x
  channel at all, the beam sweeps across the screen like an ordinary scope.
* Picture keys may be set on **any** channel of the group. The first value
  found is used, so you only need to write it once.
* `shape=round` draws a round CRT face with rings and a crosshair. It clips by
  default, so the corners of the value range fall off the glass. `clip=off`
  keeps those corners visible.
* `persist` is how much of the trail is left after 100 ms. The fade is scaled
  by the tick, so changing the tick does not change the trail length.
  `persist=0` gives no trail, only the beam. Every pixel decays toward the
  background, and the distance is rounded down, so a trail always fades out
  completely and leaves no ghost.
* `beam` is the line width and `dot` is the radius of the bright head. `glow`
  makes the head bloom and rounds the line ends. For crisp square curves, use
  `beam=1 dot=0 glow=off line=step`. An odd width is drawn on the half pixel,
  so a 1 px line is exactly one pixel wide.
* `line=step` holds each sample until the next one: the line goes across at the
  old value, then straight up or down to the new one. That is how a square wave
  should look. `straight` draws a diagonal from sample to sample, which makes a
  square wave look like a trapezoid.
* You can drive a plot of **inputs** by holding the mouse on the canvas: x
  follows the pointer, and so does the first y input. One `/step` while paused
  commits both values in the same cycle.
* A plot channel also gets its normal row in the trace, so you can see both
  where the beam is and how it got there.

Example (`tools/panel/demo/xyo.csp`):

    #annotate panel xout  kind=plot axis=x id=plot1 shape=round clip=on scale=0.000976 unit="ms"
    #annotate panel yout1 kind=plot axis=y id=plot1 color=amber scale=0.000976 unit="V"
    #annotate panel yout2 kind=plot axis=y id=plot1 color=red   scale=0.000976 unit="V"

The same plot drawn as sharp logic levels instead of a glowing trace:

    #annotate panel xout kind=plot axis=x id=plot1 beam=1 dot=0 glow=off line=step persist=0.5

## The bar

At the top are the program chooser (every `.csp` file in `tools/panel/demo/`
and in `examples/`), **run** / **pause** / **step**, and the **tick** (10, 25,
50, 100 or 250 ms; the starting value comes from `CSP_PANEL_PERIOD`). While
paused, values from widgets are held, and one **step** commits all of them in
the same cycle.

## Notes and known gaps

* **The look is one map per widget.** Every key in the tables above is read
  the same way for every kind (`csp_panel:look/3`). Before this change, `label`
  and `hidden` passed the key check and then did nothing, and `color`, `unit`,
  `scale`, `min` and `max` only had an effect on plots.
  `color=amber` in `xyt.csp` silently fell back to phosphor, because the
  palette did not contain amber. `min=-512` was a syntax error, because the
  annotation grammar had no negative numbers. All of these are fixed now and
  covered by tests (`make test` in `tools/panel`).
* **`__Colour` in names** is now redundant with `color=`. It puts presentation
  into the identifier, which then has to be spelled out in every rule. It still
  works, but `color=` is the better way.
* **Display only.** `scale` cannot be used to enter values in real-world units.
  Doing that would need the panel to convert back to counts and round, and then
  the panel would send something you cannot see typed at the prompt.
* **A plot has no label of its own.** The text next to the canvas is the `id`.
* **Fixed geometry.** A plot is always 240 px square, a slider is 200 px, and
  the trace is 720 px plus 110 px for labels. There are no size keys. A label
  longer than 13 characters is cut short in the trace.
* **Beam settings are per plot, not per channel.** All channels in one picture
  share `beam`, `dot`, `glow` and `line`.
* **Port 9 is the CPX pixel strip.** This is an assumption about the board, not
  something the language says.
* **`hidden` is per tool.** `#annotate varp X hidden` belongs to the model
  checker and does not hide anything in the panel.
