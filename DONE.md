# DONE

Avklarade punkter, flyttade hit från TODO.md. Nyast överst.

## Villkor droppas TYST vid parse-fel i guarden.
  `Q = 1 ? undefinedname` och `Q = 2 ? A &&` svarar båda "OK" och lagras som
  OVILLKORLIGA regler (`Q=1`, `Q=2` i /list). En stavfel i en guard gör alltså
  en villkorad regel alltid-på -- tyst. Troligen samma rot som [ROM]/[RAM]-
  listningsbuggen överst: guard-delen tappas någonstans mellan parse och
  emission istället för att sätta ERR_SYNTAX.

## Fälttypen läses aldrig -- `integer` ger inget tecken (2026-07-22) -- KLART 2026-07-27
  Löst: csp_heap_get teckenutvidgar när vw->vt == V_INTEGER (bit-vägen). endian
  fungerade redan (setup_field kopierade ca.endian -> vw->endian, csp_heap_get
  greppar E_BIG). Syntaxen fanns redan: #field tar integer/unsigned/big/little
  via P_OPTS. make_buf_view (Buf[a..b]-slices) förblir unsigned/native med FLIT --
  det är rätt default för en anonym byte-slice, och unpack.csp förlitar sig på det;
  signed/BE uttrycks via NAMNGIVET #field. Default för typlös #variable är signed
  (Tonys beslut) -- adder3/half_adder deklarerar nu bit-vars `unsigned`. Nytt test
  field_signed (LE+BE signed + unsigned-kontrast). test+san 52/52. Se DONE.md.

  Deklarationen bär `vt`, läsvägen struntar i den:

      #buffer F:8 out can 0x123
      #field S:16 out integer F[0..15]
      v = 0 - 300
      S = v
      -> S = 65236, inte -300

  Vyn är 16 bitar; att läsa ut den ska teckenutvidga när `vt` är V_INTEGER.
  Samma sak för `endian`: `make_buf_view` (som `<<=`/`>>=` bygger) hårdkodar
  dessutom V_UNSIGNED och E_NATIVE, så pack/unpack kan inte ens UTTRYCKA ett
  signed eller big-endian fält. Automotive-signaler är rutinmässigt båda.
  Två arbeten: (a) låt läsvägen respektera vt/endian, (b) ge pack-syntaxen ett
  sätt att ange dem.
  Hittades när #can/#field vägdes mot variabler+pack: det HÄR var argumentet
  för att behålla mekanismen, och det visade sig inte finnas i koden.

## Float-literal över 2^31 overflowar i parsern (2026-07-22) -- KLART 2026-07-27
  Löst: uint64-ackumulering + range-check per måltyp, ERR_NUMBER_RANGE. Se DONE.md.

  `d = 2500000000.0 * 4.0` ger `-7168`. UBSan pekar på csp_rt.c:3095:

      runtime error: signed integer overflow: 250000000 * 10 cannot be
      represented in type 'int'
      csp_rt.c:3111: left shift of negative value -1794967296

  Literalen byggs upp i en `int` och skiftas sedan till Q16.16. Både
  ackumuleringen och skiftet spiller. Bör antingen räknas i uint32/int64 eller
  avvisas med ett fel -- ett tyst fel värde är det sämsta utfallet.
  Verifierat pre-existerande (ren HEAD-build ger samma UBSan-utskrift).

## `-r <fil>` slukar filnamnet, och bart `-r` är atoi(NULL) (2026-07-22)
  Två fel i samma optionsrad. `case 'r': reactive = atoi(optarg);`

      ./csp -r prog.csp -i     # prog.csp blir ARGUMENT till -r, inte en fil
                               # -> tomt program, /list visar bara State
      ./csp -r -i              # optarg == NULL -> atoi(NULL), segfault

  Optionssträngen har `r:` (obligatoriskt argument) medan usage-texten lovar
  `-r, --reactive[=B]` (valfritt). Antingen `r::` + NULL-koll, eller ta bort
  det valfria ur hjälptexten. Fungerande form idag: `-r1`.
  Samma mönster värt att kolla på `-t/--transaction[=B]`.
  Verifierat pre-existerande (ren HEAD-build beter sig likadant).

## Oaligned pekarläsning i disassemblern -- M0-risk (2026-07-22)
  `csp_print.c:706`, `ro_ptr(&tok_table[t].name)`. UBSan:

      runtime error: load of misaligned address ... for type 'const void *',
      which requires 8 byte alignment

  Fyras av vilket `/list` som helst på ett program med regler, i alla exempel.
  Ofarligt på x86, men det här är EXAKT familjen som HardFaultade projektet
  förut (PACKED csp_func_t på Cortex-M0) -- en pekarmedlem i en packad struct
  som läses som pekare. Trolig fix densamma: ta bort PACKED från tok_table-
  posten, eller läs den via en byte-vis accessor.
  `make san` fångar det INTE, för escript-harnessen kör aldrig `/list`.
  Verifierat pre-existerande (ren HEAD-build ger samma).

## exit csp_linux after -d and -n or no program / no interaction

  if -d AND
     (not compile (-C) AND no file input AND not interactive (-i))
  then print debug info and exit early

## csp_buf_t: bitfält sparar NOLL. Mätt 2026-07-18, fyra layouter.
    nuvarande (allt uint8_t)              12 byte
    A: små fält hopslagna till ett u16    12
    B: xref:29 + resten inpackat          12
    C: allt i två u32-ord + en u8         12
    D: som C men PACKED                    9
  uint32 xref tvingar 4-bytes-alignment, så allt utom PACKED landar på 12
  oavsett hur bitarna arrangeras. Bitfält är alltså rent bortkastad möda här.
  PACKED ger 9 byte (25%), på cpx_m 70 buffertar = 210 byte. MEN: PACKED-array
  => oalignade multibyte-accesser på Cortex-M0, vilket är exakt fällan som
  HardFaultade projektet förut (packad csp_func_t). Rekommenderar INTE.
  För att nå 8 byte med naturlig alignment krävs <= 64 bitar; behovet är 71:
    hp 16 + nbytes 8 + dlc 8 + xref 29 + transport 2 + dir 2 + flags 4
  Alltså måste något verkligt offras: extended CAN-id (xref -> 24 bitar) eller
  hp -> 12-13 bitar. Ingetdera gratis.

  `loc` (RAM/ROM/IO) SKRIVS men LÄSES ALDRIG -- ta bort den. Krymper inte
  structen (alignment), men det är ett dött fält.

  STÖRRE FISK: nbuf ~= antal leafs, för varje konstant/timer/digital/variabel
  får en EGEN buffert via setup_slot/auto-buffer. cpx_m: 70 buffertar för ett
  program med en handfull riktiga buffertar. Alltså 12 byte metadata för att
  beskriva 4 byte value_t. Om VIEW_SLOT-leafs slapp csp_buf_t helt (vyn pekar
  rakt på en heap-offset) skulle buf-tabellen krympa till de riktiga
  buffertarna -- storleksordningen 700+ byte på cpx_m, ~3x mer än PACKED ger,
  utan alignment-risk. Verifiera antagandet först.

## CAN, kvar att göra
  - view.pos är en byte => bara de första 32 byten av en frame är adresserbara.
    Deklarationen klarar hela 64 (ca.bit är 9 bitar); setup_can vägrar nu
    explicit i stället för att wrappa tyst. Full CAN FD kräver uint16 pos,
    vilket kostar en byte per LEAF i csp_view_t -- mät innan.
  - Cyklisk TPDO som skickar ÄVEN när värdet är oförändrat går inte att uttrycka
    (dirty sätts bara vid ändring). Behövs en period på #can, eller ett sätt
    att tvinga fram en sändning.

  - Objektinstanser med #can-fält: alla instanser binder mot SAMMA #buffer, så
    de delar frame. Rimligt? Eller ska varje instans ha sin egen frame/id?
  - Buffert-parts (.dlc/.tx/.rx/.id) är INTE DIN/DOUT-skuggade -- de ligger på
    csp_buf_t, inte i en value-slot. Följd: `F.dlc = 3` följt av `x = F.dlc` i
    SAMMA cykel ger 3, medan `.period` på en timer (som ligger i value-sloten)
    hade gett det gamla värdet. Pinnat i tests/unit/can_parts (d1 == 3).
    För `.tx` är det rätt -- det är ett kommando, inte ett värde. För `.dlc`,
    som är konfiguration, är det en inkonsekvens mot `.period`. Avgör om det
    ska enhetliggöras eller dokumenteras som avsiktligt.
  - `.ext` (extended id) -- Tony förklarar varför senare. RTR medvetet utelämnat.

## Baka rule_ip/rule_state i ROM/EEPROM? (2026-07-26)
  IDÉ (Tony). Idag räknas rule_ip (ordinal->ip) och rule_state (ordinal->State-
  mask) om vid boot i csp_csr (number_rules / number_rule_states) -- RAM-only,
  reaktiv-only. Man SKULLE kunna baka dem i ROM (bredvid rom_idg/ofs/edg) och/
  eller EEPROM.
  AVVÄGNING (min lutning: behåll compute-at-boot):
  - De är HÄRLEDDA ur instruktionsströmmen, deterministiskt, EN pass -> boot-
    vinsten mikroskopisk även på AVR.
  - RAM-besparing: reaktivt är AV default på AVR (allokeras ej där); bara
    reaktiva kort (mkrzero, mer RAM) berörs. Svag.
  - CRC: instruktionerna är redan CRC-skyddade -> korrupt instr fångas FÖRE
    omräkning; baka ip/state = redundant CRC-yta.
  KOHERENS-ARGUMENTET (det starkaste): grafens KANTER (rom_idg/ofs/edg) är redan
  bakade -- varför baka kanterna men räkna om ip/state? Om man bakar allt blir
  hela reaktiva strukturen en bakad+verifierad enhet. Värt det OM: RAM-kritiskt
  reaktivt kort, ELLER man vill ha hela reaktiv-strukturen som en CRC-enhet.
  (Notera: rule_ip pekar redan förbi block-gaten via skip_gate -- en bakad
  rule_ip måste bakas med samma skip, annars NINSTATE-return-buggen igen.)

## Max ETT #in FAILSAFE block (2026-07-24)
  NU blie det en FAILSAFE modul istället!
  FAILSAFE-mekaniken finns (reserverat sticky state, #in FAILSAFE-block, se
  DONE). Kvar: parsern tillater flera #in FAILSAFE. Ska vara max ETT sa det ar
  entydigt utpekbart och verifierbart. csp_parse_in: om state==FAILSAFE och ett
  FAILSAFE-block redan finns -> fel. (Del av failsafe-arbetet; crc_failsafe och
  park-fallback aterstar ocksa -- se DONE-noten.)


## 1.0 (efter release)

### Image-format v8: ett sammanhangande objekt med offsets (2026-07-29)

  - **Ett objekt per image** i stallet for sju losa arrayer. `CSP_IMAGE_TYPE`
    (makro med alla langder) genererar structtypen; kompilatorn gor layouten,
    vilket ar det som haller byte-CRC:n arlig. `aligned(4)` pa typen -- headern
    ar PACKED, sa utan den arver structen alignment 2 fran `index_t`.
  - **Sektioner nas via offset, inte pekare.** Offsets overlever att imaget
    kopieras till en annan flashsida eller till RAM; pekare gor det inte, och
    pekare gar inte att kontrollera mot nagot.
  - **`crc_hdr` sist och over ALLT ovanfor** -- magic, size, role, generation,
    antalen, sektions-CRC:erna OCH offseten. En rutten offset hade annars
    skickat laddaren till en skrappadress utan att nagot protesterade.
  - **Generatorn raknar ut offseten sjalv** -- den maste, eftersom crc_hdr
    tacker dem och en CRC inte kan tas over varden bara C-kompilatorn kanner.
    `CSP_IMAGE_CHECK` later kompilatorn bekrafta varje offset med static
    asserts, sa bygget faller om de nagonsin skiljer sig at.
  - **Taggad prolog fore varje sektion** (`csp_sect_t`: tag + len i BYTE). Ger
    en cursor som gar igenom hela imaget utan att rora headern, och gor att en
    senare lasare kan hoppa over en sektion vars tagg den inte kanner.
    Verifierat: 7 sektioner, landar exakt pa `size`.
  - `magic` = `JAM\n`, plus `role` och `generation` -- falten som gor redundanta
    FAILSAFE-kopior och A/B till samma regel i stallet for tva specialfall.
    `--role` och `--generation` pa kommandoraden.
  - Laddaren tar en `const uint8_t* base`. `csp_image_ref_t` (en pekare) ar
    handtaget, eftersom varje image har sin egen genererade structtyp.
  - **Header-fri atervinning blev battre.** Forr atervanns antalen men positionerna
    kom fran linkerns pekare. Nu gar `img_from_walk` prologerna och atervinner
    positionerna ocksa. Mätt, tre fall:
      * trasig crc_hdr -> "sections verified by walk", programmet kor
      * trasig crc_hdr OCH forstorda offsets -> samma, offseten anvands inte alls
      * en andrad instruktion med intakt header -> "CRC mismatch in instr
        section", avvisas (data-korruption ar inte atervinningsbar)
  - Mega: flash 96.4k -> 97.4k, RAM oforandrat 1967. test + san 59/59, mega och
    mkrzero bygger. Sekventiellt och reaktivt image kompilerar, tva image lankar
    ihop utan kollision.


### `csp -C --prefix` -- flera image i samma firmware (2026-07-29)

  - `csp_rom_meta_t` fick `prefix`; `csp_dump_code` emitterar `<px>_str`,
    `<px>_decl`, `<px>_instr`, `<px>_idg/_ofs/_edg`, `<px>_states`,
    `<px>_header`. NULL/utelämnad = `rom`, så ett omodifierat `csp -C` ger
    exakt samma rom.c som förut (verifierat symbol för symbol).
  - `--prefix NAME` på kommandoraden. `-h` uppdaterad.
  - Stale-guarden i den genererade filen säger nu `<prefix>.c is stale`.
  - Verifierat: `csp -C -n --prefix failsafe examples/traffic_fail.csp` och ett
    vanligt `csp -C` kompilerar var för sig OCH länkar ihop utan
    symbolkollision (`ld -r`).
  - **Deskriptorn (`csp_image_t`)**: `hdr/str/decl/instr/idg/ofs/edg/states` i en
    struct. `csp_load_image(st, img)` gör jobbet, `csp_load_rom(st)` är en
    wrapper som skickar `&rom_image`. `rom_verify`, `rom_graph_ok` och alla fyra
    `rom_scan_*` tar imaget. Ingenting under laddaren känner längre ett image vid
    namn -- reaktiva grafen läste `rom_idg/ofs/edg` som globaler, går nu via
    pekare i `csp_rt_t`.
  - Deskriptorn är `RODATA` och läses via nya `ro_image()`. Åtta pekare = 16 byte
    RAM per image på AVR, och image är tänkta att komma i tretal. Mätt på mega:
    flash i stället för RAM gav de 16 tillbaka; hela steget kostar +6 byte RAM
    (de tre grafpekarna i `csp_rt_t`).
  - Rökt av med ett riktigt bakat ROM: `traffic.csp` -> rom.c -> länkat och kört,
    tillståndsmaskinen går som förut genom nya laddaren.
  - Plan för steg 2-4 (sammanhängande image, sektionstabell med offsets, magic/
    role/generation + flash-scan, sidplacering) i **doc/IMAGES.md**.
  - test + san 59/59, mega bygger.

### Pin-konfiguration blir ett API, `.dir` konfigurerar om på riktigt (2026-07-29)

  - **`csp_board_digital_config()`** är nu enda stället som vet vad en digital
    slots konfiguration BETYDER i hårdvara. Anropas av `csp_setup`, av en regel
    som skriver `.dir`/`.pullup`/`.pulldown`, och finns för en framtida
    safe-väg -- de kan alltså inte glida isär (en pinne konfigurerad på ett sätt
    och driven på ett annat).
  - **`.dir` konfigurerade inte om pinnen.** `pinMode` anropades bara i
    `csp_setup`, ur DEKLARATIONEN. Runtime-grindarna (`d.dir & DIR_IN` i
    csp_input, `& DIR_OUT` i csp_output) fanns redan och läser sloten -- det var
    bara hårdvarusidan som saknades. Ny `cfg:1` i `dvalue_t` bär begäran till
    board-lagret. `dvalue_t` blev exakt 32 bitar (7+4+2+1+1+1+16); verifierat att
    `sizeof(value_t)` fortfarande är 4. `avalue_t` är redan full, så analoga
    pinnar har ingen motsvarighet -- där grindar `.dir` bara runtime.
  - `cfg` nollas i BÅDA sloten (`iptr->d.cfg = optr->d.cfg = 0`, samma mönster
    som `csp_output_timer`). DIN/DOUT kopieras vid commit, så att nolla en av dem
    lämnar en stale begäran som kostar en pinMode på någon senare cykel.
  - `csp_setup` läser nu SLOTEN i stället för deklarationen -- samma sanning som
    runtime grindar på. Input-loopen före output-loopen är bärande: en pinne som
    finns i båda listorna (inout, eller samma fysiska pinne deklarerad två
    gånger) måste landa i det dess OUTPUT-roll vill ha. Nu dokumenterat.
  - **Dubbelläsning bortstädad** i `csp_input`: `csp_board_digital_input` gör
    redan `digitalRead` + `csp_set_ivalue`, och båda raderna upprepades direkt
    efteråt.
  - **En io-lista i stället för input[] + output[].** Alla pinnar ska kunna
    vändas, och en pinne som ligger arkiverad under sin DEKLARERADE riktning
    saknas i den andra fasen i samma stund som en regel vänder den. Båda faserna
    går nu över `st->io` och grindar på slotens AKTUELLA `dir`.
    Listan är MINDRE än de två den ersätter: en inout-post lagrades förut två
    gånger. Mätt på mega: flash 96736 -> 96128 (-608 B), statics 1967 -> 1961.
    Alternativen var sämre -- "swap mellan listorna" och "lägg varje pinne i
    båda" kostar båda dubbelt minne eftersom båda arrayerna ändå måste rymma
    värsta fallet.
  - Följd: om samma fysiska pinne deklareras två gånger under två namn avgör nu
    SISTA deklarationen dess läge (den konfigurerar sist). Förut avgjorde vilken
    fas som råkade köra sist. "Sista vinner" är samma regel som resten av
    språket patchar efter, och går att skriva ned.
  - `#field` utan riktning är inte device-I/O och står utanför listan.
  - Groundwork för typbytet (analog <-> digital): med en lista som bara betyder
    "det här är en device-leaf" blir ett typbyte en fråga om vad switchen
    dispatchar på, inte om att flytta poster mellan listor.
  - `/memory` visar en `io`-rad (ny sträng i strings.tab) i stället för in+out.
    `-P` skriver fortfarande `{input,...}`/`{output,...}`, filtrerade på
    DEKLARERAD riktning -- en listning ska visa vad programmet sa.
  - `.pin` och `.port` sätter också `cfg`. Missat i första omgången: vilken
    pinne en slot driver är en del av vad dess konfiguration ÄR
    (`csp_board_digital_config` läser `d.pin`), så en flytt behöver samma
    begäran som en riktningsvändning. Kvarstår som medveten hålighet: den pinne
    man lämnar behåller sitt läge och sin nivå, och inget namn i programmet
    pekar på den längre.
  - **`avalue_t` fick plats för cfg genom att `endian` togs bort ur sloten.**
    Den var inert: skriven av `setup_analog`, ekad tillbaka av `.endian`, läst av
    ingenting annat -- byteordning som BETYDER något bor i `csp_view_t.endian`,
    vilket är den ett bundet fält lägger ut sig med. Deklarationen
    (`csp_analog_t.endian`) är kvar och `.endian` svarar därifrån, så läsningen
    ger fortfarande det deklarerade värdet (verifierat: `#analog A:10 big`
    läser 2). Skrivning är nu en no-op. `avalue_t` = 7+4+2+1+1+1+16, en bit över.
    Alternativen mättes och valdes bort: `PORT_BITS` 4->3 bryter CPX (port 8 =
    accelerometer, 9 = NeoPixel, lästa i csp_board_analog_input/_output), och
    `PIN_BITS` 7->6 tar bort megas A0..A15 adresserade som digitala (54..69).
  - `csp_board_analog_config()`: PWM-utgång -> OUTPUT, ingång -> INPUT. Körs
    ENDAST från cfg-vägen, aldrig från setup -- på CPX namnger ett `#analog`
    en sensor via sin port, och att hävda ett läge på de "pinnarna" vid boot
    hade rört hårdvara ingen bett om.
  - `d.cfg` och `a.cfg` ligger INTE på samma bit (digital har pullup+pulldown
    före, analog bara pwm), så flaggan nollas via den medlem som satte den.
  - `.pin`-regeln (pinnen man lämnar överges) dokumenterad i båda manualerna,
    PDF:er omgenererade. `.endian` borttagen ur snabbreferensens
    digital/analog-rad -- den hör till fält.
  - Kvar: SAFE-läget (hårdvarufråga, blir ett FAILSAFE-objekt i FAILSAFE.csp).
  - Verifierat på värden att `.dir`-skrivningen landar och läses tillbaka
    (`B.dir` 1 -> 2 efter `B.dir = out`). Själva `pinMode` går inte att
    observera utan hårdvara/wokwi.
  - Byggt för mega (AVR, 37% flash) och mkrzero (ARM). test + san 59/59.

### lib/: Watchdog, Alarm, Filter -- och timers startar inte om (2026-07-28)

  - **`T = 1` startar bara om timern är STOPPAD.** `csp_output_timer` armar i
    else-grenen (`if (!running) { if (val) ... }`), så en skrivning till en
    löpande timer är en no-op. `T = 0` stoppar inte heller -- running-grenen
    tittar aldrig på `val`. Manualen säger `T = 1  // start/restart timer`.
    Idiomet "håll timern omstartad medan insignalen skakar" finns alltså inte.
    Ej ändrat i runtime: en fix ändrar semantiken för varje program som gör
    `T = 1` obetingat (fri gång i dag, aldrig-fyra efteråt). Tonys beslut.
  - **lib/debounce.csp gjorde inte det den påstod.** Den (och manualens
    Debouncer) startar timern vid första avvikelsen och SAMPLAR sedan vad
    insignalen råkar visa en period senare. Mätt: med studs varje cykel låstes
    fasen så att den alltid samplade samma flank -- Out=0 i 30 cykler av ren
    tur, och det var den turen debounce-testet mätte. Omskriven till tick +
    ms-räknare (`Settle`, `Hold`); nu nollställs Hold av varje rörelse och
    värdet accepteras först efter verklig stiltje. Testet kollar även Hold=0
    under studsen, så turen är borta ur mätningen.
  - **Nytt gemensamt idiom i lib/**: fri-gående `Tk` + `X = X + Tk.period ?
    timeout(Tk)`. Parametrarna blir vanliga variabler i ms (patchbara per
    instans, inte `.period`), Tk.period är upplösningen. Räknarna mättas vid
    sitt största meningsfulla värde -- annars når en larmad nod integer
    overflow och ser frisk ut igen.
  - **`In == Was`-grinden.** Den cykel insignalen rör sig är återställningen av
    räknaren inte committad än, så räknaren bär FÖREGÅENDE tillstånds ålder --
    nästan alltid förbi fördröjningen. Utan grinden fyrar Alarm på transienten
    och båda fördröjningarna är död kod (mätt: Active gick 1 direkt).
  - Watchdog (tystnad = fel, `In + 1 ? F.rx` när händelsen saknar värde),
    Alarm (OnDelay/OffDelay/Repeat/Ack, Send+Clear som ettcykelspulser),
    Filter (median-av-3 mot spikar + valfri IIR).
  - Filter i fixpoint stannar en LSB eller två före Med när Alpha < 1 --
    `Alpha*(Med-Out)` rundar till noll. Dokumenterat; testet kollar exakta
    tidiga steg (5.0, 7.5, 9.375) i stället för svansen.
  - `T.period` går att läsa i uttryck. Verifierat.
  - Nya tester watchdog, alarm (inkl. glitch-instans), filter. test + san 59/59.

### min/max/clip blir V_NUMBER, trunc/round tillagda (2026-07-28)

  - `min`/`max`/`clip` tar nu `V_NUMBER` in och ut, samma mönster som `abs`.
    `clip(0.5, -1.0, 1.0)` gav 0 förut; ger 0.5 nu. Heltalsvägen oförändrad.
  - **Argumentpromotering**: V_NUMBER coercar inte per argument, så
    `min(int,float)` hade jämfört en rå int mot ett fixpoint-ord. `process_fcall`
    avgör nu den gemensamma typen först (ett float-argument -> alla float) och
    promotar de övriga. `argcode`/`avt` får den PROMOTERADE typen, vilket också
    är vad `call_rtype` behöver.
  - **Fold-vägen läste okonverterat värde**: `dval = fn(st, argcode, arg, ...)`
    hämtar `rarg[j].val`, men coercionen skedde på en lokal kopia. Ett
    promoterat heltal hade foldats som rått bitmönster. Coercerat värde skrivs
    nu tillbaka till stack-posten.
  - `trunc` (mot noll) och `round` (närmaste, halva bort från noll) tillagda,
    `V_NUMBER` in / `V_INTEGER` ut; heltalsargument passerar oförändrat.
  - **`FIX_TO_INT` gjorde FLOOR trots att dess egen kommentar sa "truncate".**
    Följd: implicit float->int gav -3 i fixpoint-bygget och -2 i float-bygget
    för -2.5 -- samma källkod, olika svar. Truncerings- och rundningslogiken
    ligger nu i `fix_trunc()`/`fix_round()` (static inline i csp_fixpoint.h,
    samma stil som fix_sqrt), och `FIX_TO_INT`/`FIX_TO_INT_RND` anropar dem.
    Alltså går `op_CVTFI` samma väg som `trunc()` -- implicit och explicit kan
    inte längre säga olika saker, och byggena är överens.
    `FIX_TO_INT_RND` var oanvänd OCH rundade halvor mot +oo; följer nu C:s
    round() (halvor bort från noll) som `fix_round`.
  - Nytt test numfun (inkl. implicit narrowing). test + san 56/56.

### abs blir V_NUMBER, fabs bort, negativa fixpoint-tal skrevs ut fel (2026-07-28)

  - **CVTIF emitteras korrekt** för int-argument mot float-parameter (verifierat
    i disassemblyn: `LD -> CVTIF -> ARG -> CALL`). MEN `argcode`/`avt` bär
    argumentets URSPRUNGLIGA typ, före coercion. För V_NUMBER är det rätt
    (ingen coercion sker) -- men flerargumentsfallet `min(int,float)` måste
    promota argumenten själv innan V_NUMBER kan användas där.
  - `abs` tar nu `V_NUMBER` in och ut; `fn_abs` dispatchar på `type & 0xf` som
    `fn_sign`. `fabs` borttagen (tabell, fn, strings.tab, båda manualerna,
    farith-testen, lib/pid.csp). NOLL nya tabellposter -- `csp_instr_call_t` är
    exakt 32 bitar och `csp_instr_t` en 4-bytes union, så FUNC_BITS 5->6 hade
    breddat VARJE instruktion i varje program.
  - Ny `call_rtype()`: `rtype == V_NUMBER` betyder "bredaste argumenttypen".
    Det är regeln som gör V_NUMBER användbar för min/max/clip också.
  - `func->rtype` lästes utan rom-skydd (rå PROGMEM-deref) -- ny `func_rtype()`
    med rd8, samma mönster som func_pure/func_arity.
  - **`fprint_fvalue` skrev ut varje negativt fixpoint-tal en hel enhet fel.**
    `FIX_TO_INT` är en aritmetisk skift = FLOOR, och den parades med en
    bråkdel tagen ur BELOPPET: -2.5 blev "-3.500000". `csp_print_fixpoint`
    (println-vägen) hade rätt hela tiden, så println och -Q sa olika saker om
    samma värde. Nu tas båda halvorna ur beloppet och tecknet sätts för hand.
    Detta drabbade -Q OCH -s-dumpen, alltså allt man felsöker med.
  test + san 55/55.

### Manual-audit mot koden: hela FD-ramen, .endian, bredd-validering (2026-07-27)

Genomgång av doc/manual_en.md mot koden efter 1.0. Fyra saker i koden var fel
eller osagt, resten var manualen som halkat efter.
  - **Fältbitar > 255 avvisades.** csp_view_t.pos är uint16 sedan 289d41e, men
    setup_field hade kvar `if (pos > 255) error` + `(uint8_t)pos`-casten. Bort
    med båda: ca.bit är 9 bitar (0..511) och ramen är enda gränsen -- hela
    64-bytes-FD-ramen är adresserbar nu. Test: field_fd (bit 496..511, BE, 32b).
  - **`.endian` fanns bara på #analog.** Manualen lovade den på bundna fält;
    csp_dio_get_part/set_part hade ingen HEAP-gren för PART_ENDIAN, så den läste
    0 och skrev ingenting. Endian bor i vyn (som pos och len) -- läs/skriv
    därifrån, oshadowat precis som PART_DIR.
  - **Tyst bredd-trunkering.** `#variable X:40` blev 8 bitar (res är 5 bitar och
    håller bits-1), `#field w:40` likaså, och `bind`/`Buf[a..b]` hade INGEN
    range-koll alls -- en bind utanför bufferten skrev utanför heapen
    (csp_heap_get/set litar på vyn). check_res() + MAX_RES_BITS/MAX_VIEW_BIT i
    csp.h; variable/constant/analog/field kollar bredden, bind och make_buf_view
    kollar bit-range MOT bufferten. Avvisa, inte wrappa.
  - Manualen (BÅDA språken, en + sv, PDF:er omgenererade): FD-gränserna,
    FAILSAFE (tredje implicita staten, och att den är STICKY -- stod
    ingenstans), default-signedness för typlös `:N`, bredd-gränsen 1..32,
    `.endian`, `-r` (manualen påstod `-r0`/`-r1` som inte finns), `--no-eeprom`,
    EEPROM-överlagringen vid boot, och appendixets `#buffer :<bits>` som
    motsade brödtextens BYTES.
  - `#in FAILSAFE` hålls MEDVETET tunt i manualen, med en not om att den tänkta
    formen är `#module FAILSAFE` som egen ROM-image + EEPROM-bank (flera banker,
    en av dem den säkra; växling = rebase). Se FAILSAFE-planen överst i TODO.md.
  test + san 53/53.

## 0.9 (pågående)

### `#field integer` teckenutvidgar; signed/BE CAN-signaler funkar (2026-07-27)

`#field S:16 integer F[0..15]` läste tidigare tillbaka osignerat (S=v där v=-300
gav 65236). Deklarationen bar redan vt/endian -- läsvägen struntade i vt.
  - csp_heap_get teckenutvidgar nu på bit-vägen när vw->vt == V_INTEGER: fyller
    de höga bitarna från fältets bredd (len+1) upp till containern. Osignerade
    fält och 32-bitsbreda (inga lediga högbitar) lämnas som get_bits gav dem.
  - endian fungerade redan: setup_field kopierar ca.endian -> vw->endian och
    csp_heap_get har en E_BIG-gren. Syntaxen fanns också: #field tar
    integer/unsigned/big/little via P_OPTS (bekräftat i parse-dumpen).
  - make_buf_view (Buf[a..b]-slices) förblir unsigned/native MED FLIT -- rätt
    default för en anonym byte-slice, och unpack.csp bygger på det. Signed/BE
    uttrycks via namngivet #field, inte via slice-syntaxen.
Default-signedness för typlös #variable = signed (Tonys beslut). Följd: en smal
signed var som håller en satt topbit läser negativt -- 1-bitars signed kan bara
vara 0/-1. adder3/half_adder deklarerar därför sina bit-vars `unsigned` (de ÄR
osignerade bit-kvantiteter; testen var fel, inte semantiken). Nytt test
field_signed: LE 4-bit + 8-bit signed, unsigned-kontrast över samma bitar, och
ett BE 16-bit signed round-trip. test+san 52/52.

### Number-literal overflow avvisas istället för tyst fel värde (2026-07-27)

`d = 2500000000.0 * 4.0` gav `-7168` -- literalen byggdes i `int` (`v*10`) och
skiftades till Q16.16, båda spillde (UBSan: signed overflow + left shift of
negative). Nu ackumuleras i uint64 (ingen UB under skanningen) och range-checkas
när måltypen är känd:
  - float (Q16.16): heltalsdel > 32767 avvisas (leading '-' är separat token, så
    literalen är alltid icke-negativ; -32768.0 går inte att uttrycka -- inneboende).
  - int: > INT32_MAX (2147483647) avvisas.
  - hex: > 8 nibbles avvisas; ackumuleras i uint32 så 0xFFFFFFFF-idiomet (== -1)
    behålls utan UB.
Ny felkod ERR_NUMBER_RANGE ("number out of range") via strings.tab. Tyst fel
värde var sämsta utfallet -- nu ett tydligt fel. VERIFIERAT under UBSan+ASan:
overflow-inputen ger ren avvisning utan UB-diagnostik; 3.14/32767.5/2147483647
parsas rätt; test+san 51/51.

### str + state self-verify: alla fyra ROM-sektioner oberoende (2026-07-26)

Färdigställer per-sektions self-verify (decl/instr fick END-markörer tidigare).
Nu självverifierar ALLA fyra ROM-sektioner utan headern:
  - str: 3-byte trailer efter datan -- 0xFF-sentinel (aldrig en giltig längd
    eller ASCII, markerar slutet för header-fri skanning) + 2-byte crc. rom_scan_
    str skannar 0xFF, verifierar crc över [0..sentinel]. rom_str blir strp+3.
  - state: sentinel-state (snum 0x7f -- snums är 0..15) + en crc-state som packar
    16-bit crc över name(9)|snum(7)<<9. rom_scan_state. rom_states blir ns+2.
  - csp_load_rom recovery skannar nu ALLA fyra (decl/instr-markörer + str/state-
    trailrar) -- helt header-oberoende. Header-crc:erna oförändrade (över datan);
    generatorn förberäknar dem en gång och delar med trailrarna.
ROM_FORMAT_VERSION 6->7. VERIFIERAT: korrupt n_str I HEADERN (+ crc_hdr) -> v6
skulle avvisat (header-baserad str-verify), v7 ÅTERHÄMTAR via str/state-trailern
och kör. Normal load oförändrad. test+san 51/51, firmware mkrzero 27%.
Detta är substratet för FAILSAFE-modulen (TODO): str-integritet låser upp
FAILSAFE-print. FAILSAFE_INIT struket -- modulens #in INIT tar det.

### END-markörer (ROM header-recovery) + EEPROM-always-load + CRC-fuzzer (2026-07-26)

Tre saker.

1) Self-verifierande sektions-markörer (ROM). rom_decl/rom_instr avslutas nu med
en DECL_END_MARK / OP_END_MARK (reserverade 0x3f) som bär en self-CRC över
[sektionsdata + markören med crc-fältet 0:at]. Så sektionen kan verifieras UTAN
headern: skanna till markören (position = längd), verifiera crc:n. csp_dump_code
emitterar dem (arrayerna blir nd+1/nn+1; header-crc:erna oförändrade, över datan).
csp_load_rom: när crc_hdr FAILAR (och bara då -- en data-korruption med intakt
crc_hdr foldar markören samma trasiga byte) skannar rom_scan_decl/instr
markörerna; om de + str/state (via headern) verifierar -> KÖR med varning
"header CRC bad -- sections verified via END markers". ROM_FORMAT_VERSION 5->6.
VERIFIERAT: korrupt crc_hdr + intakta sektioner -> återhämtar och kör; korrupt
decl-DATA -> avvisar (ingen recovery). (str/state saknar markörer än -> header-
beroende; TODO.)

2) BUGG: EEPROM laddades inte med firmware. csp_linux gejtade boot-auto-load på
!csp_has_firmware() -> ett kort med bakad firmware laddade ALDRIG sina EEPROM-
patchar. Fel -- patcharna lever i EEPROM OVANPÅ ROM (Arduino gör alltid rätt:
load_rom + eeprom_load). Fix: villkoret blev !given && !no_eeprom; ny
--no-eeprom-flagga. "would clobber ROM"-kommentaren var föråldrad (eeprom_load
gör om load_rom internt). VERIFIERAT: patch sparad ovanpå firmware laddas efter
reboot. Se [[project_eeprom_always_loads]].

3) CRC-destroyer-fuzzer. tests/crc_destroyer.sh + bitflip.c + crc_prog.csp
(FAILSAFE-program). Sparar programmet till EEPROM, flippar bitar (1-bit
EXHAUSTIVT, multi-bit samplat) under ASan/UBSan, och kräver att VARJE korrupt
load hanteras graciöst (restore/reject) -- ALDRIG krasch/hang. make-target
test_crc_destroyer (ej i make test -- långsam). Quick-override CRC_MAX1/CRC_MULTI.
VERIFIERAT: 0 krascher; varje 1-bit-flip i EEPROM avvisas (CRC fångar allt).
(EEPROM saknar markörer -> testar reject, ej recovery; ROM-recovery-fuzzning via
executable-poke är nästa steg.)

test+san 51/51.

### Reaktiv state-dispatch: rule_ip förbi gaten + rule_state (2026-07-26)

STEG mot att ta bort den "hemliga" per-regel State-EQI:n i reaktivt läge.

STEG 1 (klart, fixar LATENT BUGG): rule_ip pekar nu FÖRBI block-gaten
(skip_gate i number_rules). Roten: csp_react anropar csp_eval_rule EN gång per
regel, men OP_NINSTATE gör `return n+nxt` för att hoppa in i blocket -> return:en
AVSLUTAR anropet utan att köra kroppen. Så första regeln i ett multi-state
`#in a b`-block kördes ALDRIG reaktivt (single-state INSTATE faller igenom, så
den funkade -> buggen var maskerad). Med rule_ip förbi gaten går reaktivt in vid
villkoret; den infällda EQI:n gejtar. VERIFIERAT: in_multi kör nu i BÅDA lägena
(hit=2 seq OCH reaktivt). skip_gate/gate_mask rör bara GLOBALA State-gates
(LD läser st->sx) -- objekt-`#in` gejtar på obj.State (per-instans) och lämnas.

STEG 2a (klart, grund för 2c): rule_state[ord] -- en State-mask per regel byggd i
csp_csr (från gate-kedjan / OP_RULE.implicit för bare). csp_react testar
State ∈ rule_state[ord] INNAN csp_eval_rule ("testa State innan vi hoppar") och
skippar out-of-state-regler utan att köra eval. Den infällda EQI:n är KVAR än så
länge (redundant backup + beroende-kant). Bara på reaktiva byggen (SUPPORT_
REACTIVE); AVR-default är seq -> ingen kostnad där.

STEG 2c (KLART): den infällda EQI:n är fysiskt BORTA. asm_rule emitterar inget
per-regel State-test längre (cnd=-1). State->regel-kanterna flyttades till
rule_state-baserade kanter i csp_csr: gate-LD:n skippas (is_gate_ld) i pass 1+3,
och varje gejtad ordinal får en State-kant (idg-räkning + add_state_edge med
samma dedup som instruktions-fyllen; över-räkning ger hål, ofarligt). Sekventiella
bare-regler gejtas av en OP_RULE.implicit-check i csp_eval_rule (mot INIT+NORMAL).
asm_EQI/EQEQ/OR #if 0:ade (sparade för framtida peephole; OP_EQI körs/listas/
grafas fortf.). VERIFIERAT: 0 st OP_EQI i reaktiv in_multi-ROM (hemliga EQI borta);
listningen ren (ingen folded guard); NORMAL+ FAILSAFE-isolering intakt reaktivt
(State-kanterna funkar). rule_state (2B/regel, RAM-only, reaktiv-only) betalar sig
nu -- ersätter N EQI+OR-instruktioner per multi-state-regel.

test+san 51/51. Firmware mkrzero 26% (mindre -- EQI-emission borta), mega
opåverkad (seq). Kvar (kosmetik, uppskjutet): användarens EXPLICITA `State==a`
listas som `State==3` (EQEQ-vägen resolvar ej statnamn) -- det peephole vi sparade.

### Multi-state #in A B C + NORMAL+ (FAILSAFE-isolering) (2026-07-25)

Två sammankopplade features (löser att globala regler läckte in i FAILSAFE).

1) `#in A B C` -- ett block som kör i NÅGOT av de listade tillstånden (OR).
   Sekventiell gate = OR-kedja: OP_NINSTATE (nytt opcode, "hoppa IN i blocket om
   State==imm") per tillstånd utom sista, avslutad av OP_INSTATE (skip om !=).
   open_in_block() bygger kedjan, patchar NINSTATE-hoppen till blockstart, INSTATE
   patchas vid #end. Enkel `#in <s>` är byte-identisk med förr (en INSTATE).
   Reaktiv: per-regel-villkor blir (State==A||State==B||...) via asm_OR (aktiverad
   ur #if 0). Listern rekonstruerar via list_state.

2) NORMAL+ : en bar top-level-regel (utan #in, ej i modul) körs default i de
   INBYGGDA drift-tillstånden INIT+NORMAL, aldrig i ett speciellt tillstånd. Så
   globala regler TYSTNAR när maskinen går in i FAILSAFE/user-state -- FAILSAFE
   blir en ö. Stateless-program (sitter i INIT) kör från cykel 0, ingen startup-
   transition. Implementation: BARA per-regel-villkoret (State==INIT||NORMAL)
   fälls in (via sdefv, asm_rule) -- INGEN blockgate (en per-regel-gate satte en
   INSTATE på varje regels reaktiva entry-ip och kraschade csp_react; villkoret
   ensamt gejtar både seq och reaktivt). OP_RULE fick en `implicit`-bit (nxt
   16->15) så listern suppar den injicerade State-guarden -> bar-regler listas
   bart.

Listning: BÅDA listerna (/list i csp_rt.c cmd_list OCH -P i csp_dump.c
csp_list_rules) rekonstruerar nu `#in <states> ... #end` från gate-kedjan --
header/#end utan radnummer, reglerna inuti numrerade med State-guarden suppad
(via list_states-set + in_list_states). Bar-regler listas bart. (Känd kvarleva:
en användares EXPLICITA `State==a`-villkor listas som `State==3` -- EQEQ-vägen
resolvar ej statnamn; pre-existerande, syns mest i konstruerade tester, ej i
traffic.)

FÄLLOR som bet: OP_NINSTATE saknade op_info-entry -> NULL-namn -> segfault i -C -r
(fixat). Per-regel-blockgate bröt reaktiva grafen (borttagen, se ovan). Stateless
nådde aldrig NORMAL -> {INIT,NORMAL} istället för bara NORMAL. CandySpeak/csp_
config.h är EGEN fil (ej symlänk) -> MAX_IN_STATES måste in där också.

ROM_FORMAT_VERSION 4->5, EEPROM_VERSION 9->10 (instr-wire-format ändrat: OP_
NINSTATE + INSTATE nxt 14->13+implicit + RULE nxt 16->15+implicit).
VERIFIERAT: test+san 51/51. `#in a b` kör i a och b (hit=2). Bar-regel fryser i
FAILSAFE (n=frozen=4). Multi-state ROM round-trippar. traffic.csp (user-migrerad
till #in red redyellow green yellow) kör. Firmware mega 37%/mkrzero 26%.
Nya tester: in_multi, normal_plus (seq-only; self-inc-regler triggar ej reaktivt).

### #buffer: nbits->nbytes, csp_field_t, större buffrar (2026-07-25)

Följdändringar på byte-beslutet:
  - csp_bufdecl_t.nbits -> nbytes: lagra bytecount direkt, inte bits (res*8).
    Parsern lagrar res, cap 1023 byte (var 127). Två bit-kontexter behöll *8:
    setup_field (fält-far-plats: nbytes*8) och setup_buffer (res = nbytes*8).
    Byte-kontexter (est_leaf, listrar, emitter) använder nbytes rakt av.
    ROM_FORMAT_VERSION 3->4.
  - csp_can_t -> csp_field_t (typen; unions-medlemmen ca kvar). #can-arvet borta.
  - EEPROM_VERSION 8->9: en gammal save far INTE laddas nar bits blir bytes.
    Subtilt: sparade decl-bytes ar BYTE-IDENTISKA (samma 10-bitsmonster, bara
    tolkningen andras), sa varken rom_fp eller crc_decl fangar det -- bara
    versionen. Samma sak for ROM (crc_hdr oforandrad -> version-bumpen kravs).

Buggen den storre maxen avslojade: buffer-subsystemet var uint8_t rakt igenom
(csp_buf_t.nbytes, csp_buf_alloc-param, setup_buffer-lokal), sa 256..1023-byte
buffrar trunkerades (300 -> 300&0xff = 44 byte allokerat) -> heap-overskrivning
vid byte-index. FIX (storleksneutral): droppade doda csp_buf_t.loc (skrevs alltid
0, lastes aldrig), breddade nbytes uint8_t -> uint16_t. Samma struct-storlek.
csp_buf_alloc-param + setup_buffer-lokal ocksa uint16_t.

VERIFIERAT: 300-byte buffer, byte 299 lagrar/laser tillbaka ratt; ASan ren (ingen
overskrivning); :1024 avvisas. Test 49/49, san 49/49, firmware mega 36%/mkrzero
26% (RAM oforandrad -> struct holl storleken).

### #buffer-storlek: alltid BYTES (2026-07-25)

Var inkonsekvent: vanlig buffer angav storlek i BITAR (`#buffer B:8` = 8 bitar),
CAN-buffer i BYTES (`:8 can` = 8 bytes DLC). Samma `:N`, två betydelser --
forvirrande. Nu ALLTID byte: en #buffer ar en byte-behallare, oavsett transport.
En regel: buffer = byte, #field = bitar, #variable = maskat sub-byte-varde.

  - csp_parse_buffer: nbits = res*8 for bade plain och CAN (var res respektive
    res*8). Cap 127 byte (nbits <= 1023-faltet), CAN 64 (FD). Default 8 byte.
  - Listrar (board csp_rt.c, csp_dump.c csp_list_decl + Erlang -P): alla visar
    nu byte (nbits>>3), inte den villkorade bits/bytes.
  - OFFER: sub-byte skalar-buffrar (B10:10 = 10 bitar maskat) forsvann -- det ar
    nu #variable:s jobb (bar egen bitbredd) eller #field (bit-vy). tests/unit/
    buffer_scalar omskriven till byte-semantik (B1:1/B2:2/B3:3, maskning till
    byte-bredd: Masked1=300&0xff=44, Masked2=70000&0xffff=4464) + .expect.
  - Ovriga buffer-tester/exempel opaverkade: CAN var redan byte; plain-buffrar
    med falt i laga byte behaller observerbart varde. cpx_m_color Live:16->:2,
    manual (en+sv) + BUFFERS.md uppdaterade.

VERIFIERAT: B:2 -> 2 byte, CAN :8 oforandrat, -P visar {size,2}. Bakad byte-
buffer-ROM laddas rent med ratt storlekar och varden. Test 49/49, san 49/49,
firmware mega 36%/mkrzero 26%.

### csp_dump.c: FIELD/BUFFER-luckor i output-stilarna (2026-07-24)

KRITISKT: ROM-emittern (csp_dump_code) klumpade DECL_BUFFER med END/VIEW/NONE
och skrev bara common -- tappade bf.nbits/transport/id. En #buffer bakad i ROM
fick nbits=0 (noll storlek) OCH crc_decl mismatchade (folden ser riktiga nbits,
emitten skrev 0) -> "ROM rejected: CRC mismatch in decl section". Nu egen gren
som skriver .bf={...}. VERIFIERAT: buffer_scalar bakad laddas rent, buffervärden
IDENTISKA mot körning som källa (B16=5000, B10=1000, Masked8=44, Masked10=976);
CAN-buffer F300 bakas med transport=2, id=0x300.

Övriga stilar hade också luckor (nu ifyllda):
  - csp_dump_decl (-P Erlang parse-dump): DECL_BUFFER saknades -> tom rad. Nu
    {decl,N,buffer,"namn",[{size},{type},{transport},{id}]}.
  - csp_list_decl: DECL_BUFFER saknades; DECL_FIELD skrev gamla "#can" -> nu
    "#field" (+ #buffer name:size [dir] [can 0x<id>], matchar board-listern).
  - csp_dump_object (objekt-medlemsdump): bara var/timer -> la till BUFFER,
    DIGITAL, ANALOG, FIELD. Buffer i modul syns nu per instans i state-dumpen.
  - csp_dump_state (globala): DECL_FIELD saknades -> tillagt.
  - csp_dump_rule (edge-list): DECL_BUFFER tillagt (pack gör buffern reaktiv).

FRÅGA: "#buffer i objekt -- funkar det?" JA. Funktionellt sedan tidigare (per-
instans-storage, se tests/unit/module_buffer). Dumpen skippade dock medlemmen;
nu syns m1.B=0x201 / m2.B=0x809 korrekt oberoende per instans.

Bara csp_dump.c (host-only, ej i firmware). Host test 49/49, san 49/49.

### EEPROM använder csp_image_header_t + korrupt graf => sekventiell (2026-07-24)

Två saker.

1) csp_eeprom byggd på csp_image_header_t (EEPROM_VERSION 7->8). RAM-patchen
beskrivs nu av en inbäddad csp_image_header_t precis som rom_header beskriver
flashen -- samma fold/verify-form för båda. Ersätter den lösa ram_nd/nn/strp/ns
+ enda data_crc med per-sektions-CRC (str/decl/instr/state), så en korrupt save
PEKAR UT sektionen. #disable-bitmappen får eget descriptor (n_dis + crc_dis).
ROM-identiteten är nu hela rom_header.crc_hdr (komplett fingeravtryck: räknare +
alla sektions-CRC) i st f rom_nd/nn/strp+rom_crc. Yttre crc_hdr täcker headern så
en flippad räknare inte kan vilseleda data-läsningarna. RAM: samma maskin
skriver/läser -> råa bytes, ingen ROM-normalisering (runtime-scratch verbatim).
  VERIFIERAT (host, scratch-eeprom, en save+load): 2 decls/18 instrs + disable-
  set round-trippar, regel visas R! efter load. Korruption pekar ut sektion:
  header->crc_hdr (generiskt fel), data->"decl section", dis->"disable set".
  Avvisad load lämnar körbart state. Gammal v7-db ignoreras graciöst vid boot.

2) Korrupt graf => sekventiell körning, inte avvisning. rom_graph_ok() bröts ut
ur rom_verify: en trasig graf är återställbar (grafen är bara reaktiv-schemaets
optimering), instruktionerna är intakta. csp_load_rom sätter rom_nedg=0 och
csp_cycle faller till csp_eval (full sekventiell) -- transaktionsmodellen ger
samma committade state. Varnar "ROM graph corrupt -- running sequential".
  VERIFIERAT: full_adder med korrupt rom_edg kör sekventiellt; /state IDENTISKT
  mot intakt reaktiv körning. Degradera mot körande, inte mot dött.

Host test 49/49, san 49/49. Firmware mega 36%/mkrzero 26%, rent.

### Graf-CRC: rom_idg/ofs/edg i integritetskontrollen (2026-07-24)

Den reaktiva grafen (rom_idg[nd], rom_ofs[nd+1], rom_edg[nedg]) var enda ROM-
sektionen utanför CRC-täckningen. Lade till crc_graph i csp_image_header_t
(före crc_hdr, så headern täcker den). ROM_FORMAT_VERSION 2->3.

  - Byte-CRC giltig: index_t == uint16_t (fast 16-bit, byte-stabil host/LE-
    target), runtime läser via ro_word -- samma bytes generatorn folder.
  - rom_verify foldar de tre arrayerna i samma ordning/storlek som emissionen,
    men BARA när n_edg > 0 (annars stub-arrayer som aldrig läses; crc_graph=0).
    Konsistent med runtime som redan gejtar alla graf-läsningar på rom_nedg.
  - Generatorn: crc_graph = 0xFFFF-fold över st->idg/ofs/edg när nedg>0.

VERIFIERAT: reaktiv full_adder-ROM (-r1, n_edg=4) laddas rent (crc_graph=14238
== runtime-verify). Korrupt idg/ofs/edg -> "graph"-sektion. Host test 49/49,
san 49/49. Firmware mega 36%/mkrzero 26%, rent.
NOT: grafen bakas bara med fäst flagga (-r1); "-r fil" sväljer filnamnet
(optional_argument optarg=NULL) -- separat känd bugg på TODO.

### csp_image_header_t: samla ROM-header i en struct (2026-07-24)

De 7 lösa rom_*-skalärerna (version, n_str, n_decl, n_instr, n_edg, n_states +
per-sektions-CRC:er) spreds för vinden. Samlade dem i en enda PACKED
csp_image_header_t (version, n_*, crc_str/decl/instr/state, crc_hdr sist). ROM_
FORMAT_VERSION 1->2.

  - ro_header(p) läser ut hela headern (AVR: memcpy_P, host: deref). Aldrig
    derefa en PROGMEM-struct direkt.
  - rom_verify(h): crc_hdr först (över sizeof(h)-sizeof(crc_hdr)), sedan
    crc_str/decl/instr/state. Returnerar sektionsnamnet som fallerar.
  - csp_dump.c-generatorn emitterar EN rom_header-literal med alla CRC:er
    (decl-CRC över kanoniserade decls: nollar runtime-scratch). Kompiletids-
    guard #if ROM_FORMAT_VERSION != 2 / #error mot förlegad rom.c.
  - Tom rom.c: all-noll header, aldrig avvisad (verify körs bara n_instr!=0).

VERIFIERAT end-to-end: cpx-ROM laddas rent (generator-CRC == runtime-verify).
Korrupt data pekar ut rätt sektion: str-data->str, decl-data->decl,
instr-data->instr, korrupt CRC-fält->hdr (crc_hdr trippar först, väntat).
Host test 49/49, san 49/49. Firmware: mega 37% flash/23% RAM, mkrzero 26%/16%,
inga varningar.

### Full ROM-CRC + EEPROM-payload-CRC + LE-guard (2026-07-23)

Utökade rom_crc fran bara instruktioner till hela ROM-bilden (str + decls +
instr + storleks-skalarer), och gav EEPROM en egen data_crc over sin payload.

MATT (objcopy, fixtur, x86/avr/arm-LE vs arm-BE) att avgora fragan om decl ar
byte-stabil:
    x86 LE / avr LE / arm LE:  I=ed008d04  D=4155e357 44332211   IDENTISKA
    arm BE:                    I=b4c048d0  D=05aa8fe5 11223344   SKILJER
Decl ar lika byte-stabil som instr INOM samma endianness -- PACKED, bitfalt
packas likadant, -fshort-enums paverkar inte enum-BITFALT. Sa en byte-CRC over
decls funkar. BE skiljer (bitfalt MSB-forst), och det ar INTE en ren word-swap
(scalarer swappar, bitfalt gor det inte). Alla riktiga host/mal ar LE, sa:
  - csp_crc16(crc, ptr, n, is_rom): generell inkrementell byte-CRC (ro_byte for
    flash). Ersatter csp_rom_crc16.
  - LE-guard: CSP_STATIC_ASSERT(__BYTE_ORDER__==LITTLE) dar csp_crc16 bor. En
    BE-host/mal failar bygget med tydligt meddelande i st f tyst CRC-miss.
  - Skalarerna rom_str_len/n_decl/n_instr/n_edg/n_states: int -> uint16_t
    (int var 2B AVR / 4B host -> inte byte-stabil). Nu byte-stabila.
  - rom_image_crc() (runtime) och generatorn folder SAMMA byte i samma ordning.
    Generatorn maste nolla runtime-scratch (is_mapped/bound/reg, timer
    fired/running/_res) sa dess live-decl matchar emitterad rom_decl -- KOPPLAT
    till emittern (dokumenterat i bada).
  - EEPROM: data_crc over payloaden (str+decls+instr+states), skild fran rom_crc
    (firmware-fingeravtryck). Samma maskin skriver/laser -> ra byte, ingen
    normalisering. Verifierat: korrupt payload-byte -> "cannot load", ren
    ROM-baslinje kvar. EEPROM_VERSION 6 -> 7.
  - BONUS-FIX: csp_eeprom_load:s felvag korde csp_rt_init (river view/heap) men
    rebuildade inte -> nasta host-cykel kraschade mot NULL. Nu rebuildar
    felvagen om rt_init hann kora (did_init-flagga). Pre-existerande, exponerad
    av payload_crc.
Ej i CRC:n: reaktiva grafen (bara -r ROMs) och rom_states (state_t ej PACKED,
4B host / 2B AVR). Se TODO.

### Dubbel-tokenisering bort ur djup-vägen (2026-07-23)

csp_process_line klassificerade en bar rad (regel vs immediat query) genom att
tokenisera hela raden till ett token_t tv[MAX_LINE_TOKENS] och leta EQ/QUEST --
och slangde sedan resultatet, varpa nedstroms (csp_parse ELLER
csp_process_immediate) tokeniserade IGEN. Den frame satt direkt ovanfor
csp_parse:s egna tv[24], sa det var 144 byte av djup-vagens stack for ingenting.

Ersatt med line_is_rule(): en teckenscanner som gor samma "topp-niva EQ eller
QUEST"-test. Finessen en ra '='-scan missar: '=' finns aven i ==, <=, >=, != (
jamforelser, INTE tilldelning), sa '=' raknas bara som en ENSAM '=' (varken
foljd/foregangen av =<>!). Strangar och //-kommentarer hoppas over, sa
print("a=b") och `x // = c` ar queries. RIMP '<-' har varken '=' eller '?', sa
`ra <- fa` blir query precis som forr.

Bevisat IDENTISKT beteende: gamla tv[]-klassificeraren och nya scannern ger
samma beslut pa 14 kantfall (inkl. sträng-'=', kommentar-'=', a=b=c). 48/48.
Sparar 144 byte pa parse-djupvagen -> margin bor stiga fran +617. Nasta
kandidat om mer behovs: csp_parse_expr:s ostack[10]+rstack[10] (~130 byte).

### ROM version + CRC (2026-07-23)

Robusthet mot stale/korrupt generat -- exakt klassen som kostade oss 18-juli-
ROM:en och EEPROM v5. Generatorn (csp -C) bakar in tva ROM-konstanter,
verifierade i tre lager:

  - `#if ROM_FORMAT_VERSION != N / #error` overst i rom.c -> ett gammalt generat
    BYGGER inte mot en nyare csp.h. Fangar "glomde regenerera" tidigast mojligt.
  - `rom_version` (uint16) + koll i csp_load_rom -> fel firmware/rom-par avvisas
    vid boot.
  - `rom_crc` (uint16, CRC-16/CCITT over ROM-instruktionerna) + koll i
    csp_load_rom -> trasig flash (dalig upload, brownout) avvisas vid boot.

Avvisning = kor TOMT med ett meddelande ("ROM rejected: format N ..." /
"CRC mismatch"), inte krasch -- kortet forblir svarbart och orsaken syns.
ROM_FORMAT_VERSION i csp.h, bumpas nar csp_decl_t/csp_instr_t-layouten eller
rom_*-symboluppsattningen andras (NAMEPOS_BITS hade bumpat den). Bo rjar pa 1.

csp_rom_crc16(code, n) ar delad (csp_rt.c): generatorn kor den over programmet
den emitterar, csp_load_rom kor den over baked rom_instr, och csp_eeprom
ateranvander den (rom_crc16-makrot) for att fingeravtrycka firmware en save
gjordes mot. Laser via ro_instr sa den ar korrekt bade pa host (plain) och AVR
(memcpy_P). Tom rom.c far falten ocksa (rom_n_decl==0 -> aldrig avvisad).

### Stack/arena-kollisionen -- roten till en tredagarsjakt (2026-07-23)

CSP_STACK_RESERVE 512 -> 2048. Detta EN rot bakom tre olika symptom som vi
jagade var for sig i tre dagar:
  - tomma /list-rader (stacken skrev over en nyss tillagd decl)
  - falsk "setup failed: out of memory" (csp_rebuild gick djupt vid boot och
    korrumperade SITT EGET estimat/mid-tillstand -> csp_mid_alloc failade; host
    far plats med cpx i 3000, arenan var 5616 -- den var aldrig for liten)
  - omstartsloopar (stacken nadde en returadress -> AVR reset)

Arenan vaxer RAM-deklarationer NEDAT fran sin topp; stacken vaxer NEDAT fran
RAMEND mot samma adress. 512 byte mellan dem racker inte: djupaste punkten
(push_imm, i uttrycksparsern) nadde 919 byte PAST reserven in i arenan. 2048
ger margin +617 med cpx.csp och lamnar arenan pa ~4080 (cpx behover <3000).

Verktyget som knackte det, och som stannar kvar:
  - /memory har en `margin`-rad: minsta avstandet stacken NAGONSIN kommit
    arenan (csp_stack_mark samplar de djupa punkterna -- csp_eval_rule,
    csp_parse, csp_parse_expr, pmatch). Negativt = redan overskrivet. Alltid
    pa, billig. Pa host "-" (8 MB stack, motet sker aldrig).
  - CSP_STACK_WATCH + -finstrument-functions lagger till "at 0xADDR": funktionen
    som natt djupast. make -f Makefile.mega watch bygger det; whichfn ADDR=0x..
    slar upp adressen (word-adress, *2). Bakom #ifdef, noll kostnad normalt.

Delfix pa vagen som ocksa stannar: csp_exprbuf_t (~423 byte) ar nu static i
csp_print.c i stallet for en stack-lokal i csp_print_rule -- flyttar spiken fran
stacken till .bss. Ensamt racker det inte (darav 2048), men det tar bort den
storsta enskilda djup-vagen.

Ovanpa detta: kortets loop() skippar nu csp_cycle nar started==0 (rebuild
failade) och hanterar serial FORST -- sa ett OOM-boot ger en SVARBAR prompt
(/memory, /clear) i stallet for en krasch mot NULL view/heap.

### RAM-decl-namn korrupta på AVR med ROM (2026-07-23)

Att lägga till en deklaration vid prompten på mega gav namn som skrevs ut som
HELA ROM-strängtabellen (`State␄INIT␆NORMAL...`). Host var blint.

Rot, mätt fram efter TRE fel-hypoteser (rom.c, PROGMEM, stacken -- alla
avfärdade av mätning): decl:ens `name`-fält var `STRING_BITS` brett, och
STRING_BITS är 7 på AVR (max 127). Men `name` håller en LOGISK strängposition,
`rom_strp + RAM-offset`. cpx.csp har 130 byte ROM-strängar, så den FÖRSTA
RAM-decl:ens namn landar på position 131 -> 7-bitarsfältet hugger av det till
skräp. Kopplingen name<->STRING_BITS var buggen: det ena är en positionsbredd,
det andra en RAM-buffertstorlek, de har inget med varandra att göra.

Nytt syns bara nu för att decl-tillägg på AVR var trasigt förut (PROGMEM-
dispatchbuggen, samma session) -- detta var första RAM-decl:en som listats.
Host har STRING_BITS=9, så 131 rymdes.

Fix: eget `NAMEPOS_BITS = 9` (0..511), frikopplat från STRING_BITS. Ryms i
DECL_COMMON:s två lediga bitar (30 av 32 använda), så csp_decl_t är KVAR på
8 byte på både host och AVR -- verifierat med avr-nm. `new_string` felar nu med
ERR_STRING_SPACE om en position inte får plats, i stället för att hugga av.
Kvarvarande gräns: rom_strp + 128 <= 511, alltså ROM-strängtabeller upp till
~380 byte.

`state_t.name` hade EXAKT samma bugg (också STRING_BITS-brett, också en logisk
position, satt från new_string) -- den slog till på #states tillagda vid
prompten på ett kort med ROM. Samma fix: name:NAMEPOS_BITS, och snum tar resten
av 16-bitarsordet (NUM_BITS = 16-NAMEPOS_BITS = 7 bitar, max 127 mot MAX_STATES
16). state_t stannar på 2 byte (AVR) / 4 byte (host, icke-PACKED i en unsigned).
new_string-vakten täcker states med, samma väg.

Diagnostiken byggd på vägen och behållen: `/memory` har en `margin`-rad som
mäter minsta avståndet stacken NÅGONSIN kommit arenans topp (negativt = stacken
har varit inne bland deklarationerna). csp_stack_mark samplar på de djupa
punkterna (csp_eval_rule, csp_parse, csp_parse_expr, pmatch). CSP_STACK_WATCH +
-finstrument-functions ger dessutom vilken funktion som satte rekordet. Den
underliggande stack/arena-kollisionen är en verklig latent fara (se TODO):
reserven 512 är en gissning, nollpunkten på mega ligger runt 1320.

### Stale EEPROM-save gav phantom-decls efter NAMEPOS-fixen (2026-07-23)

Direkt följd av NAMEPOS_BITS-ändringen. En färsk boot på mega (Wokwi behåller
EEPROM mellan omflashningar) visade två tomma R-rader i /list -- ps.nd=24 mot
rom_nd=22, med d22=DECL_END och d23=DECL_NONE, ren skräp. Host var blint
(autoload hoppas över när firmware är inlänkad; kortet kör csp_eeprom_load
ovillkorligt vid boot).

Två fel:
- NAMEPOS_BITS breddade decl:ens name-fält, vilket flyttar VARJE bitfält efter
  det i csp_decl_t. En v5-save är därmed binärt inkompatibel och lästes som
  skräp-decls. rom_crc fångar det INTE -- den är över ROM-INSTRUKTIONERNA, vars
  layout inte ändrades. EEPROM_VERSION 5 -> 6 avvisar nu en gammal save vid
  version-kollen, INNAN ps.* rörs.
- csp_eeprom_load lämnade ps.nd/nn/strp/ns UPPBLÅSTA när den failade efter
  header-läsningen (satta på "Logical counts"-raden, aldrig återställda på
  error-vägen). En misslyckad load måste lämna en REN ROM-baslinje, inte ROM +
  halvläst skräp. error: återställer nu ps.* till rom_*.

  Verifierat: /load av en handpatchad v5-save ger "cannot load", 22 decls (inte
  24), och en cykel efteråt kraschar inte (ASan).

### /load kraschade -- och maskerade felmeddelanden (2026-07-22)

`csp_eeprom_load` avslutade med `csp_rt_start(st)`. Skulle vara `csp_rebuild`:
bara rebuild kör `csp_mid_reset`, och `csp_rt_init` några rader ovanför har
just nollat `mid_base`/`mid_end`. Alltså returnerade VARJE `csp_mid_alloc`
NULL, och view/dset/buf/heap/input/output/timer blev alla nollpekare. Nästa
cykel dog i `csp_slot` (`st->heap[dir] + st->buf[v->buf].hp`).

Kommentaren i csp_arduino.c:s setup beskriver exakt fällan -- den var känd,
bara inte tillämpad här.

`csp_rt_start` UPPTÄCKTE det och returnerade -1 med
ERR_TOO_MANY_DECLARATIONS. Returvärdet slängdes, så `/load` svarade OK.

Boot-vägarna klarade sig för att main() och .ino:n anropar `csp_rebuild`
själva direkt efteråt. `/load` hade inget som reparerade.

Samma rad förklarade den andra TODO-punkten: det slängda felet låg kvar i
`ps.err`, och `csp_set_error` skriver inte över ett redan satt fel -- så efter
en autoladdad eeprom.db kom ERR_ALREADY_DEFINED aldrig fram och en
omdefiniering svarade "too many declarations". Båda TODO-posterna borta.

    /save /clear /load   -> programmet tillbaka, /list och /state korrekta
    autoload + omdef     -> "variable x is already defined"

Verifierat under ASan+UBSan, disable-setet överlever cykeln.

### #can -> #field (2026-07-22)

Frågan var om mekanismen behövs alls, nu när `#buffer F:8 out can 0x123` finns
och variabler kan packas/packas upp med `<<=` / `>>=`. Svaret: ja, men namnet
var fel.

MÄTT mot vcan0, samma tre framar (0x11, 0x12, 0x12 i byte 0, byte 1 alltid 0):

    #field A:8 in unsigned F[0..7]     ->  2 x "A rule", 0 x "B rule"
    A = F[0..7] ? F.rx  (variabel)     ->  2 x "A rule", 1 x "B rule"

Variabelversionen rapporterar en ändring på B som aldrig skedde på bussen. Det
är inte en bugg utan en följd av modellen: ett fält ÄR framens bitar och har
ingen egen existens att initialiseras, medan en variabel har det -- övergången
"aldrig skriven" -> "skriven med noll" är en ändring i variabeln men inte i
framen. Reproducerat två gånger, båda designerna.

Två mekaniska skäl därtill: `csp_commit` köar per DECL_FIELD-vy efter RX ("so
changed() has real granularity"), och variabler ligger inte i `st->input[]`, så
en frame med 20 signaler där en ändras ger 20 skrivningar i stället för en
väckning. Plus att unpack-raderna kräver `? F.rx` -- utan den har en ren
`=`-regel ingen trigger alls i reaktivt läge (därav att can_pack är seq-only).

ARGUMENTET SOM INTE HÖLL: tecken och endian. `make_buf_view` hårdkodar
V_UNSIGNED/E_NATIVE så pack inte ens KAN uttrycka ett signed fält -- men
`#field S:16 out integer` ger också 65236 för -300, så läsvägen struntar i vt.
Deklarationen bär typen, implementationen bryr sig inte. Lagt i TODO.

Namnet: ingenting i mekanismen är CAN-specifikt. Det är en namngiven, typad,
riktad bitvy in i en buffert -- lika rätt för Modbus-register, en SPI-frame
eller en packad struct i RAM. CAN-heten sitter helt i `#buffer ... can 0x123`.
Så `#can` -> `#field`, och internt DECL_CAN -> DECL_FIELD, V_CAN -> V_FIELD,
F_CAN -> F_FIELD, csp_parse_can -> csp_parse_field, pat_can -> pat_fdecl
(PAT_FIELD var upptaget av uttrycksmönstret `<obj>.<field>`), tag-tecknet
'k' -> 'f'.

`field` är INTE ett reserverat token, så `#field` går den vanliga WORD-vägen
via find_decl_entry -- dispatch-grenen som `#can` behövde (eftersom `can` är
reserverat som transportnyckelord på `#buffer`) kunde tas bort.

### #disable / #enable (2026-07-22)

`#disable <range>` / `#enable <range>`, range = `3 7-9` (blankseparerat).
Regel N = den N:te OP_RULE i instruktionsordning, 1-baserad. Varje regel
emitterar exakt en -- en oguardad får en alltid-sann LI först -- så serien har
inga hål. INTE samma som den reaktiva ordinalen från `number_rules`, som också
numrerar modulentréer och den implicita kroppen vid varje range-bas.

**En check täcker båda exekveringsvägarna.** `csp_eval_rule(st, n)` kör EN
regel; sekventiella loopen anropar den i följd och `csp_react` anropar den med
`rule_ip[ord]`. Båda rinner in i regelns OP_RULE, och `r.nxt`-hoppet som redan
hoppar över en falsk guards kropp är precis det hopp en disablad regel behöver:

    if (((st->dis_ip == NULL) || !bitset_tst(st->dis_ip, n)) &&
        st->reg[instr(st,n,r.cnd)].i)

Priset är att guarden fortfarande evalueras. Rena uttryck, så utan effekt.

**Nyckeln är OP_RULE:s egen adress, inte regelns första instruktion.**
Första försöket nycklade på villkorets start, som Tony ville. Den går bara att
härleda statiskt, och `csp_eval_rule` returnerar på FEM ställen: OP_NEXT,
OP_ENTER, OP_NEW, OP_LEAVE och OP_INSTATE när blocket hoppas över. Med bara
NEXT/ENTER som gränser hamnade biten på en OP_LEAVE, vars skip svalde de
matchande poparna -- `st->esp` sprang ur `stack[4]`, segfault på
cpx_m_color.csp (tio objekt). OP_RULE:s ip behöver ingen härledning.

**Två representationer:** `dis_rule` (regelnummer, på structen, sanningen,
16 byte) och `dis_ip` (OP_RULE-adresser, härledd i `csp_rebuild`, allokeras
bara när något faktiskt är disablat -- annars NULL och en test i heta loopen).
Numren lagras som de skrivs, så `#disable 5` överst i en fil funkar innan
regel 5 är parsad, och de re-resolvas efter varje edit.

**EEPROM (v5):** ETT bitset, skrivet i sin helhet -- `dis_rule` täcker redan
ROM och RAM, numren löper 1..r_rom genom ROM och vidare in i RAM. Nya
headerfält `rom_crc` (CRC-16/CCITT över ROM-instruktionerna) och `n_dis`.
Tre grindar med olika jobb: versionen svarar "förstår jag layouten", CRC:n
"är det samma program", n_dis "gäller numren fortfarande". CRC-miss avvisar
HELA sparningen (RAM-patcharna refererar ROM-decls per index); n_dis-miss
släpper bara overlayen och säger till.

Förkastat: disable-biten i OP_RULE-ordets sex lediga bitar. Gav fyra
representationer av samma faktum, och vinsten fanns inte -- `csp_eeprom_load`
avvisar redan hela sparningen vid ROM-mismatch, så fallet där RAM-disables
överlever medan ROM-disables kastas kan inte uppstå.

**Nummer utanför programmet.** Ett ensamt nummer är ett påstående om en
specifik regel, en range är ett svep. Så `#disable 7` på ett sexregelsprogram
är ett FEL (`no rule 7, the program has 6`) medan `#disable 1-40` klampas till
sista regeln och går igenom. Bara en range som BÖRJAR bortom slutet avvisas.
Konsekvensen, medvetet vald: `#disable` kan inte stå ovanför reglerna i en
källfil. Framåtreferensen köpte bara det fallet, och att räkna regler som
kommer längre ner är ändå bökigt -- raden läser bättre efter dem.

Listningen fick en kompakt kolumn samtidigt: `%3d R  `, `F` för flash/ROM,
`!` för disablad, tomt nummer för deklarationsrader. `[ROM]/[RAM]`-taggbuggen
överst i TODO satt i samma kodvandring -- taggen togs från body-start, som
bara nollställs vid NEXT/ENTER/LEAVE, så den låg kvar under `rom_nn` när
vandringen redan var i RAM. Tas nu från OP_RULE:s ip.

Mega: +22 byte RAM (16 för bitsetet). 48/48 på `make test` och `make san`.

### CAN-frames (2026-07-18)
En frame ÄR en buffert; dess fält är bit-vyer in i den. Därmed gör den
befintliga view-maskineriet all packning och uppackning -- ingen egen
CAN-kodning behövs.

Syntax: ramen deklareras, fälten binder mot den vid namn.
    #buffer F201:8 in can 0x201     // storlek i BYTES när transporten är can
    #can A:8  unsigned F201[0..7]   // fält ärver riktning från ramen
    #can B:16 unsigned F201[8..23]
    #buffer Fbig:64 out can 0x300   // CAN FD

- `setup_can` var tom; binder nu fältets vy mot ramens buffert. `ca.id` är
  #buffer-deklarationen, så ramen har namn, storlek (DLC), riktning och id på
  ett ställe i stället för att vara anonym och gissad.
- `#buffer` fick en egen decl-struct (`csp_bufdecl_t`) med nbits/transport/id.
  Storleken låg i DECL_COMMON.res -- 5 bitar med bits-1, så allt över 32 bitar
  trunkerades TYST (`#buffer Big:64` blev 4 byte). Nu är `bf.nbits` enda
  sanningen. OBS: setup_buffer delas med auto-bufferten en vanlig #variable
  får, och där ligger storleken fortfarande i res.
- `can` är nu ett reserverat ord (transport-option på #buffer), vilket kräver
  en egen dispatch-gren för `#can` -- samma fälla som `#in` redan hade.
- Dirty-flagga per buffert, satt i `heap_dset_copy` (den walken har redan
  slagit upp view -> buffert, så det är gratis) och rensad av `csp_can_output`.
- RX går via DOUT-skuggan, inte rakt in i DIN: DIN måste behålla föregående
  frame så `can_mark_fields` kan se VAD som ändrats. Utan det markerades
  ingenting dirty, `changed()` var alltid falsk och inga reaktiva regler
  köades. Ingen extra kopia behövs -- transaktionen håller redan den gamla.
- Följd av transaktionsmodellen, värd att komma ihåg: när `changed(X)` är sann
  ligger det nya värdet i DOUT och regler läser DIN, alltså ser man FÖRRA
  värdet i samma cykel. Gäller all device-input, och samma sak drabbar `<-`.
  Ett mellanled (`fresh = changed(A)`) fördröjs lika mycket som värdet och
  lägger dem i fas. Se TODO för den skarpa varianten (källa som ändras exakt
  en gång).
- `/list` renderade varken `#can` eller `#buffer` -- båda föll i `default:` och
  blev tomma `[RAM]`-rader. Round-trippar nu exakt.
- Drivrutinshookar: `csp_can_init/recv/send`. Linux: SocketCAN bakom
  `--can=IFACE`, annars stubbar så `#can` fortfarande parsar och kör torrt.
  Arduino: arduino-CAN-API bakom `CSP_HAS_CAN` (per bräde, i dess Makefile).
- Objektfältsvarianten av DECL_CAN i `csp_rt_start` använde `ix` (objektet) i
  stället för `fx` (fältet) och saknade `break`.
- PDO-formerna faller ut utan PDO-kod: RPDO = frame in -> fält ändras ->
  beroende regler köas. TPDO event = regel skriver fält -> commit -> skickas.
  TPDO cyklisk = `Frame.X = ... ? timeout(T)`.
- Ett program vars ENDA input är bussen dog direkt: utan timer är wait_ms
  NOTIMEOUT, inget ändras, och varje "finns det jobb kvar"-test sa nej ->
  loopen avslutade innan första framen hunnit komma. `csp_can_active()` håller
  loopen vid liv, och CAN-socketen ligger nu i poll-setet så en frame väcker
  den i stället för att den snurrar (mätt: 12 cykler på 4 s i tomgång).
- Frame-parts (`.rx`, `.tx`, `.id`). De läses av BUFFERTEN, inte ur en
  value-slot, så ett fält svarar för sin ram också: `V.id == F300.id`.
  - `.rx` sätts vid COMMIT, inte vid mottagning. Det är hela poängen: den blir
    sann i samma cykel som datan blir läsbar, så `? F201.rx` hamnar i fas.
    `changed(A)` är sann en cykel TIDIGARE, medan bytena ligger kvar i skuggan
    och fälten fortfarande har förra framen. Lever exakt en cykel.
    Gjorde `fresh`-kryckan i can_input.csp överflödig.
  - `.tx` är en skrivbar trigger som tvingar fram en sändning. Det är enda
    sättet att uttrycka en CYKLISK PDO: en oförändrad ram är inte dirty.
    Verifierat mot vcan0 -- samma oförändrade payload var 300 ms.
  - `.id` fanns i part-enumen men låg blockerad på just den här omskrivningen.
  - `.dlc` åt båda håll: skrivs för att skicka färre byte än ramens storlek
    (klampas till nbytes -- heapen rymmer inte mer), läses för att få veta hur
    många byte avsändaren faktiskt skickade. Verifierat mot vcan0:
      TX  F302.dlc = 3          ->  vcan0 302 [3] 42 00 00
      RX  cansend 303#AABBCC    ->  F303.dlc = 3,  V = 0xAA
          cansend 303#11..77    ->  F303.dlc = 7,  V = 0x11
  - OBS: buffert-parts är INTE DIN/DOUT-skuggade (de ligger på bufferten, inte
    i en value-slot), så `F.dlc` läses tillbaka direkt i samma cykel. Jämför
    `.period` på en timer, som ligger i value-sloten och ÄR skuggad. Se TODO.
  - Modell: `csp_timer_t.fired` var förlagan -- en edge-flagga för "hände den
    här cykeln" fanns redan i språket, CAN är bara en annan transport.
- All legacy CAN-hantering borttagen (~183 rader), se TODO.
- Test: tests/unit/can_pack -- fa/fb i byte 0/1, fw läser båda som 0x0201.
- Test: tests/unit/can_parts -- .id genom ram och fält, .rx=0 utan buss, .tx.
- Exempel: examples/can_input.csp -- monitorerar 0x201 och printar A/B/C.
- Exempel: examples/can_output.csp -- bygger 0x300 av variabler, visar både
  event-PDO (fältskrivning gör ramen dirty) och cyklisk PDO (.tx).
- Exempel: examples/can_pack.csp -- en CAN-ram UTAN en enda #can-deklaration.
  Ramen är bara en #buffer med can-transport, så allt som funkar på en buffert
  funkar på den: `<<=` pack, `>>=` unpack, `bind`. Poängen med att binda #can
  mot #buffer i stället för mot ett rått id: en frame slutade vara en särskild
  sorts sak och blev en buffert med en adress.
  Visar också skillnaden mellan de två vägarna in i en ram:
    bind  = ALIAS, korrekt redan i .rx-cykeln
    >>=   = KOPIA, läsbar först nästa cykel (skriv-nu-läs-sen, som allt annat)
  Och tre triggers med olika betydelse:
    ? In.rx        varje mottagen frame
    ? changed(cmd) bara när fältet faktiskt ändrades -- en frame som upprepar
                   sitt innehåll ger ingen utskrift. Verifierat: samma frame
                   skickad två gånger ger .rx-rader men INGEN changed-rad.
                   OBS triggern måste gå via en variabel, annars printas
                   föregående värde (changed() är sann en cykel för tidigt).
  can_mark_fields markerade bara #can-fält ur input-listan, så en ren
  #buffer-ram fick ingen leaf markerad => commit kopierade aldrig och RX-datan
  nådde aldrig den committade halvan. Nu markeras ramens egen leaf först
  (heap_dset_copy flyttar hela bufferten för vilken dirty leaf som helst),
  och per-fält-passet ligger kvar för changed()-granulariteten.
- VERIFIERAT MOT vcan0, båda riktningarna:
    TX  candump vcan0 ->  vcan0 200 [8] 01 02 00 00 00 00 00 00
    RX  cansend vcan0 123#3412FF00...  ->  seen=4660 (0x1234), got=255

### rochar/PROGMEM-svep + csp_print_error (2026-07-19)
Utgångspunkt: `rochar*` som printas med `csp_print_str` läser fel adressrymd på
AVR. Min första räkning sa 8+1 platser -- den var FEL på 5 av dem. `rochar` är
bara `const char`; strängen ligger i flash bara om DEFINITIONEN bär `RODATA`.

    csp_fmt_vtype/pindir   deklarerad rochar*  -> faktiskt PROGMEM (tabell m. RODATA)
    csp_format_error       deklarerad rochar*  -> faktiskt RAM (bara literaler)
    builtin_cmds .name/.help  deklarerad rochar*  -> faktiskt RAM (ingen RODATA)

Att "fixa" de fem RAM-fallen till rostr hade INFÖRT buggen (pgm_read_byte på en
RAM-adress). Faktiskt trasiga var 4: csp_fmt_vtype x3 + csp_fmt_pindir via
state_col.

- De 4 rättade. `state_col` tar `const char*`, vilket dolde att en PROGMEM-sträng
  skickades in -- både utskriften och längdvandringen var fel. Ny `state_rocol`
  som går via ro_byte i båda.
- Felsträngarna FLYTTADE till flash i stället för att bara rätta deklarationen:
  20 strängar som aldrig ändras låg i RAM. Mätt på mega:
  **3399 -> 2987 byte globals, 412 byte sparade** (41% -> 36%). CPX oförändrad
  (ARM, RODATA är tom).
  De ligger i **strings.tab** (`s_err_*`), inte som ad-hoc `static rochar` i
  csp_rt.c -- samma mekanism som alla andra RODATA-strängar. strtab tar "resten
  av raden" som literal, så mellanslag är inga problem.
  En sak fick lösas: ERR_NAME_TOO_LONG byggde sin text med
  `stringify(MAX_NAME_LEN)` vid kompilering, vilket inte går i en tabellfil.
  Gränsen skickas nu som ett andra argument i stället:
  "identifier name too long %d, max %d". Macrona ify/stringify är därmed
  oanvända (lämnade kvar, de kostar inget).
- `csp_print_error()`: light printf med %s/%d/%% som läser formatet via ro_byte.
  Behövdes eftersom embedded saknar stdio och den gamla vägen printade
  formatsträngen RÅ -- användaren fick se "variable %s is not declared".
  Verifierat: "variable undefinedname is not declared",
  "function max/5 does not exist", "variable A is already defined".
- decl_name() var ett EGET problem som rostr inte löser: samma funktion
  returnerar pekare i två adressrymder (flash för ROM-intervallet, RAM annars)
  och anroparen kan inte se vilken. Sex platser, varav tre DEREFADE pekaren
  (`state_strlen`, `!*nm`) och inte bara printade den. Nya `decl_name_len` och
  `decl_name_empty` går via csp_str_byte; utskrifterna via csp_print_str_at.
  `csp_set_err_arg_ix` KOPIERAR nu namnet till felområdet i stället för att peka
  på det -- då är varje err_arg garanterat en RAM-sträng, vilket både fprintf
  och csp_print_error kan lita på.
- builtin_cmds: namn och hjälptexter ligger nu också i strings.tab (s_cmd_*,
  s_h_*). Bara TEXTEN flyttades -- tabellen behåller sina FUNKTIONSPEKARE och
  ligger kvar i RAM, eftersom ro_ptr är pgm_read_word (16 bitar) och en
  funktionspekare på mega2560 (256K flash) inte får plats i 16 bitar. Samma
  fälla som den packade csp_func_t som HardFaultade förut.
- BUGG jag införde och ASan fångade: jag bytte `strncmp` mot `ro_memcmp` i
  csp_cmd_dispatch. memcmp stannar INTE vid NUL, så `/listing` (7 tecken)
  jämfördes byte för byte mot "list" (4) och läste 3 byte utanför arrayen.
  Ny `ro_strncmp` som stannar vid terminatorn, plus `ro_strlen`. Kör `make san`
  på sånt här -- det syntes inte i vanlig körning.

- `csp_print_lit()`: makro som lägger en STRÄNGLITERAL i flash på plats, i
  stället för att den hamnar i .data och kopieras till RAM vid boot. Samma idé
  som Arduinos F(). Block-scope static, ren C89 (ingen GNU statement-expression)
  och kompilerar som C++, vilket .ino kräver. 66 anropsplatser konverterade.
  Det ÄR ett statement, inte ett uttryck -- ett anrop stod i en `return` och
  fick skrivas om. Ternärer (`csp_print_str(x ? "on" : "off")`) rörs inte, de
  är inte literaler.
  Uppdelningen är nu: csp_print_lit för literaler, csp_print_rostr för en
  rochar* man redan håller, csp_print_str för riktiga char* (namn, argument).

MÄTT PÅ MEGA, hela svepet:
    före             3399 byte globals (41%)
    felsträngar      2987 (36%)   -412
    kommandotexter   2585 (31%)   -402
    literaler        1893 (23%)   -692
  Totalt 1506 byte RAM frigjort -- 44% av utgångsläget. .data gick från
  1642 till 950 byte. Flash växte ~1700 byte, vilket är hela poängen: en mega
  har 256K flash och 8K RAM. CPX oförändrad (ARM, RODATA är tom).

### Sista literalerna ur .data (2026-07-21)

Tony hade städat resten; kvar i .data på mega var `State`, `INIT`, `NORMAL`,
plus `"name"`, `"on"/"off"` och EEPROM-magicen.

- **`State`/`INIT`/`NORMAL`** ligger i strings.tab. Två sorters användning,
  två sorters lösning:
  - Som *deklarationsnamn* (`csp_new_decl`, `add_state`): `new_string` memcpy:ar
    texten in i strängtabellen och tar en `char*`, så flash-namnet måste till
    RAM först. `RO_TSTR(State, ros_State)` gör en tstr_t på stacken via
    `ro_strcpy`. Bufferten är 12 byte -- räcker för de interna namnen, därför
    är makrot inte en allmän konvertering.
  - Som *jämförelse* (`is_state_var` i disassemblern, `state_is_state_var` i
    /state): `csp_str_eq_ro` läser båda sidor byte för byte genom var sin
    segmentaccessor. Ingen kopia alls, och de anropas en gång per utskriven
    deklaration.
- **`decl_type_name`** returnerade en `tstr_t` som pekade på `s_variable` osv,
  och `csp_set_err_arg_tstr` memcpy:ade den -- fel adressrymd på AVR. Returnerar
  nu `rostring_t`; ny `csp_set_err_arg_rostr` kopierar byte för byte, av samma
  skäl som `csp_set_err_arg_ix` gör det: varje err_arg måste sluta som en vanlig
  RAM-sträng.
- **`csp_format_error` är nu static.** Formatet är bara meningsfullt för
  `csp_print_error`; att exportera det var vad som lockade fram fprintf-anropen.
- **`csp_linux` använder `csp_print_error`.** De tre platserna som gjorde
  `fprintf(stderr, (char*)csp_format_error(...), err_args...)` går via en lokal
  `print_error()` som växlar print-sinken till stderr. Fungerade på host bara
  för att RODATA är vanligt minne här; nu kan de två formaterarna inte glida
  isär. csp_arduino behövde inget -- den går via `csp_process_line`, som redan
  använde `csp_print_error`.
- **EEPROM-magicen** är en RODATA-array, läst/skriven med `ro_memcmp`/nya
  `ro_memcpy`. `/latch on|off` jämför med `ro_strcmp` mot strings.tab.

MÄTT PÅ MEGA: .data 554 -> 542 byte, globals 1485 (18%). `strings` på
.data-sektionen ger noll läsbar text kvar -- det som är kvar är
pmatch-mönstren, `builtin_cmds` funktionspekare, vtabeller och switch-tabeller.

### Manualen genomgången + tre fel Tony hittade (2026-07-18)
doc/manual_en.md läst från början till slut och uppdaterad (1184 -> ~1520 rader).
CAN-sektionen skrevs om helt (sa fortfarande "in development"), ettcykelsregeln
fick en egen del i exekveringsmodellen, /pause /resume /live dokumenterade,
sju saknade kommandoradsflaggor tillagda. Varje nytt kodexempel kört genom
parsern i stället för att bara se rätt ut.

Tre fel Tony hittade vid granskning, alla verifierade och rättade:

1. `.dir` fanns inte som part på en buffert. `csp_dio_get_part`s VIEW_HEAP-gren
   hanterade VAL/RX/TX/ID/DLC men lät DIR falla i default => 0, fast bufferten
   har en riktig riktning. Inkopplad nu (läs+skriv), fungerar för vanlig
   #buffer, för CAN-ram och för ett #can-fält som läser sin rams riktning.
   Test: can_parts (dout=2, din=1, dfld=2).

2. `Pin = number` som pin-tilldelning i modulinit var RENT FEL. Mätt:
     #B b1 Pin=7      -> b1.Pin  digital 0:0  = 1   (satte VÄRDET, 7 -> 1 bit)
     #B b2 Pin.pin=9  -> b2.Pin  digital 0:9  = 0   (satte pinnen)
   Manualen hade både en tabellrad och ett helt avsnitt byggt på den felaktiga
   formen. Rättat till `.pin`/`.port` med en uttrycklig varning. Exemplen i
   repot var oskyldiga -- deras `A=1` är #variable-fält, alltså korrekt.

3. "An `#in S` block is exactly sugar for adding `&& State == S`" stämmer inte
   längre. Blocket kompilerar till en GATE (OP_INSTATE) som hoppar över hela
   gruppen i ett steg, plus ett per-regel-test kvar för den reaktiva vägen.
   Omformulerat till mental modell + vad som faktiskt händer.

### #buffer i moduler (2026-07-18)
Per-objekt-uppsättningen i csp_rt_start hade case för CONSTANT/VARIABLE/TIMER/
DIGITAL/ANALOG/CAN men INTE för DECL_BUFFER -- den föll i `default: break`.
En `#buffer` inne i en modul parsade alltså utan klagomål och allokerades
aldrig. Estimatorn räknade den däremot (est_leaf kallas per medlem), så det
fanns reserverat men oanvänt utrymme -- inget minnesfel, bara en buffert som
inte fanns.

- Ny `parent_leaf()`: ca.id är förälderns DECL-index, inte en leaf. För en
  medlem i ett objekt ligger en förälder som är medlem i SAMMA modul i det
  objektets lagring, medan en global förälder inte gör det. Utan den
  distinktionen aliasar varje instans mot modulmallens slot.
  Används av både setup_variable (bound) och setup_can.
- Verifierat att båda grenarna stämmer: m1 packar 1/2 -> 0x0201, m2 packar
  9/8 -> 0x0809 (egna buffertar), medan en medlem bunden till en GLOBAL
  buffert ger samma värde i alla instanser.
- Test: tests/unit/module_buffer.

### Globaler synliga i modulkroppar (2026-07-18)
En modulkropp kunde inte se NÅGON global -- inte en konstant, inte en variabel,
inte en timer, digital, analog eller buffert. Mätt på alla sex typerna, alla
gav "variable K is not declared". `csp_lookup_decl` började söka vid
`INDEX(mdef)+1`, alltså i modulkroppen, och globalerna ligger före.

- Uppslagningen är nu tvådelad: modulkroppen först (så en medlem skuggar en
  global med samma namn), sedan globalerna i [0, mdef). `lookup_decl_in` hoppar
  redan över andra modulers kroppar, så deras medlemmar förblir privata.
- Fällan på anropssidan: fyra ställen wrappade villkorslöst i
  MAKE_INDEX(CURRENT, ...) när man var inne i en modul. En global som
  resolvades därifrån hade då blivit CURRENT-relativ och pekat in i objektets
  lagring. Ny `is_module_local()` avgör i stället.
- Regression jag införde och fångade: dubblettkontrollen vid DEKLARATION gick
  också via den nya uppslagningen, så en medlem kunde inte längre heta samma
  som en global. Delat i `csp_lookup_decl_local` (deklaration, bara egen scope)
  och `csp_lookup_decl` (referens, scope + globaler). Att lägga till en global
  får inte bryta en modul som råkar använda namnet.
- `/list` visade inte `bind` på en variabel -- utskriften gick inte att klistra
  tillbaka. Round-trippar nu: `#variable r integer bind Live[11..15]`.
- Test: tests/unit/module_globals -- två instanser läser globalerna, en tredje
  modul skuggar och måste få sitt eget värde.
- Exempel: examples/cpx_m_color.csp -- RGB565 som namngivna konstanter, plus en
  16-bitars buffert med bundna r/g/b som byggs av ljussensorn vid körning.
  Bufferten är global och läses av alla tio Pixel-objekten, vilket bara går
  tack vare den här fixen. cpx_m.csp lämnad orörd.

### AVGJORT: print läser DIN, inte DOUT (2026-07-18)
Frågan: borde `print` visa det nyss tilldelade värdet (DOUT) i stället för det
committade (DIN)? "Rule of least surprise", med `print(x) ? x < 10` som
exempel. Svar: nej, behåll DIN.

- Exemplet är redan rätt. `print(x) ? x < 10` printar alltid ett x under 10,
  eftersom guarden och argumentet läser SAMMA sida och därför inte kan säga
  emot varandra. Mätt: 0, 0, 3, 6, 9, 1.
- DOUT mitt i en cykel är inte "det nya värdet", det är "vad som råkat skrivas
  hittills". Två prints av samma variabel i olika regler skulle kunna visa
  olika värden beroende på regelordning => utskriften blir ordningsberoende,
  vilket är precis det transaktionsmodellen finns för att undvika (och det som
  bär "sista regeln vinner", alltså patchningen).
- Print skulle ljuga om vad reglerna gör. Mätt på ett program med
  print / x = 42 / print / y = x / print i samma cykel: med DIN säger alla tre
  raderna samma sak och y stämmer med det printade. Med DOUT hade rad 2 visat
  42 medan nästa regel räknade med 0.
- Det hade dessutom blivit en halv fix: den verkliga överraskningen är att
  ändringstriggers (changed(), .rx, `<-`) fyrar en cykel före värdet de
  beskriver. `y = x ? changed(x)` hade fortfarande "sett fel ut".

Om behovet av att se den väntande sidan blir verkligt hör det hemma i ett NAMN
(`x.out` / `pending(x)`), inte i print -- explicit på anropsstället och
användbart i alla uttryck. REPL:ens immediate-väg i /live är en annan sak och
kan göra som den vill utan att röra regelsemantiken.

### Benvärmare (2026-07-18)

- **Tillägg under drift.** `edited` sätts nu på ALLA add-vägar; `csp_cycle` gör
  en lat `csp_rebuild` i toppen av cykeln. Stängde luckan där en regel tillagd
  under drift aldrig fick några reaktiva kanter -- demonstrerad åt båda hållen
  (utan fix B=0, med fix B=1).
- **Rule counter.** `n_rule_emit` räknas vid emission i `alloc_instr_ptr` (INTE
  vid OP_RULE -- `number_rules` räknar rule *bodies*, avgränsade av
  OP_NEXT/OP_ENTER). `graph_rules` är snapshotet `csp_rebuild` tar. Oberoende
  staleness-signal vid sidan av `edited`.
- **Parse-rollback.** `csp_pstate_save/restore` var död kod; utökade till
  `csp_pmark_t` som även täcker parse-markörerna (`mdef`, `ent`, `sdef`,
  `in_marker`, `save_sx`, `sx`, `cur`) -- de ligger i `csp_rt_t`, inte i `ps`,
  och var precis de som överlevde ett fel. En mark tas vid `#module`, så ett
  fel innan `#end` rullar tillbaka HELA modulen ("Module aborted") i stället
  för att svälja alla följande rader.
  Mätt: per rad backar instruktionssidan redan av sig själv -- per-rad-rollback
  är billig försäkring, modultransaktionen var den verkliga vinsten.
- `test.sh` matar REPL:en via `<test>.csp.stdin`.

## 0.8 och tidigare

Se git-historiken; taggen 0.8 sammanfattar minnesarkitektur-omarbetningen
(dubbeländad kod-pool, mitten-allokator, claim av fritt RAM vid boot, kön
ersatt av ordinal-nycklade bitset, SAMD flash-EEPROM, /live, /pause).
