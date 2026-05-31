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

# Introduktion

CandySpeak är ett reaktivt programmeringsspråk designat för inbyggda system och mikrokontrollers. Det kombinerar enkelhet med kraftfulla abstraktioner för att hantera sensorer, timers och I/O.

Språket är regelbaserat - varje rad beskriver VAD som ska hända och NÄR, inte HUR. Systemet evaluerar alla regler varje cykel och uppdaterar utgångar baserat på ingångar.

## Grundläggande koncept

- **Variabler** - lagrar värden mellan cykler
- **Digitala I/O** - knappar, LEDs, reläer
- **Analoga I/O** - sensorer, PWM
- **Timers** - tidbaserade händelser
- **Moduler** - återanvändbara komponenter

# Språkbeskrivning

## Deklarationer

Deklarationer börjar med `#` och definierar programmets resurser.

### Variabler

```
#variable <namn>[:<bitar>] [<typ>] [= <värde>]
```

Exempel:
```
#variable Counter:8 integer = 0
#variable Temperature float = 20.0
#variable Flag = 0
```

### Digitala I/O

```
#digital <namn> [in|out|inout] [pullup|pulldown] [<port>:]<pin>
```

Port är valfritt (för mikrokontrollers med flera I/O-portar).

Exempel:
```
#digital Button in pullup 2
#digital Led out 13
#digital Relay out B:7
#digital Status inout C:4
```

### Analoga I/O

```
#analog <namn>[:<upplösning>] [in|out] [pwm] [<port>:]<pin>
```

Upplösning är antal bitar (default 10).

Exempel:
```
#analog Sensor in A0
#analog Dimmer:8 out pwm 9
#analog HighRes:12 in A:1
```

### Timers

```
#timer <namn> <period_ms> [= 1]
```

`= 1` startar timern automatiskt vid programstart.

Exempel:
```
#timer Blink 500 = 1
#timer Debounce 50
#timer Timeout 5000
```

### Moduler

Moduler grupperar relaterad funktionalitet för återanvändning. En modul är en mall som kan instansieras flera gånger med olika konfigurationer.

```
#module <namn>
  <deklarationer>
  <regler>
#end
```

#### Modulinstansiering

```
#<ModulNamn> <instans> [<init>]*
```

Där `<init>` kan vara:

| Form | Betydelse |
|------|-----------|
| `fält = värde` | Sätt initialvärde (statiskt) |
| `fält <- uttryck` | Reaktiv koppling (uppdateras när uttryck ändras) |

Init-uttryck kan blandas fritt. Fält markerade `in` måste initieras.

#### Exempel: Full Adder

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

Här har `a0` statiska ingångar, medan `a1` och `a2` kopplar sin carry-ingång reaktivt från föregående adders carry-utgång.

#### Pin-tilldelning för I/O

När en modul innehåller `#digital` eller `#analog` deklarationer kan pin-numret utelämnas i modulen och tilldelas vid instansiering:

```
#module Button
#digital Pin in pullup       // inget pin-nummer
#variable Out:1 out = 0
#timer T 50

T = 1 ? Pin != Out
Out = Pin ? timeout(T)
#end

#Button btn1 Pin=2           // tilldela pin vid instansiering
#Button btn2 Pin=3
#Button btn3 Pin=4
```

Detta gör moduler återanvändbara över olika hårdvarukonfigurationer.

Pin-tilldelning kan även överrida default:

```
#module Led
#digital Out out 13          // default pin 13
...
#end

#Led led1                    // använder default pin 13
#Led led2 Out=12             // override till pin 12
```

#### Åtkomst till modulfält

Använd punkt-notation för att komma åt en modulinstans fält:

```
MainLed = btn1.Out ? 1       // läs avfångad output
```

## Regler

Regler har formen:

```
<åtgärder> ? <villkor>
```

Åtgärderna utförs när villkoret är sant.

### Tilldelning

Vanlig tilldelning - utvärderas varje cykel:
```
Led = 1 ? Button == 0
Counter = Counter + 1 ? Timer
```

### Reaktiv tilldelning

Med `<-` körs regeln endast när någon variabel i högerledet ändras:

```
Output <- Input * 2
```

Regeln ovan körs bara när `Input` ändras, inte varje cykel.

Med villkor - regeln körs när RHS-variabel ändras OCH villkoret är sant:

```
Filtered <- RawValue ? RawValue > Threshold
```

### Flera åtgärder

Separera med komma:

```
Led = 1, Counter = Counter + 1 ? Button == 0
```

### Villkor

Villkor kan vara:

- Jämförelser: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logiska: `&&` (och), `||` (eller), `!` (icke)
- Timer-funktioner: `timeout(T)`, `elapsed(T)`
- Specialfunktioner: `changed(x)`, `rising(x)`, `falling(x)`

### Exempel

```
Led = ~Led ? timeout(Blink)
```

Växla LED vid varje timeout.

```
Output = 1, T = 1 ? Input && !Output
```

Sätt Output och starta timer T när Input blir hög.

## Inbyggda funktioner

| Funktion | Beskrivning |
|----------|-------------|
| `timeout(T)` | Sant en cykel när timer T går ut |
| `elapsed(T)` | Millisekunder sedan T startade |
| `progress(T)` | 0-100, hur långt timern kommit (%) |
| `changed(x)` | Sant om x ändrades denna cykel |
| `rising(x)` | Sant vid 0→1 övergång |
| `falling(x)` | Sant vid 1→0 övergång |
| `cycle()` | Nuvarande cykelnummer |
| `abs(x)` | Absolutvärde |
| `min(a,b)` | Minsta värdet |
| `max(a,b)` | Största värdet |
| `print(x)` | Skriv ut värde |
| `println(x)` | Skriv ut med radbrytning |
| `latch(b)` | Håll utgångar (1) eller släpp igenom (0) |

## Operatorer

| Prioritet | Operator | Beskrivning |
|-----------|----------|-------------|
| Högst | `~` `!` `-` | Bitvis NOT, logisk NOT, negation |
| | `*` `/` `%` | Multiplikation, division, modulo |
| | `+` `-` | Addition, subtraktion |
| | `<<` `>>` | Bitskift |
| | `<` `<=` `>` `>=` | Jämförelse |
| | `==` `!=` | Likhet |
| | `&` | Bitvis AND |
| | `^` | Bitvis XOR |
| | `|` | Bitvis OR |
| | `&&` | Logisk AND |
| Lägst | `||` | Logisk OR |

# Linux-verktyget

## Installation

```bash
git clone <repo>
cd candyspeak
make
```

## Användning

```bash
./csp [flaggor] [fil.csp]
```

### Flaggor

| Flagga | Beskrivning |
|--------|-------------|
| `-h` | Visa hjälp |
| `-i` | Interaktivt läge |
| `-n` | Endast parsning, kör ej |
| `-C` | Visa kompilerad C-kod |
| `-c N` | Max antal cykler |
| `-T MS` | Max körtid i millisekunder |
| `-t[=0\|1]` | Transaktionsläge (default på) |
| `-r[=0\|1]` | Reaktivt läge (default av) |
| `-Q` | Spåra variabelvärden |
| `-P` | Debug parser |
| `-L erlang` | Output i Erlang-format |

### Exempel

Kör ett program:
```bash
./csp program.csp
```

Visa kompilerad kod:
```bash
./csp -C -n program.csp
```

Kör max 100 cykler med spårning:
```bash
./csp -c 100 -Q program.csp
```

Interaktivt läge:
```bash
./csp -i
```

## Generera inbäddad kod

För att generera C-kod för Arduino eller annan mikrokontroller:

```bash
./csp -C -n program.csp > program_code.h
```

Inkludera sedan denna header i ditt Arduino-projekt tillsammans med `csp.h` och runtime-filerna.

# Avancerade koncept

## Exekveringslägen

CandySpeak stödjer olika exekveringslägen som byter minne mot prestanda. Standardläget är säkert och förutsägbart, men för resursbegränsade system eller stora program kan andra lägen vara effektivare.

### Transaktionsläge (`-t`)

**Default: PÅ** (`-t1`)

I transaktionsläge samlas alla ändringar inom en cykel och tillämpas atomärt vid cykelns slut. Detta säkerställer:

- Regler ser konsistent tillstånd (inga partiella uppdateringar)
- Ordningen på regeleval spelar ingen roll
- Förutsägbart beteende

Stäng av med `-t0` för lägre minnesanvändning, men då ser regler mellanvärden när de skrivs.

```bash
./csp -t0 program.csp    # icke-transaktionellt läge
./csp -t1 program.csp    # transaktionellt (default)
```

### Reaktivt läge (`-r`)

**Default: AV** (`-r0`)

I reaktivt läge evalueras bara regler vars ingångar har ändrats. Detta är effektivare när:

- Få variabler ändras varje cykel
- Programmet har många regler
- Systemet har begränsad CPU

Resultatet ska vara identiskt med att evaluera alla regler, men använder mer minne för beroendehantering.

```bash
./csp -r1 program.csp    # reaktivt läge
./csp -r0 program.csp    # evaluera alla regler (default)
```

### Kombinera lägen

Lägena kan kombineras:

| Kommando | Transaktion | Reaktiv | Användning |
|----------|-------------|---------|------------|
| `./csp` | PÅ | AV | Default, säkert |
| `./csp -r1` | PÅ | PÅ | Många regler, få ändringar |
| `./csp -t0` | AV | AV | Minimalt minne |
| `./csp -t0 -r1` | AV | PÅ | Lite minne + effektivt |

## Interaktivt läge

Starta interaktivt läge med `-i`:

```bash
./csp -i
./csp -i program.csp    # ladda program först
```

### Interaktiva kommandon

| Kommando | Beskrivning |
|----------|-------------|
| `/help` | Visa kommandon |
| `/list` | Lista alla deklarationer |
| `/state` | Visa nuvarande värden |
| `/reset` | Återställ till initialvärden |
| `/latch on` | Håll utgångar (frys nuvarande värden) |
| `/latch off` | Släpp igenom utgångar (normal drift) |
| `/commit` | Verkställ väntande värden |
| `/save` | Spara tillstånd till EEPROM-fil |
| `/load` | Ladda tillstånd från EEPROM-fil |
| `/quit` | Avsluta |

### Direkt evaluering

I interaktivt läge kan du:

```
> X + 1              # evaluera uttryck
> X = 5              # tilldela värde direkt
#variable Y = 10     # lägg till ny deklaration
Y = X * 2 ? 1        # lägg till ny regel
```

### Utgångslatch

Latchen styr om utgångar skrivs till hårdvara. Som en elektronisk latch:

- `/latch on` - Håll (latcha) nuvarande utgångsvärden. Utgångar beräknas internt men skrivs inte till pinnar.
- `/latch off` - Släpp latchen. Utgångar flödar igenom till hårdvara normalt.

**Interaktivt läge startar med latch PÅ** (utgångar frysta). Detta är en säkerhetsfunktion - du kan experimentera utan att påverka hårdvaran. Använd `/latch off` när du är redo att aktivera utgångar.

För programmatisk styrning i körande kod, använd `latch()`-funktionen:

```
latch(1) ? Fault       // håll utgångar vid feltillstånd
latch(0) ? !Fault      // släpp när felet försvinner
```

# Arduino-exempel

## Blink - Blinkande LED

Det klassiska Arduino-exemplet i CandySpeak:

```
#digital Led out 13
#timer Blink 500 = 1

Led = ~Led, Blink = 1 ? timeout(Blink)
```

LED växlar var 500:e millisekund. `Blink = 1` startar om timern.

## Knapp med LED

Tänd LED när knappen trycks:

```
#digital Button in pullup 2
#digital Led out 13

Led = !Button ? 1
```

Knappen är aktiv-låg (pullup), så `!Button` är sant när nedtryckt.

## Debouncer - Avfångare

Filtrera bort kontaktstuds från en knapp:

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

Modulen väntar 50ms efter en ändring innan den accepterar det nya värdet.

## Toggle - Växla med knapp

Tryck för att växla LED:

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

## Dimmer - PWM-styrning

Justera ljusstyrka med två knappar:

```
#digital BtnUp in pullup 2
#digital BtnDown in pullup 3
#analog Led:8 out pwm 9
#variable Brightness:8 = 128
#timer Rep 100

Brightness = Brightness + 10 ? !BtnUp && Brightness < 245
Brightness = Brightness - 10 ? !BtnDown && Brightness > 10
Rep = 1 ? !BtnUp || !BtnDown
Led = Brightness ? 1
```

## Temperaturstyrning

Enkel termostat med hysteresis:

```
#analog Sensor in A0
#digital Heater out 7
#variable Setpoint = 200
#variable Hysteresis = 10

Heater = 1 ? Sensor < Setpoint - Hysteresis
Heater = 0 ? Sensor > Setpoint + Hysteresis
```

Värdet 200 motsvarar ca 20°C med en typisk NTC-sensor.

## Rinnande ljus - Knight Rider

LEDs som rör sig fram och tillbaka:

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

## Pulserande LED - Andning

Mjuk pulsering med PWM:

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

## Trafikljus

Sekventiellt trafikljus:

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

Sekvensen: Röd → Röd+Gul → Grön → Gul → Röd...

# Kompilera till fast kod

För produktionsanvändning kan CandySpeak kompileras till C-kod som inkluderas direkt i firmware.

## Steg 1: Generera kod

```bash
./csp -C -n program.csp > program_rom.h
```

## Steg 2: Arduino-projekt

Skapa ett Arduino-projekt med följande struktur:

```
projekt/
  projekt.ino
  csp.h
  csp_rt.c
  csp_arduino.c
  program_rom.h
```

## Steg 3: Huvudprogram

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

## Fördelar med kompilerad kod

- Snabbare exekvering
- Mindre minnesanvändning
- Ingen parser behövs
- Konstant ROM-storlek

# PDF-generering

Denna manual kan konverteras till PDF med pandoc:

```bash
# Installera pandoc och LaTeX
sudo apt install pandoc texlive-latex-recommended texlive-fonts-recommended

# Generera PDF
pandoc doc/manual.md -o doc/manual.pdf \
  --pdf-engine=pdflatex \
  --toc \
  --toc-depth=2 \
  -V colorlinks=true \
  -V linkcolor=blue

# Alternativt med XeLaTeX för bättre typsnittsstöd
pandoc doc/manual.md -o doc/manual.pdf \
  --pdf-engine=xelatex \
  --toc
```

# Appendix: Snabbreferens

## Deklarationer
```
#variable <namn>[:<bitar>] [typ] [= värde]
#digital <namn> [in|out|inout] [pullup|pulldown] [<port>:]<pin>
#analog <namn>[:<upplösning>] [in|out] [pwm] [<port>:]<pin>
#timer <namn> <period_ms> [= 1]
#constant <namn> = <värde>
#module <namn> ... #end
```

## Modulinstansiering
```
#<Modul> <instans> [<init>]*

Init-former (kan blandas):
  fält = värde       // statisk init
  fält <- uttryck    // reaktiv koppling
  Pin = nummer       // pin-tilldelning för #digital/#analog
```

## Regler
```
<åtgärder> ? <villkor>
åtgärd1, åtgärd2 ? villkor
variabel = uttryck ? villkor     // vanlig tilldelning
variabel <- uttryck              // reaktiv (kör vid ändring)
variabel <- uttryck ? villkor    // reaktiv med villkor
Timer = 1 ? villkor              // starta timer
```

## Timer-funktioner
```
timeout(T)    // sant en cykel vid timeout
elapsed(T)    // ms sedan start
progress(T)   // 0-100% av perioden
T = 1         // starta/starta om timer
```

## Kantdetektering
```
changed(x)    // värdet ändrades
rising(x)     // 0 → 1
falling(x)    // 1 → 0
```
