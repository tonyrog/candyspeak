# TODO - Klart-markerat flyttas till DONE.md, inte hit.

# SAVE/COPY IMAGE

/copy A B			-- B can not be running
/copy B EEPROM			-- eeprom will be erasse
/copy EEPROM B			-- save patches file as program B
/copy A  -- output as HEX?

/save        -- write RAM patches/program EEPROM
/save A	     -- write RAM program to partition A

/erase A     -- erase parition A
/clear /save -- erase EEPROM

# FAILSAFE-TRAPPAN

## FAILSAFE = ett #module (INSIKT, Tony 2026-07-26)
  Gör FAILSAFE till en `#module FAILSAFE` istället för ett `#in FAILSAFE`-block.
  En modul ÄR den självförsörjande enheten: egna decls, egen kod (ENTER..LEAVE),
  egna states, egen #in INIT, per-instans-lagring.
  (FAILSAFE INIT state måste köras vid fail, pinnar kan behöva definieras om!)
  GRATIS:
  - FAILSAFE_INIT = modulens #in INIT (starta timer där). Ingen entry-flagga.
  - Självdelimiterande: DECL_MODULE.n + OP_ENTER.num avgränsar redan modulens
    range -> inbyggda "END-markörer" för modulen.
  - "Segmentet" = modulen (kontiguös). crc_failsafe = CRC över modulens decl-
    range + instr-range + refererade strängar (modul-SLICE-crc).
  - Flera kopior / EEPROM-patch = re-instansiera / module-patch.

  FÖRFINING (Tony 2026-07-26): FAILSAFE = en ANDRA ROM-IMAGE.
  - Takeover = rikta om bas-pekarna (rom_decl_p/instr_p/str_p + counts) mot
    FAILSAFE-skivan = EXAKT vad csp_load_rom redan gör (rebasar runtime på ROM).
    Så FAILSAFE-segmentet blir en självständig image: egen rom_fs_header + str/
    decl/instr + self-verify-trailrar. "Växla" = csp_load_rom(FAILSAFE) + rebuild.
    NOLL eval-loop-ändringar. ALL ROM-maskineri (verify/recovery/version)
    återanvänds rekursivt.
  - Implicit SINGLETON: instansieras ej som objekt; runtime REBASAR på den (som
    på ROM). Dess #in INIT kör setup.
  - CONSTRAINT (möjliggöraren): FAILSAFE får INTE referera globala/andra-objekt-
    fält -> kompileringstidsfel. Rebasen är giltig bara om skivan är
    självförsörjande.
  - FAILSAFE re-deklarerar sina egna pinnar (egen decl, samma fysiska pinne;
    main + FAILSAFE kör aldrig samtidigt). Pin-konflikt-checken måste tillåta det
    / inte cross-checka (FAILSAFE kompileras som egen enhet).

  ARBETSORDNING:
  1. RUNTIME-REBAS-PRIMITIV -- se grupp 2 ovan. Gör denna först.
  2. AKTIVERINGSMODELL: main kör normalt, FAILSAFE vilande/redo; vid fel/Panic/
     watchdog -> växla (repoint + rebuild). Boot: main failar verify -> boota
     FAILSAFE.
  3. LOKALISERA FAILSAFE vid korruption: namnet ligger i str. Reserverad modul-
     markör/flagga på DECL_MODULE (eller: FAILSAFE är en egen image -> hittas via
     sin egen rom_fs_header, ingen namnmatchning behövs).
  4. KOMPILERING (kan vänta): `csp -C` riktad på BARA `#module FAILSAFE` ->
     `rom_failsafe.c` (egen strängtabell, egna decl-index, eget pin-space).
     Återanvänder generatorn. Syntax/semantik oförändrad. `--prefix`/`--role`/
     `--generation` finns redan.
  KONSEKVENS: crc_failsafe, FAILSAFE_INIT, eget segment -> allt kollapsar till
  "FAILSAFE = andra ROM-image + rebas-primitiv". str+state self-verify KLART.

## Trappan: FAILSAFE som recovery-target (2026-07-26)
  MÅL: vid korrupt ROM (header/kod/decls) hoppa till en verifierad FAILSAFE
  istället för dead/park. FAILSAFE måste kunna köra ISOLERAT -- den behöver sin
  EGEN skiva av instr + decls + str (inte bara koden).

  INSIKT (Tony): om str-arean är OK kan FAILSAFE PRINTA en diagnostik ("FAILSAFE:
  sensor X fault") -- slår en tyst blink. Ger en extra degraderingspinne: str OK
  men kod korrupt -> skriv ut vad du kan innan park (Erlang: logga + reboota).

    header OK, sektioner OK               -> kör normalt
    header-crc rutten, END-markörer OK    -> kör normalt              KLART
    sektion korrupt, FAILSAFE-seg OK      -> hoppa FAILSAFE           (kräver segment)
    ROM-FAILSAFE korrupt, EEPROM-patch OK -> den                      (framtid)
    allt korrupt                          -> park (UART + watchdog)

  Steg 1 (per-sektion self-verify, alla fyra sektionerna) är KLART, se DONE
  2026-07-26. Steg 2 = modul-slice-CRC (modulen är självdelimiterande, inga nya
  markörer). Steg 3 struket (modulens egna #in INIT tar det). Steg 4 = takeover +
  lokaliseringsmarkör. Steg 5 = EEPROM-FAILSAFE-patch + park-fallback.


# 4. BEKVÄMLIGHET

## `#every <timer>`-block i stället för 24 gånger `? timeout(T)` (Tony 2026-08-11)
  FRÅGAN som ledde hit: borde `timeout(T)` vara en instruktion, som `changed`?
  MÄTT FÖRST: `changed()` är INTE en instruktion -- den kompilerar till exakt
  samma `LI + ARG + CALL` som timeout. OP_CHG finns men emitteras bara på den
  reaktiva `<-`-vägen. Så båda är anrop: 3 instruktioner, 12 byte per användning.
  examples/cpx_ball_array.csp har 24 stycken = 288 byte kod och 1200
  builtin-dispatchar per sekund vid 50 Hz.

  SOM OPCODE: ryms precis i csp_instr_mem_t (op 6 + x 4 + mem 16), alltså 4 byte
  i stället för 12 -- ~192 byte sparat i den filen. MEN det finns bara TRE
  opcodes kvar före OP_END_MARK (OP_AVAIL=60, END_MARK=63), och en per builtin
  är dyrt.

  BÄTTRE, och det Tony egentligen bad om ("undvika upprepning"): ett BLOCK.
    #every Tick
      Acc = ...
      Vel = ...
    #end
  Samma form som `#in <state>`: OP_INSTATE gatear ett helt block med en patchad
  hopplängd, och den maskinen finns. 24 villkor blir ETT, och källan blir
  läsbarare. De två komponerar, men blocket dominerar -- ta det först och lägg
  bara till OP_TIMEOUT om anropen fortfarande syns i en mätning efteråt.

## /compact
  RAM-regler kan komprimeras när `R` eller `E` är avstängda (`!`): reglerna tas
  bort helt. Sparas det efteråt är de permanent borta. Taggarna F/E/R och `!` i
  `/list` är klara (se DONE 2026-07-31) -- de säger redan exakt vad man förlorar,
  vilket är förutsättningen för att våga köra en compact.

## Stack/arena-marginalen: varna, inte bara visa (2026-07-23)
  `CSP_STACK_RESERVE` är 2048 och `margin` är +617 med cpx.csp (se DONE).
  Kvar som förbättring, inte bugg:
  - Låt `/memory` VARNA när margin kryper under t.ex. 256 -- en rad
    "WARNING: stack near arena" så man ser det utan att läsa siffran.
  - 2048 är mätt mot cpx.csp + korta REPL-rader. Ett program med moduler eller
    djupt nästlade uttryck kan gå djupare. Går margin negativt på ett riktigt
    program: höj, eller krymp djupvägen. Nästa kandidat är de två nästlade
    `token_t tv[24]` i `csp_process_line` + `csp_parse` (~288 byte) -- raden är
    redan tokeniserad en gång, andra passet är redundant.


# 5. VERIFIERINGSSKULD

Inte buggar -- saker som byggts men inte setts fungera på järn.

- XOFF/XON (`serial_hold`/`serial_release`) syns bara på ett kort med RIKTIG
  UART. Över USB CDC blockeras värden av endpoint-backtrycket oavsett. Testa på
  mega: klistra in en längre fil i minicom med software flow control av och på.
- arduino-CAN:s globala `CAN` (SAMD/ESP32 on-die) är oprövad. MCP2515-vägen på
  rp2040 är körd mot riktig buss i tio timmar.


# DOKUMENTERADE KONSEKVENSER (inte åtgärder)

## csp_dio_get_part/set_part är stora, och den enkla fixen kostar mer (2026-07-31)
  1 012 + 848 = 1 860 byte på AVR, ~5 % av en exec-only-bild. Mätt och
  undersökt; slutsatsen är att låta dem vara.

  VAR BYTESEN LIGGER: `csp_dio_get_part` är 489 instruktioner men bara 8 anrop
  -- och **80 `pop` mot 16 `push`**. Funktionen är stor nog att gcc sparar undan
  halva registerbanken, och VARJE return-väg (inklusive den tidiga heap-grenen)
  bär en full återställning. Det är prolog/epilog-duplicering, inte
  switch-logiken. De små hjälparna (`csp_dio_get_pin_part` m.fl.) är 46-82 byte
  styck och är inte problemet.

  PROVAT OCH FÖRKASTAT: dela heap-grenen och slot-grenen i egna NOINLINE-
  funktioner, så var och en får liten registerbudget och en epilog. Halvorna
  krympte som väntat (1 012 -> 372 + 520) men TOTALEN blev 132 byte STÖRRE:
  omslagsfunktionen inlinades vid varje anropsställe och duplicerade
  vy-uppslaget i stället. `noinline` på omslaget ändrade ingenting. Återställt.

  DET SOM SKULLE FUNGERA, och varför det inte gjordes: ta bort switcharna helt
  med tabelldriven dispatch -- (offset, skift, bredd) per (del, cvt). Men
  delarna är BITFÄLT i olika union-armar av `value_t`, så offsets går inte att
  ta adressen till; tabellen skulle bli handskriven. Det är exakt samma
  buggklass som emitter/CRC-normaliseringen i grupp 1: en tabell som tyst måste
  följa med när en struct ändras, och som felar som korrupt data i stället för
  som ett kompileringsfel. Inte värt 1,8 kB.

  Om någon återkommer hit: mät FÖRST att posten fortfarande är stor, och läs
  `pop`-antalet -- det är den siffran som säger om det är dispatch eller
  registertryck.


## En #constant listas som sitt VÄRDE, inte sitt namn
  `println(A, " ", B)` med `#constant B string = "World"` listas som
  `println(A," ","World")`. Konstanten viks bort vid parsning: `A = B` och
  `A = "World"` ger BYTE-IDENTISK kod (`OP_LI .imm=<strängposition>`), och för
  strängar delar de dessutom position eftersom `lookup_string` deduplicerar.
  Namnet finns alltså inte i instruktionsströmmen att hitta.

  En omvänd uppslagning vore en GISSNING som döper om äkta literaler:
  `println("World")` skulle listas som `println(B)` så fort någon deklarerat en
  konstant med det värdet, och `N = 5` som `N = Five`. Sämre än att tappa namnet.

  Att göra det på riktigt = inte vika konstanter (en instruktion + en
  minnesläsning per referens, plus ROM-storlek) eller en sidotabell över
  källform. AVGJORT (Tony 2026-07-31): inte värt det. Listningen är korrekt och
  klistras tillbaka med samma betydelse; bara namnet är borta, och namnet fanns
  aldrig i koden.

## `<-` och changed() fastnar på FÖRE-värdet när källan ändras exakt en gång
  De fyrar på ändring, men regler läser den committade sidan -- alltså värdet
  från före den ändring de fyrade på.
    #variable fa = 0
    #variable ra = 0
    fa = 1
    ra <- fa          // ra = 0. För alltid. I BÅDA lägena.
  fa ändras 0->1 i cykel 1, `<-` fyrar där och läser DIN som ännu är 0. Sedan
  ändras fa aldrig mer => `<-` fyrar aldrig igen.
  Med en källa som ändras LÖPANDE syns samma sak bara som en cykels
  eftersläpning, vilket är transaktionsmodellen och helt ok:
    fa=10 -> ra=9  (både `ra <- fa` och sekventiellt `rb = fa`)
  Samma rot som CAN-monitorns problem: `println(A) ? changed(A)` printar förra
  framen. Workaround som fungerar: lägg triggern i en variabel
  (`fresh = changed(A)`) -- den fördröjs lika mycket som värdet och hamnar i fas.
  Se examples/can_input.csp.

  AVGJORT (Tony, 2026-07-18): det här är INTE en bugg och ska inte "fixas".
  Semantiken är att input är input, och att man inte läser output förrän nästa
  cykel. Att `<-` fyrar på ändringen och läser förra cykelns värde är den regeln
  tillämpad konsekvent, inte ett undantag från den. Att låta `<-` läsa DOUT vore
  att böja semantiken för att ett testfall ska se snyggare ut.
  Står kvar som DOKUMENTERAD konsekvens: engångsfallet är det som förvånar folk,
  och `fresh = changed(A)` är mönstret som löser det.
  OBS: att `rb = fa` (utan `?` och utan `<-`) inte fyrar reaktivt är av samma
  skäl korrekt -- det reaktiva ligger bakom `?`, och `X <- Expr ? Cond` tar med
  variabler i både Expr och Cond i kanterna. `tests/unit/can_pack` är seq-only av
  just det skälet: den använder vanliga `=`-regler.


# COOL STUFF

## POKE-PROPAGERING + REGEL-TRACE (debug-verktyg, drömt fram 2026-07-17)
  Idé: i /live-läge, poka ett värde och kör BARA de regler som beror på det --
  inget annat. Motorn gör redan 90%: en manuell tilldelning skulle anropa
  `csp_enq_elist(ix)` (köar beroende regler i pending-bitsetet) följt av EN
  `csp_react(st)` (drainar och kör dem + deras kaskad). Kräver att
  immediate-assign (`csp_process_immediate` / `csp_dio_set`-vägen i live) routas
  via enqueue i stället för att bara sätta värdet.
  Användningsfall (Tonys): en regel du trodde skulle fyra gör inte det (inget
  pling). Du tittar: `Led = 1 ? BtnA && X > 7`. Du kollar X, sätter `> X = 8`,
  och ser om regeln fyrar nu. Interaktiv triggerfelsökning.

  TRACE ovanpå -- fristående, GÖR FÖRST, den är nyttig i vanligt körläge också:
  `/trace on|off`. `csp_react` dequeuear regel-ordinal -> `rule_ip` -> kör; där,
  bakom flaggan, printa vilken regel som fyrar. Billig variant: regelindex/ip.
  Snygg variant: kör exprbuf-disassemblern (funkar nu) -> full regeltext.

  ÄRLIGA BEGRÄNSNINGAR: (1) timers/`timeout(T)` fyrar INTE av en poke -- triggern
  är timern, inte ett värde; behöver riktig tid (`csp_input_timer`). Poke når
  allt som hänger på VÄRDEN, inte det som väntar på TID. (2) states/#in gate:ar
  rätt (en regel bakom State==ON fyrar bara om staten matchar) -- funkar, men man
  styr staten genom att poka State också.
  Hooks finns: `csp_enq_elist`, `csp_react`, `rule_ip`, exprbuf, `st->live`.

## Array notation (PÅBÖRJAD 2026-08-11 -- se DONE för det som är klart)
  OBJEKT-rutten nedan är ÖVERSPELAD (Tony 2026-08-11). Den byggdes inte, för två
  fynd vid genomgången gjorde en billigare mekanik möjlig:
  - `st_index()` indexerar `view[]`, inte deklarationer. Deklarationen läses på
    `INDEX(ix)` ensamt (`leaf_cfg_vt`), utan `cbase`.
  - Pinnen bor i LAGRINGEN, inte i deklarationen: `setup_analog` kopierar `d.an`
    in i DIN/DOUT-sloten per instans.
  Alltså: `OP_SETOX` sätter `cbase = offs[cur] + reg*stride` -- en ELEMENTOFFSET,
  inget objektnummer. Noll DECL_OBJECT, noll offs[]-poster, noll object[]-poster
  per element. Arrayen upptar en DEKLARATION per element (view[] indexeras av
  deklarationsindex, så element kan inte dela en); huvudet bär namnet, svansen
  har `cont=1` och namn 0. Längden återfinns med en scan (`csp_array_len`).

  KLART OCH TESTAT: `A[uttryck]` LÄSNING, och skrivning med KONSTANT index.
  `[` är en MARKÖR på operatorstacken (`IS_ARR_MARKER`, samma form som
  `IS_FUNC_MARKER`) -- inte ett rekursivt `csp_parse_expr`, för `csp_stack_mark()`
  sitter i just den funktionen med noteringen att marginalen bottnar där på AVR.
  Markörens uint32 bär selektorn i bit 31 och declindex i 16..30, så ingen
  sidostack behövs. Ett KONSTANT index viks till elementets egen deklaration:
  noll instruktioner, bounds-check vid kompilering, och immediate-läge funkar av
  samma anledning. Ett RUNTIME-index blir SETOX.
  Tester: tests/unit/array_index + fem fall under "arrays:" i tests/repl.sh.

  KLART OCH TESTAT: `A[uttryck] = rhs` -- SKRIVNING med runtime-index, och
  `#constant CT[10] = { ... }` med init-listor.
  Vänstersidan av en regelkropp matchas av `pat_body`, inte av uttrycksparsern,
  så skrivning är en EGEN väg. Den fick ett ANDRA optionellt block för
  `'[' <uttryck> ']'`, INTE ett `P_CHOICE` -- det första blocket backar ändå
  komplett (inklusive `[`) när `P_INTEGER_S` möter något som inte är konstant,
  vilket är precis det som förut lät `A[I] = 99` falla igenom som r-värde.
  `Buf[0..3]` ligger därmed kvar på sin gamla väg. Armeringen sker EFTER att
  högersidan laddats -- dess egen LD går också genom `asm_seto` och skulle annars
  konsumera engångsflaggan.
  `safe.A[i]` (array i ett NAMNGIVET objekt) avvisas medvetet: det skulle kräva
  OP_SETO och OP_SETOX samtidigt, och båda konsumeras av samma åtkomst.

  RÄTTELSE till en tidigare anteckning här: de 26 orelaterade sviten-felen berodde
  INTE på `collect_first`/`P_ALT` och inte på stop-tabellens tak. De berodde på
  att jag räknade det INRE blockets bytelängd men aldrig det OMSLUTANDE `P_OPT`
  som innehåller det. `pat_body` bär nu sin uträkning i en kommentar.
  Fällan är generell och värd att minnas: en fel längd i mönstret felar inte där
  den står -- parsern hoppar till fel ställe och det syns som ett par dussin
  syntaxfel på helt andra rader.

  KLART: `#analog`/`#digital A[N]` med pinnlista (`9:0..9`, `0:1..3,7,9`, eller
  en blandning). Möjligt bara för att PINNEN bor i per-element-LAGRINGEN, seedad
  från deklarationen av setup_digital/setup_analog -- tio element som delar en
  deklaration driver ändå tio olika utgångar. Fel antal pinnar mot längden är ett
  FEL, inte tyst fyllning: extra element hade annars pekat på pin 0, som är en
  riktig pinne på varje kort här.

  examples/cpx_ball_array.csp KOMPILERAR OCH KÖR -- 50 regler blev 7.

  KLART (2026-08-13): BÅDA handskrivna scannrarna flyttade in i pmatch, på Tonys
  fråga "borde man inte skriva detta i pmatch?".
  - INIT-LISTAN: `pat_initval` = `P_CONST_S` + separator (`,` eller `}`) fångad
    med `P_TOK_W`. `{` är nu ett ALTERNATIV i pat_constant, så tokenvektorn
    muteras inte alls längre (minus-hacket och `{`-borttagningen är borta). Ger
    gratis: konstant-UTTRYCK (`{ 1+2, MAX/2 }`) och STRÄNGAR (med `string`,
    samma regel som en skalär strängkonstant). Listan gås igenom TVÅ gånger --
    en räknande och en skrivande -- vilket är vad som gör längden känd innan
    deklarationerna finns UTAN en fast maxgräns på antalet element.
  - PINNSPECEN: `pat_pin_item`, ett item per pmatch-anrop. Ny syntax: FLERA
    PORTAR (`1:1..3,2:1,3,5,9:,7..9`) och en port som står ensam (`9:`).
    Per-element-port fungerade redan i runtime -- setup_analog/setup_digital
    kopierar `port` till lagringen precis som `pin`.

  TRE BUGGAR som föll ut av omskrivningen:
  - `0:1,4,7` (ren pinnLISTA, utan `..`) har ALDRIG fungerat, trots att den stod
    i manualen. Läst som port var stop-seten för det ledande talet `:` ensamt,
    så scanningen sprang till nästa kolon på raden -- eller av slutet -- och vek
    ihop hela listan till ETT tal. Bara formerna som börjar med `..` gick fram.
    Fixat i två steg: `pat_port_pin` matchar nu ett efterföljande `,` (det är
    DET som får COMMA in i pinnens stop-set), och pat_pin_item läser ETT tal som
    blir port eller pinne beroende på om ett `:` följer -- inte två alternativ
    som var för sig börjar med att läsa ett tal.
  - `process_op` läste UNDER operandstacken när konstantfoldaren fick en position
    som inte är ett uttryck (`,4`). Ett mönster som provar ett alternativ gör
    precis det, och `num = (k > ti) ? k - ti : 1` lämnar över en token när
    stop-token står först. ASan fångade det; en arity-koll överst i process_op
    fixar det för ALLA anropare.
  - `#analog` listade aldrig sin TYP. Med signed som ny default betyder det att
    `out unsigned 9:0` kom tillbaka som signed -- allt över halva skalan
    negativt. Listas nu när vt != V_INTEGER.

  SIDOFYND, EJ ÅTGÄRDAT: unärt minus på en KONSTANT viks inte.
  `#constant B = -N` är syntaxfel medan `#constant B = 0-N` och `#constant B = -5`
  går bra -- `-` framför ett tal fälls in i literalen av tokenisern, framför ett
  NAMN gör den inget. Gäller överallt konstantfoldaren körs, inte bara i
  init-listor (där jag hittade det). Liten fix i uttrycksparsern, värd att göra
  före release: formen är precis vad man skriver i en koefficienttabell.

  KVAR AV ARRAYERNA:
  - REAKTIVT: grafen har en kant per DEKLARATION och en indexerad läsning tar
    ingen kant alls (rentry:n byggs direkt i RB-fallet, förbi add_var). Alla
    array-program hittills är timer-gatade så det biter inte, men `A[I] <- ...`
    väcks inte. Antag kant till hela arrayen (grovt men korrekt) eller enq per
    element.
  - BROADCAST `P = 0` som bekvämlighet. Behövs inte av cpx_ball_array (den
    släcker bara de pixlar bollen lämnat, se filen) men läser bättre än fyra
    släckregler.
  - `safe.A[i]` -- array i ett NAMNGIVET objekt. Avvisas medvetet.
  - stride > 1 (array av MODULER). Fältet finns i OP_SETOX och är alltid 1 idag.

  SIDOFYND, åtgärdat: ett RUNTIME-fel sattes men rapporterades aldrig -- det finns
  inget kommando som väntar inne i eval-loopen. En bounds-check ingen ser är ingen
  bounds-check. Host-loopen i csp_linux.c skriver nu ut och NOLLSTÄLLER felet per
  cykel (regeln fyrar varje cykel; ett dåligt index får inte bli en ström i 50 Hz).
  Arduino-loopen har INTE fått motsvarande -- gör det när samma väg behövs där.

  GENERALISERING till #digital/#analog/#constant -- planera INNAN nästa typ
  läggs till, annars kopieras splitsningen fyra gånger (Tony 2026-08-11).
  Två delade hjälpare, båda typoberoende:
  - `array_splice()` -- plocka ut `[N]` ur tokenvektorn FÖRE pmatch, så resten av
    deklarationsgrammatiken är oförändrad. En array skiljer sig bara i HUR MÅNGA
    deklarationer den gör, aldrig i vad de säger.
  - `array_replicate()` -- kopiera det färdigbyggda huvudet N-1 gånger,
    kontinuerligt från `i+1`, så varje parser kan peta sitt eget per-element-fält
    med `ram_decl_at(st, i+k)` i en loop. INGEN funktionspekare: tre rader per typ
    slår en callback-apparat, och är billigare på AVR.
  Replikeringen kan alltså inte förbli en ren kopia:
  - `#analog P[10] out 9:0..9` -- en PINNE per element. Och TODO:ns eget exempel
    är en LISTA (`0:1..5,7,9,13,15,17`), vilket är precis vad `P_ARRAY` i pmatch
    finns för.
  - `#constant CT[10] = {...}` -- ett VÄRDE per element.

  KONSTANTER ÄR INTE KOSMETIK. En `#constant` VIKS BORT vid referensen -- `A = B`
  och `A = 5` ger byte-identisk kod (se "En #constant listas som sitt VÄRDE").
  `CT[Idx]` KAN INTE vikas: vilket element som läses avgörs vid körning.
  Lagringen finns redan (`setup_decl` allokerar en slot åt DECL_CONSTANT), så det
  som krävs är att den INDEXERADE referensen hoppar över foldningen och lägger ut
  en LD. En gren att planera in, inte en efterhandsfix -- och det är just den
  konstruktionen examples/cpx_ball.csp behöver för sina cos/sin-tabeller.

  Två beslut att ta när det byggs:
  - Får `[N]` utelämnas när en lista anger längden? (`#digital D[] in 0:1..5,7`)
    STATUS: `#constant A = { 1,2,3 }` (utan hakparenteser alls) sätter längden
    från listan. Tomma `[]` är INTE implementerat -- array_splice kräver
    `[ INT ]`. Värt att lägga till: `A[]` säger på deklarationen att det ÄR en
    array, vilket `A = {...}` inte gör.
  - `#analog P[10] out 9:0..3` (tio element, fyra pinnar) ska vara ett FEL, inte
    en tyst nollfyllning.

  DET REAKTIVA är fortfarande olöst: grafen har en kant per DEKLARATION, och
  `Acc[INDEX]` beror på ALLA element (vilket som läses avgörs vid körning).
  Antag antingen kant till hela arrayen (grovt men korrekt) eller enq per element.
  Biter inte i examples/cpx_ball.csp -- varje regel där är gatead på
  `timeout(Tick)`, så inget i programmet är reaktivt. Bra första mål av det
  skälet: arrayer kan bevisas fungera utan att grafen rörs.

  ÖVERSPELAT (kvar för spårbarhet): "Kan återanvända OBJEKT-kodningen -- ett
  arrayelement är en objektinstans med en medlem, runtime-index = välja objekt
  vid körning." Kostade en DECL_OBJECT + en offs[]-post + en object[]-post per
  element, ~120 byte för P[10] på ett kort med 2675 byte kvar.

  #digital D[5] in 0:1..5,7,9,13,15,17
  #variable Acc[3]
  #analog A[3]:10 in 0:1..3
  #variable INDEX = 0
  #timer Td 1000

  Acc[INDEX] <- Acc[INDEX] + A[INDEX]
  INDEX <- (INDEX + 1) % 3 ? timeout(Td)

  Semantik (expanderad):
   #digital D0 in 0:1 ... D4 in 0:5
   #variable Acc0 Acc1 Acc2
   #analog A0 in 0:1 ... A2 in 0:3
   Acc0 <- Acc0 + A0 ? Index==0
   Acc1 <- Acc1 + A1 ? Index==1
   Acc2 <- Acc2 + A2 ? Index==2
   Index <- (Index + 1) ? timeout(Td)

## Interrupt
  BYGGT: trigger som OPTION på pin-deklarationen, se doc/EVENTS.md.

  #digital Drdy in falling 2:13
  #digital Btn  in pullup rising 2:7
  Sample = Imu ? Drdy.fired

  Ett avbrott är en egenskap hos hur pinnen är konfigurerad -- samma sorts sak
  som `pullup`, och det finns ingen `#pullup`-deklaration av samma skäl. Det
  fanns en `#event`-deklaration först; den kostade ett nyckelord, tva monster,
  tva felkoder och en extra listningsrad for att saga vad ett optionsord sager.

  Backends: host (samplad -- testbar med -F) och STM32 EXTI. Ovriga portar
  lankar de svaga defaultarna, sa programmet kompilerar och kor; kallan armas
  aldrig och /state satter `!` efter triggern sa tystnaden syns.

  KVAR:
  - LPC2000 EINT (EXTINT/EXTMODE/EXTPOLAR + VIC-kanal 14). bridgezone P0.16 ar
    motivet -- AVR:en vacker LPC:n ur power-down. Medvetet inte byggt: att
    vacka en nedslackt LPC ar errata-territorium och vill lasas pa forst.
  - Arduino attachInterrupt. Litet; inget arduino-kort har en kalla an.
  - `ready` som nagot annat an ett nej. Finns i grammatiken, ingen backend.
    Pa en #buffer ar samma sak redan `.rx`.

## #when <condition> ... #end
  BYGGT. Se doc/EVENTS.md.

  #when Drdy.fired && A < 100
    X = f1
    Y = f2
  #end

  `#when`, INTE `#in` -- `#in <state>+` ar statemaskinens syntax och lases
  battre om den far behalla ordet (Tony 2026-09-07). Kompilerar till villkoret
  plus EN OP_NINSTATE mot noll: samma opcode som `#in`, annat immediate, nxt
  patchad forbi blocket vid `#end`. Ingen ny opcode, ingen ROM-formatandring.

  MATT vinst pa fyra regler:
    ? Drdy.fired              1 instruktion per regel
    ? timeout(T)              1 instruktion per regel
    ? Drdy.fired && A < 100   5 per regel -- 81 utskrivet, 67 som block, och
                              villkoret utvarderas EN gang per cykel i stallet
                              for fyra
  Alltsa: blocket lonar sig pa sammansatta villkor och knappt alls pa en enkel
  del. Vart att veta innan man tar till ett.

  FIXAT PA VAGEN: ett oavslutat block snurrade. Skip-distansen patchas vid
  `#end`, och i REPL:en star blocket oppet medan man skriver -- med cykeln
  igang. Distans 0 ar ett hopp till gaten sjalv. Bada gate-opcodes behandlar nu
  nxt == 0 som "slutet pa strommen". Gallde `#in` lika mycket.

  NASTLING: `#in`, `#when` och `#module` delar EN stack, fyra djup, sa `#end`
  stanger den som oppnades sist. Varje post bar ocksa state-kontexten -- `#in`
  satter den for reglerna inuti, och blocket runt behover sin egen tillbaka.

  Tva tysta fel fixade pa vagen:
  - Ett block som lamnades oppet i slutet av en fil accepterades tyst. Nu:
    "prog.csp:5 #when opened on line 3 was never closed" -- innerst forst, och
    pa raden det OPPNADES. Inte i REPL:en, dar ett block ar oppet medan man
    skriver.
  - Ett bart uttryck inuti ett oppet block kordes som immediate i stallet for
    att bli en regel i blocket. Nastlingen avgor nu, inte texten.

## UART
  Skicka strängar och tecken på ett UART-objekt:

  #uart Tx 0:5
  Tx.send = 'X'
  Tx.send = "Hello"

## UDP/SOCKET
  Skicka meddelanden över UDP/IP:

  #buffer Udp:128 inout udp 192.168.2.1  // interface address
  Udp = "Hello world\n"
  Udp.tx = 1

## PRIORITET (Tony 2026-09-08): interaktiv reaktiv BASIC pa hardvara
  Text-flytten ur instruktionsstrommen, strommade instruktioner, tcp/uart och
  epoll ar alla PARKERADE bakom detta. Det som raknas ar vad som hander nar man
  skriver en rad vid prompten.

  GJORT 2026-09-08. Tre saker stod har som "rader som tas emot, sager OK och
  gor ingenting". Efter att ha undersokt dem var EN av tre sann, och det ar
  vart att skriva ner varfor de andra tva inte var det:

  1. csp_csr returnerade TYST ur en void-funktion pa sex stallen. FIXAT: den
     returnerar int och satter ERR_OUT_OF_MEMORY, och csp_rebuild propagerar.
     MEN: det gick inte att observera. csp_rt_start kor direkt efter, ur samma
     tomma bump, och faller da ocksa -- sa csp_rebuild gav -1 anda. Tre
     programformer svepta mot -m pa host (fa lov/manga regler, manga lov/breda
     regler, och det vanliga fallet) utan att traffa fonstret. LATENT, inte
     levande. Det som gor det latent och inte omojligt ar att rt_start NOLLAR
     mid_full hogst upp, sa csr:s fel lamnar inget spar alls.

  2. `B = A` mellan tva buffertar FUNKAR. Den ar en cykel efter, darfor att en
     regel laser den commitade halvan -- den vanliga DIN/DOUT-regeln, inte ett
     fel. Jag tittade pa en /state i forsta cykeln och drog fel slutsats.

  3. Byte-indexering FUNKAR ocksa. Det var LISTNINGEN som tappade subscriptet:
     `A[0] = 65` listades som `=65`, `T = B[0]` som `T=`. En Buf[a..b] ar en
     syntetiserad DECL_VIEW utan eget namn, och listningen renderade det namnet.
     FIXAT i exprbuf_var: parentens namn plus byte-intervallet.

  LARDOMEN, och den ar viktigare an de tre: listningen fick MIG att felsoka
  tva fungerande funktioner som trasiga. Det ar precis vad en listning som inte
  gar tillbaka in kostar -- den ar inte kosmetik, den ar det man laser nar man
  inte kan koda om vad som hander.

## TCP och UART som transporter (parkerat 2026-09-08)
  Bada finns redan som terminaler i utils/candyspeak_parse.yrl (T_TCP, T_UART).
  Det som skiljer dem fran UDP ar INTE mekaniken utan semantiken:

    UDP droppar det den inte hinner lasa. Ett datagram ar en ogonblicksbild, och
    en ko av dem ar inte data som vantar utan data som redan var inaktuell.

    En STROM har inga meddelandegranser att droppa pa, och de olasta byten i
    karnan AR mottrycket som gor den till en strom. Sa tcp/uart BEHALLER det de
    inte kan ta. Den som behover varje meddelande valjer tcp -- det ar svaret,
    inte en runtime-flagga pa udp.

  Det oppna: en `#buffer` ar en FAST layout och en strom ar det inte. Antingen
  ramar man in strommen (langdprefix? avgransare?) eller sa blir tcp/uart nagot
  annat an en buffer -- se console-transporten nedan, som ar samma fraga.

## Pollset i epoll-form (parkerat 2026-09-08)
  Idag byggs pfd-arrayen om vid varje poll (poll_set i port/csp_linux.c). Vad
  det borde vara:

    ps_add / ps_mod / ps_del / ps_wait   -- fyra anrop, poll() under

  Kompakt array, swap-remove, parallell info-array. VIKTIGT: reverse-indexet ska
  INTE nycklas pa fd -- fd:er ar processglobala och obundna, sa en tabell fd->
  index maste dimensioneras efter fd-rymden eller hashas. Indexet som behovs ar
  agare->plats, och det kostar noll: varje socketpost bar sitt platsnummer och
  swap-remove rattar det ena element som flyttade.

  epolls data.u32 AR den parallella infon, buren av karnan i stallet for av oss:
  registrera en token per fd, fa tokens tillbaka fran wait, aldrig fd:er. Da
  finns ingen reverse-lookup i nagon av implementationerna. Men epoll ar
  LANGSAMMARE an poll vid sex fd:er (ett syscall per registreringsandring mot
  noll) -- skalet att forma det sa ar tcp senare, manga anslutningar.

  DET SOM FAKTISKT SAKNAS IDAG ar remove: ingenting stanger nagonsin en
  UDP-socket. /undo pa en `#buffer B:4 in udp 5000` och porten ar bunden livet
  ut. Med add/remove kan en mark-and-sweep i slutet av csp_input stada -- varje
  levande in-buffert anropar csp_udp_recv varje cykel, sa en omarkerad socket ar
  en som ingen buffert vill ha.

## Konsol-routing over CAN: ett mikro-OS som lib i CandySpeak (Tony 2026-09-08)
  FORLAGAN, sagd av Tony: Forths KEY/EMIT-vektorer, och F18/GA144:s
  PORT-EXEKVERING -- en nod satter PC till en portadress och kor det som kommer
  in pa porten. `can.in -> repl.in` ar samma sak, grovkornigt: porten ar en
  legitim EXEKVERINGSKALLA, inte bara en datakalla. Det ar redan vad
  `#buffer Fd:4 out repl` gor.
  Skillnaden som styr designen: en F18-nod BLOCKERAR pa en tom port, och den
  asynkrona handskakningen ar flodeskontrollen -- ingen ring, ingen pollning.
  Vi har en karna och en cykel som maste fortsatta, sa vi far ringen i stallet.
  Deras elegans ar kopt for 143 andra processorer.


  BYGGT 2026-09-08: TR_CONSOLE + TR_REPL, escapen och ringarna. Se
  src/csp_console.c och doc/manual_en.md. Kvar star tva saker som visade sig
  under bygget och som blockerar sjalva routing-libbet:

  1. `B = A` MELLAN TVA BUFFERTAR KOMPILERAR, LISTAS OCH KOPIERAR INGENTING.
     Ingen varning, inget fel. `A[0] = 65` tappar dessutom vansterledet i
     listningen (`=65`), sa byte-indexering som LVALUE finns inte heller.
     Det betyder att bryggan console<->can maste skrivas som ett #field per
     byte -- atta falt och atta regler per riktning. Bryggan pa tva rader som
     stod har tidigare gar inte att skriva an.

  2. GENOMSTROMNINGEN. Jag skrev tidigare att N ramar per cykel i
     csp_can_output var fixen. Fel: flaskhalsen ar REGELN, som kor en gang per
     cykel och flyttar hogst en buffert. En CAN-ram bar 8 byte, sa taket ar
     8 byte per cykel oavsett vad csp_can_output gor. Att hoja det kraver att
     transporterna talar med varandra utan en regel emellan -- alltsa en RUTT
     som runtime-objekt, vilket ar precis vad ordet "routing" betyder och vad
     detta stycke egentligen efterfragar.


  MALET: skriva pa nod A:s konsol och na REPL:en pa nod B, som bara har CAN.
  Nagra (konfigurerbara) frame-id reserveras som konsolkanal. Protokollet:
  select-id pa kanalen, ratt nod svarar "sedd och redo", ingen annan svarar,
  resend och sedan timeout. Sedan gar tecken fram och tillbaka.

  DEN AVGORANDE INSIKTEN: det som ska exponeras ar inte UART:en utan REPL:ens
  BYTESTROM. Ett kort utan UART har ocksa en REPL. Pa konsolnoden ar `in` det
  anvandaren skrev och `out` det som ska visas; pa fjarrnoden ar `in` tecken att
  mata REPL:en med och `out` det REPL:en skrev. Riktningarna ar spegelvanda i de
  tva andarna, vilket ar precis vad en brygga ar.

  Som en transport pa en buffer blir routingen tva regler:

    #buffer Con:8 inout console
    #buffer Ch:8  inout can CONSOLE_ID

    Ch  <<= Con ? Con.rx && Selected
    Con <<= Ch  ? Ch.rx

  och da funkar det for VARJE backend -- console<->udp, console<->spi -- utan en
  rad till. Det ar samma tva regler med en annan #buffer.

  VAD SOM MASTE LIGGA I C, och det ar allt:
  - En TR_CONSOLE, ett case i csp_buf_input och ett i csp_buf_output.
  - Inmatningen finns REDAN: csp_line_input(&st->line, c) ar "mata REPL:en ett
    tecken" och csp_line_space() ar mottrycket. Noll nytt.
  - Utmatningen finns INTE: csp_print_char skriver rakt ut i varje port. Att
    fanga vad REPL:en skriver kraver en avtappning -- en ring pa 64-128 byte.
    Det ar hela kostnaden, och det ar den enda nya lagringen.

  ALLT ANNAT I CANDYSPEAK: select, ack, resend, timeout, sekvensnummer. Ett
  #module. Skalen: protokollet kan andras utan att flasha om runtime, det gar
  att testa pa host med -F (en CAN-buffert ar redan drivbar dar), och det ar det
  som gor pastaendet "ett mikro-OS som lib i CandySpeak" sant i stallet for
  dekorativt.

  FALLAN: mata ALDRIG REPL:en fran en regel genom att anropa csp_process_line.
  Arenan ar inte reentrant -- en deklaration som kompileras mitt i en cykel
  bygger om strukturerna cykeln star mitt i. Sanken ska KOA in i csp_line och
  lata huvudloopen konsumera den pa sin vanliga plats, exakt som en byte fran
  UART:en. csp_line hanterar redan en klistrad ko.

  AVGRANSAT (Tony 2026-09-08): EN master, EN uppkoppling i taget. Tva konsoler
  som valjer var sin nod ar bortdefinierat tills vidare, och da behovs varken
  avsandar-id i svaret eller sekvensnummer for multiplexing. Bilden ar: USB-
  serial in i en nod som har CAN, dess REPL initierar uppkopplingen, och sedan
  relayas UART fram och tillbaka om noden svarade. CANopen gor detta till en
  katedral (CiA 309, SDO block transfer); tva reserverade id och ett select ar
  hela saken.

  Och pa CAN behovs ingen egen retransmission for BITFEL: en CAN-ram ar
  kvitterad av lankskiktet eller sand om av hardvaran. Det som kan forsvinna ar
  ram som inte far plats, alltsa flodeskontroll -- inte parvis ack.

  TVA SAKER SOM AVGRANSNINGEN INTE LOSER:

  1. ESCAPE-TECKNET, och det ar det enda som INTE kan ligga i CandySpeak.
     Nar relayen ar igang ater den varje tecken anvandaren skriver, sa den
     lokala REPL:en ar oatkomlig -- och om det ar relay-REGELN som ar fel finns
     ingen vag ut alls utom reset. Escapen maste darfor sitta i C, pa det ENDA
     stallet dar bytes tas ifran, fore avledningen: en teckenjamforelse.
     Telnets Ctrl-], minicoms Ctrl-A, ssh:s ~. -- alla tre sitter dar av samma
     skal.

  2. UTMATNINGEN FAR ALDRIG BLOCKERA I csp_print_char. UART-vagen busy-waitar
     pa hardvaran, vilket ar ofarligt darfor att hardvaran tommer sig sjalv. En
     ring som bara toms av csp_buf_output toms bara nar CYKELN kor -- och
     cykeln kan inte kora medan vi star och vantar inne i en utskrift. Det ar
     ett dodlage, och det ar latt att skriva av misstag.

     Formen som funkar: konsolbufferten skickar UPP TILL N ramar per cykel sa
     lange den har bytes -- en drain pa utsidan som speglar den vi redan har pa
     insidan for UDP. Ingen ny mekanism.

     Rakningen som avgor N: en ram per cykel vid 50 Hz ar 400 B/s, och en
     `/list` pa en kilobyte tar da tva och en halv sekund. Vid N=16 blir det
     6,4 kB/s, val under vad 250 kbit CAN bar (~18 kB/s nyttolast). Sa N ar
     skillnaden mellan "gar att skriva pa" och "gar att anvanda".

  DET UNDERLIGGANDE, som ar samma fraga som tcp/uart staller: en `#buffer` ar en
  FAST layout och konsolen ar en STROM. Antagandet "fast layout tills vidare"
  bar hela vagen for tangenttryckningar, och det ar drain-takten -- inte
  formatet -- som avgor om det bar for utskrifter ocksa.
