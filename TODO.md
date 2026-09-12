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

## En STRIPPAD slavnod pa en 328p (undersokt 2026-09-09)
  MATT, inte gissat. `make -f Makefile.board BOARD=uno exec`:

    37954 byte av 32256   -- 117 %, alltsa ~5,7 kB over
    971 byte RAM av 2048  -- 47 %, RAM ar INTE problemet

  Reaktiv graf ar redan av i uno-bygget (SUPPORT_REACTIVE 0). Var bytena ligger,
  ur avr-nm pa den elf bygget anda producerar:

    main             5706   porten + allt inlinat i loopen
    eval_op          3340   opcode-tabellen
    csp_rebuild      2032   layouten (csp_csr inlinat)
    csp_load_image   1214
    csp_rt_init      1130
    csp_sys_module    838   Sys byggs i RUNTIME, varje boot
    setup_buffer      658
    csp_setup         630
    csp_load_rom      614
    add_state         516
    new_string        472 + csp_str_recount 476

  51 symboler over 150 B summerar till 28 kB, sa det finns ingen enskild klump
  att ta bort -- det ar breda nedskarningar eller inget.

  VAD EN SLAV FAKTISKT INTE BEHOVER, i storleksordning:
  - STRANGAR. En exec-nod skriver aldrig ett namn. new_string + csp_str_recount
    ar ~950 B, plus segmentvandringarna i elva walkar och namnen i imagen.
  - csp_sys_module byggs vid varje boot i stallet for att bakas. 838 B.
  - STATES-OPCODES, om slavprogrammet inte har nagra. Men INTE add_state:
    csp_rt_init skapar INIT/NORMAL/FAILSAFE ovillkorligt, sa den funktionen
    finns i varje bygge. Se posten nedan om varfor den vager vad den vager.
  - eval_op: float- och strangoperationer ar det mesta av bredden.

  DET SOM INTE GAR ATT TA BORT: csp_rebuild. En nod som tar emot en NY image med
  /upgrade maste kunna lagga ut den -- men bara vid boot, vilket den redan gor
  (/upgrade pausar och kortet strommas om). Sa den ar engangs, inte per cykel,
  och kan inte kompileras bort.

  MAIN (5706 B) BRUTEN NER 2026-09-09. Den ar inte porten -- den ar HELA CYKELN,
  inlinad. Ingen av dessa har en egen symbol i uno-imagen:

    loop setup csp_input csp_input_timer csp_input_event csp_cycle csp_eval
    csp_eval_rule csp_react csp_enq csp_pending csp_commit csp_output
    csp_output_timer csp_buf_input csp_buf_output csp_can_input

  2434 instruktioner, och bara ~30 anrop ut. eval_op (3340) ligger utanfor som
  opcode-switchen.

  DUPLICERAR INLININGEN NAGOT? Nej -- matt. NOINLINE pa csp_eval, csp_eval_rule,
  csp_commit, csp_buf_input och csp_buf_output:

    37758 -> 37796   = +38 B

  Alltsa var GCC:s val redan det mindre. Det finns inget att hamta dar, och
  "for mycket inline" ar INTE forklaringen till main.

  DE TRE HOGARNA, av 37758:

    cykeln            main 5706 + eval_op 3340   = 9046   (24 %)
    uppstart/layout   rebuild 2032, load_image 1214, rt_init 1130,
                      sys_module 838, setup_buffer 658, csp_setup 630,
                      load_rom 614, str_recount 476, new_string 472,
                      add_state 392, setup_routes 332, setup_decl 310,
                      load_rom_sys 162            = 9260   (25 %)
    allt ovrigt                                   ~19452   (51 %)

  UPPSTART/LAYOUT AR DEN INTRESSANTA HOGEN. Den kors EN gang och bygger upp det
  som en baked image redan beskriver. En slav som bara nagonsin kor en fardig
  image ar precis det fallet dar den kan gora mer vid byggtid -- och det ar 9 kB
  mot ett gap pa 5,7.

## BARE-METAL AVR-PORT: port/csp_avr.c (skriven 2026-09-09, EJ KORD)
  Vad som finns: hela portytan utan Arduino-karnan. Pinnar som PORTx/DDRx/PINx
  med Arduino-NUMRERINGEN behallen (0..7 PORTD, 8..13 PORTB, 14..19 PORTC, tre
  jamforelser i stallet for tre PROGMEM-tabeller), UART polled utan ringar,
  TIMER0 i CTC pa exakt 1 kHz, ADC direkt, partens egen EEPROM (1 kB, med
  skip-om-lika sa oforandrade byte inte tar slitaget), CAN-stubbar, och main().

  KARNAN AR BEVISLIGEN BORTA. Symbolerna i imagen: HardwareSerial (sju metoder),
  Print, pinMode, digitalWrite, de fyra PROGMEM-tabellerna, millis, micros,
  turnOffPWM, ctors och tre ISR:er -- alla saknas. Kvar av avr-libc ar malloc,
  och den kommer fran uno-kortets CSP_ARENA_MALLOC 1, inte fran porten.

  RESULTAT, med samma kompilator och samma konfiguration:

                                    text    data   bss
    arduino exec                   37462      38   933
    bare metal, ingen lokal konsol 36658       6   769    -836 flash, -164 RAM
    bare metal, med lokal konsol   38614       6   769

  Utan lokal konsolinlasning ar funktionen DEN SAMMA som arduino-exec (det
  bygget laser heller ingen serial), sa -836 ar en rattvis jamforelse. Med
  inlasning ar det +1114, och det ar radeditorn -- se posten om den.

  FALLAN SOM KOSTADE EN OMGANG: forsta matningen sa +1064, alltsa STORRE. Tony
  sa "det kan inte stamma", och det stamde inte: jag byggde med avr-gcc 4.8.1
  och arduino-bygget anvander 7.3.0. Bada ligger installerade under
  ~/.arduino15/packages/arduino/tools/avr-gcc/. Symptomet var att ALLT var
  uniformt storre -- csp_print_uint 162 mot 100, lookup_string 254 mot 148 --
  vilket ar signaturen for en kompilatorskillnad och inte for innehall.

    ~/.arduino15/internal/arduino_avr-gcc_7.3.0-*/bin/avr-gcc   <- den ratta

  Tva konfigurationsfallor pa vagen: -DCSP_BOARD=csp_board.h maste med (annars
  SUPPORT_REACTIVE 1 och den reaktiva grafen foljer med), och CSP_SETTINGS_BYTES
  ar 128 bara under #ifdef ARDUINO -- en bare-metal-AVR far annars host-defaulten
  1024 och csp_rt_t vaxer med 896 byte RAM.

  KORTET OCH MAKEFILE-GRENEN FINNS NU (2026-09-09, samma dag):

      make -f Makefile.board BOARD=uno_bare            # imagen
      make -f Makefile.board BOARD=uno_bare upload     # via arduino-cli

  boards/uno_bare/uno_bare.terms beskriver kortet, `avr8`-grenen i
  Makefile.board bygger det. Tre saker skiljer det fran alla andra bare-metal-
  kort har, och grenen ar mest en lista pa dem:

  - INGEN GENERERAD LANK. avr-gcc har egen crt, vektortabell och linkerskript;
    parten har ingen {map,...} att bygga regioner ur och dess boot-ROM kollar
    ingen checksumma. Alltsa NOLD=1: ingen startup, ingen sysinit, ingen
    <board>.ld, ingen csp_chip-tabell -- och en .hex direkt ur elf:en i stallet
    for en gap-fylld .bin (som annars gar upp till EEPROM-imagen pa 0x810000
    och blir atta megabyte).
  - INGEN PINMUX. En AVR har inget mux-register. gen_chips check_avr VAGRAR
    darfor {pin,...} och {irq,...} pa ett bare-AVR-kort i stallet for att tyst
    generera ingenting. Kravet ar i stallet {core, Hz} -- det blir F_CPU, och
    bade UART-delaren och 1 kHz-ticken raknas ur den.
  - TOOLCHAIN AR ARDUINO-CLIS avr-gcc, inte den pa PATH. Makefilen letar upp
    ~/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-*/bin sjalv. CROSS=
    skriver over. Det ar exakt fallan nedan, inbyggd sa den inte kan uppstå igen.

  LANKEN SAGER INTE IFRAN NAR IMAGEN INTE RYMS. avr-ld:s skript ger text-
  regionen ingen LENGTH, sa en image over 32K lankar tyst. Regeln kor darfor
  `avr-size --mcu=... -C` efterat och skriver ut en rad:

      Program   35126 bytes  107.2%
      Data       1280 bytes  62.5%
      ** DOES NOT FIT on atmega328p: Program over 100% **

  JAMFORELSE MED SAMMA FLAGGOR (bada exec-only, bada med rom_host, 2026-09-09):

                                     flash    RAM
    uno (arduino exec)               37558    971     116 %
    uno_bare (bare metal)            35126   1280     107 %
                                     -2432

  Storre besparing an de -836 som matten ovan gav, av tva skal: nu far bare-
  bygget ocksa -mcall-prologues, -fno-inline-functions-called-once och
  -Wl,--relax (som handraden saknade), och den laser lokal konsol. RAM-siffrorna
  ar INTE jamforbara: uno_bare har ingen CSP_ARENA_MALLOC, sa dess 512-byte-
  arena ligger i .bss i stallet for att tas ur heapen vid start.

  KVAR: 33748 mot 32256 = 1492 byte over efter att radinmatningen kom in
  (-1378). Kvarvarande lever ar layout vid byggtid (~9260) -- egen post.

  MEGA_BARE (2026-09-09): samma port pa en ATmega2560, dar den FAR PLATS --
  fullt bygge med kompilator och REPL, 44 % av flashet. boards/mega_bare/ har
  wokwi.toml + diagram.json, sa porten gar att KORA utan hardvara.

  Tva partskillnader, bada bakom CSP_AVR_MEGA i port/csp_avr.c:
  - PINNUMRERINGEN AR EN TABELL. Pa 328p ar den aritmetik (0..7 PORTD, 8..13
    PORTB, 14..19 PORTC); pa Mega ar den det inte -- pin 4 ar PG5 mellan pin 3
    pa PE5 och pin 5 pa PE3. En byte per pin (portindex + bit) plus en tabell
    med de elva portADRESSERNA tagna ur &PORTx. Inte raknade: PORTA..PORTG
    ligger tre byte isar fran 0x22 och PORTH..PORTL tre byte isar fran 0x102,
    och att rakna over det gapet for hand skriver en trovardig fel pekare.
    Tabellen ar verifierad post for post mot karnans tva tabeller.
  - SEXTON ADC-KANALER. Sjatte mux-biten ar MUX5 i ADCSRB, inte i ADMUX. Skriver
    man bara de laga bitarna laser man kanal ch-8 -- ett riktigt varde fran fel
    pinne, alltsa den svaraste sortens fel.

  OCH EN SAK SOM SAKNADES HELT: main-slingan las tecken till csp_con_input men
  korde ALDRIG en fardig rad -- inget csp_process_line, inget csp_line_done. Med
  --gc-sections betydde det att hela kompilatorn foll bort: mega_bare matte
  35406 byte, sag ut som ett fullt bygge och var en dod konsol. Med raden inkopp-
  lad: 116788. Symptomet var att imagen sag MISSTANKT LITEN ut.

  KARNANS KOSTNAD, samma program byggt tva satt:

                                    flash            RAM
    uno       exec, arduino-cli     37408            960 + heap-arena
    uno_bare  exec, avr-gcc         33750   -3658   1264 (512 arena)
    mega      fullt, arduino-cli   122208           2335 + heap-arena
    mega_bare fullt, avr-gcc       116788   -5420   5557 (3072 arena)

  RAM-kolumnerna ar inte jamforbara: arduino-korten tar poolen ur heapen vid
  boot och sizar den till det som blev over, sa deras arena ligger inte i den
  statiska siffran.

  INTE HELLER GJORT:
  - PWM saknas medvetet (csp_board_analog_output ar en no-op) -- slaven laser
    och svarar, den driver inget.

  UPPLADDNING ar oforandrad: `arduino-cli upload -i <hex>` tar en fardig image,
  sa bootloadern pa parten ar fortfarande vagen in. `make upload` gor just det;
  {fqbn,...} och {port,...} i kortets terms ar bara till for den raden.

## RODATA LAST SOM DATA -- FYRA TABELLER TILL (2026-09-09)
  Efter att csp_str_byte var fixad sa kortet fortfarande `Error: internal error`
  pa allt som innehholl en IDENTIFIERARE, medan `1+2` gav 3. Det var inte
  identifierarna. Det var FELMASKINERIET:

      static rostring_t const err_tab[] RODATA = { ... };   // flash

      rostring_t f = (...) ? err_tab[err] : NULL;           // last som DATA
      return f ? f : ros_err_internal;

  err_tab ar RODATA -- flash pa AVR -- och ett vanligt index laser datarummet pa
  den adressen. Det kom tillbaka NULL, sa VARJE fel pa en AVR skrev reserv-
  strangen "internal error", oavsett vad som gick fel. `Z` sa "internal error"
  nar det egentligen var "variable Z is not declared".

  DET AR DEN VARSTA PLATSEN FOR BUGGEN. Den gar inte sonder i en funktion -- den
  gar sonder i maskinen som BERATTAR vad som gick sonder, och den ljuger med ett
  trovardigt svar. Den kostade en timme av felsokning at bada hallen.

  Svep av alla RODATA-tabeller i tradet. Fyra last fel:

    err_tab      alla fel blev "internal error"
    tag_tab      `// F` i stallet for `// R` i /list -- bada gick forbi det
    endian_tab   endian-namn i listningar
    filt_table   `/list timers` filtrerade inget och listade allt

  Ratt gjorda sedan tidigare, och forlagorna att kopiera: pindir_tab och
  vtype_tab (ro_ptr), stop_toks/stop_pos i pmatch-VM:en (ro_byte),
  csp_builtin_funcs (rd16(..., rom)).

## POOLEN OCH STACKEN AR SAMMA RAM, OCH INGET KONTROLLERADE DET (2026-09-10)
  Symptom: `#variable count = 1` svarade OK och listades sedan under ett
  ROM-NAMN, med den foregaende deklarationen borta. Ingen fel, ingen fault.

  ORSAK: arenan ligger i .bss och stacken vaxer ner fran RAMEND mot den. Nar
  parserns ramar nadde ner i poolen skrev de over de NYASTE deklarationerna --
  de ligger overst. Matt pa mega_bare: 3627 byte stack fallerar, 4477 fungerar.

  CSP_STACK_RESERVE i csp.h ar 2048 men anvands bara for att SIZA en
  malloc-arena. En statisk arena ignorerar den helt -- csp_mem_init sager till
  och med "The stack reserve is not lost with it", vilket ar precis det som var
  fel. Darfor har bridgezone och dl1200 handtrimmade code_budget med kommentarer om
  kort som hangt: samma bugg, tweakad en gang per kort och aldrig kontrollerad.

  ATGARDAT: `stackcheck` i Makefile.board, kord vid LANKEN pa varje bare-kort
  (bada ARCH-grenarna). Den laser partens RAM ur den genererade headern
  (CSP_AVR_RAM / CSP_LPC_RAM / CSP_STM_RAM), drar bort data+bss och sager
  ifran under STACK_MIN (4096).

  FORSTA KORNINGEN HITTADE ETT TRASIGT KORT: lpc1754 hade 1192 BYTE STACK med
  code_budget 9216 -- samma varde dl1200 bar innan det sanktes till 6144 efter
  en hangning. lpc1754 blev kvar dar for att den missades da. Sankt till 6144, ger 4264 som dl1200. Det var en
  langvarigt flaggad misstanke som ingen kunde avgora; nu ar den matt.

    mega_bare    4221      dl1200      4264      bridgezone   5064
    crazyflie   93712      lpc1754     4264  (var 1192)

  Tre av dem ar handtrimmade i efterhand efter att kort hangt, och alla landar
  precis over 4096. Det ar battre stod for golvet an interpolationen
  mellan mina tva matpunkter.

  TONYS FRAGA LOSTE DET: "Kan det vara nagon slags setup med stacken?" Testet
  som avgjorde var att ga at ANDRA hallet -- mindre pool ger MER stack, for de
  tar av samma RAM. Vid 1024 fungerade det.

## #variable KRASCHADE KORTET -- PORTEN GAV RUNTIMEN INGEN KOMPILATOR (2026-09-10)
  `#variable x = 1` startade om mega_bare. Fixat; kortet svarar OK nu.

  ROTEN: port/csp_avr.c anropade `csp_rt_init(&state, 0, 0)`. Tredje argumentet
  ar KOMPILATORNS state, och csp_rt_init gor `st->cs = cs;  // NULL on a node
  that only runs images`. Alla andra portar skickar CSP_CSTATE:

      csp_arduino.c / csp_lpcopen.c / csp_stm32.c:
          csp_rt_init(&state, REACTIVE_DEFAULT, CSP_CSTATE)

  csp_parse:s ALLRA FORSTA sats ar `st->cs->ap = &alloc;`. Med cs == NULL skrivs
  det pa adress 0 plus en offset -- pa AVR ar det REGISTERFILEN och I/O-rummet,
  dar stackpekaren ligger pa 0x5D/0x5E. Skrivningen flyttade stacken och nasta
  ret hamtade en adress ur tomma intet.

  VARFOR DET TOG SA LANG TID:
  - En AVR som faultar STANNAR INTE, den startar om. Porten skrev ingenting vid
    start, sa en krasch var oskiljbar fran ett kommando som inte gjorde nagot:
    tystnad, sedan en ny prompt.
  - /list, /memory, `1+2` och `n` fungerade hela tiden -- INGEN av dem gar genom
    csp_parse.

  VAD SOM LOSTE DET: en boot-banner (sa att en omstart syns alls), MCUSR
  (som skiljer "nagon tryckte reset" fran "programmet korde till adress 0"), och
  en krasch-ring i .noinit (RAM overlever en reset pa AVR -- bara .bss nollas).

  TRE FALLOR I VERKTYGET SJALVT pa vagen:
  - ISR:er ar ocksa instrumenterade, sa "sista funktionen" var alltid ticken.
    ISR(vect, __attribute__((no_instrument_function))).
  - Utskriftslagret kors en gang per TECKEN och drankte ringen.
    -finstrument-functions-exclude-function-list -- per FUNKTION, inte per fil:
    csp_print_char ar definierad i PORTEN, inte i src/csp_print.c.
  - Ringen skrevs over av bannern som skulle lasa den. Kopieras nu forst i main,
    och main bar no_instrument_function.
  Plus: WATCH_SKIP definierades efter `CFLAGS :=` (omedelbart expanderad) och var
  alltsa tom. Det som avslojade det var att storleken blev BYTE-IDENTISK over en
  "fix". En andring som inte andrar en enda byte har inte hant.

  TVA FLER UTELAMNANDEN I SAMMA PORT, bada mot vad alla andra portar gor:
  - `#define CSP_EMBEDDED 1` saknades. MAX_LINE_TOKENS ar 24 med den och 64
    utan, sa `token_t tv[MAX_LINE_TOKENS]` var 384 byte i stallet for 144 --
    och parservagen nastlar flera sadana. Matt, inte gissat.
  - csp_clr_error efter boot-laddningen (egen post nedan).

## #variable GJORDE INGENTING PA KORTET -- ETT FEL SOM ALDRIG RENSADES (2026-09-10)
  `#variable n = 10` var TYST och gjorde inget; nasta rad rapporterade
  "cannot load from eeprom". Det var inte adressrummen och inte parsern.

  KEDJAN:
  1. Inget sparat tillstand vid boot -- NORMALFALLET -- sa csp_eeprom_load
     satter ERR_CANNOT_LOAD.
  2. csp_set_error behaller det FORSTA felet, sa det ligger kvar for alltid.
  3. csp_parse_variable borjar med `if (st->ps.err != ERR_OK) return -1;`

  Alltsa failade VARJE #variable vid vakten, innan parsern tittat pa raden.

  port/csp_avr.c rensade aldrig felet efter boot-laddningen. Alla andra portar
  gor det -- csp_arduino.c, csp_lpcopen.c, csp_stm32.c -- och arduino-portens
  kommentar sager varfor: "no saved state is the normal case at boot, not an
  error to carry into the first command". Min port var undantaget.

  TVA FIXAR:
  - porten: csp_clr_error efter boot-laddningen, som de andra tre.
  - KLASSEN: csp_process_line rensar felet FORST pa varje rad. En rad
    rapporterar sina egna fel innan den returnerar, sa det finns aldrig nagot
    att bara med sig -- och att rensa i BORJAN i stallet for i slutet gor det
    sant oavsett vem som satte felet eller nar.

  LARDOM: ett fel som overlever sin rad ar inte ett meddelande, det ar ett
  TILLSTAND -- och det tillstandet gjorde en hel sprakfunktion oanvandbar utan
  att saga ett ord.

## make ro_poison -- ETT ANDRA ADRESSRUM PA HOST (2026-09-09)
  Tonys ide, och den fungerar: lagg const-datan pa tva platser, en som finns och
  en som inte finns, och lat runtime plocka upp offseten. Se doc/RO_POISON.md.

  TRE DELAR, INGEN ANNOTERING:
    utils/ro_ld.sh      genererar ld-fragment som samlar KARNOBJEKTENS .rodata
                        i en sidjusterad sektion csp_ro, via INSERT AFTER
    port/csp_linux.c    csp_ro_init(): mmap kopia, mprotect(PROT_NONE) original,
                        delta = skugga - original. Plus SIGSEGV-trap.
    include/csp.h       accessorerna lagger pa deltat

  Per OBJEKTFIL, inte per deklaration -- det ar hela skalet att det ar ett
  linkerskript och inte makron. En tabell som glomt sin RODATA-markering tacks
  ocksa, och det finns ingen attonde bygglista att halla i takt.

  TVA SAKER SOM INTE VAR UPPENBARA:
  - LMA/VMA-uppdelning gor INGENTING i en hostad process. Linux laddar pa
    p_vaddr och struntar i p_paddr. Deltat maste raknas vid start -- vilket
    dessutom overlever ASLR.
  - __start_/__stop_ genereras bara for ORPHAN-sektioner. En sektion som skapas
    i ett explicit SECTIONS-block ar ingen orphan och far inga; lanken foll pa
    `undefined reference to __start_csp_ro`. De definieras nu i skriptet.
  - Oversattningen ar INTERVALLKONTROLLERAD. ro_memcmp anropas med RAM-sidan
    forst (ro_memcmp(s->ptr, s_low, 3)) medan andra accessorer tar den
    forgiftade sidan forst. Testet sitter pa ADRESSEN, inte argumentpositionen.

  DEN NAMNGER LASNINGEN. En SIGSEGV-hanterare kanner igen sin egen sektion:

      *** RODATA read without an ro_ accessor: 0x... (csp_ro+0x1a0)
      csp_print_str <- csp_print_rostr <- csp_line_prompt <- main

  En small nagon annanstans aterutloses med hanteraren borttagen. Avslutar 90.

  FORSTA FYNDET: host-portens egen csp_print_rostr castade en rostring till
  const char* och lat csp_print_str ga igenom den. Fungerade bara for att host
  har ETT adressrum; port/csp_avr.c har alltid last byte for byte. Bada portarna
  gor likadant nu.

  SVITEN AR REN UNDER GIFTET: 220/220 repl, 78/78 unit. Tio fixar dit, och
  TRE AV DEM VAR INTE FELLASNINGAR utan const-data i RAM pa target:
    boot_path              8 byte, `static const char[]` utan RODATA
    csp_num_builtin_funcs  1 byte, och OSYNLIG for ro_check (skalar, inte `namn[`)
    exprbuf_str/csp_print_just  literaler som bara skickades in for att matas

  DEN DYRASTE LARDOMEN VAR VERKTYGETS EGEN: backtrace() i en signalhanterare ar
  inte async-signal-safe. Lag felet inuti libc medan ett las holls LASTE SIG
  hanteraren; timeout dodade processen och all buffrad stdout dog med den. Det
  sag ut som om ingenting hant -- tomt `got`, inget felmeddelande -- och kostade
  FYRA felaktiga hypoteser. Adressen gar nu ut med write(2) FORE backtracen.
  Fyndet under fanns hela tiden och reproducerade sex ganger av sex.

  BEGRANSNING: gcc konstantviker `static const`-lasningar med KONSTANT index
  (tab[2] utan att rora minnet). Buggarnas form ar runtime-index, sa det kostar
  lite i praktiken -- men det ar skalet att ro_check och ro_poison ar
  KOMPLEMENT: den forsta ser kod inga tester kor, den andra ser pekare ingen
  kallkodsskanning kan folja.

## make ro_check -- RODATA-klassen FANGAD I KALLKODEN (2026-09-09)
  Tony: "jag vill att vi hittar buggarna under kompilering eller test, inte pa
  target". utils/ro_check.py gor det, och utan att rora en enda deklaration:

    - hittar varje `<typ> namn[...] RODATA`-deklaration
    - flaggar varje lasning `namn[...]` som INTE ligger inuti ro_byte/ro_word/
      ro_ptr/ro_dword/ro_memcmp/ro_memcpy/ro_instr/ro_decl/rd8/rd16/sizeof/DBG
    - `&namn[i]` ar OK: att ta en ADRESS ar lagligt i bada rummen, det ar
      lasningen som inte ar det
    - host-bara filer (port/csp_linux.c, port/csp_dump.c) hoppas over: ett
      adressrum dar, sa en ra lasning ar RATT
    - `#ifdef DEBUG`-block blankas; filen scannas HEL, inte rad for rad, for en
      DBG() kan oppna pa en rad och ta sitt argument pa nasta

  VALIDERAD genom att lagga TILLBAKA alla fyra buggarna: den namnger alla fyra
  (fem trafflar -- filt_table har tva lasningar). Kor i `make test`.

  Varfor kallkod och inte runtime: ingenting behover koras, inget kort ar
  inblandat, och den tacker kod som inga tester exekverar. Giftet pa host hade
  bara sett de vagar testerna gar.

  KVAR som den INTE tacker: en tabell utan RODATA-annotering (den ser den inte),
  och pekare som byter adressrum vid runtime (csp_seg_slot). Det senare ar vad
  __flash skulle gora osagbart -- se posten om den.

## __flash: MATT KOSTNAD (2026-09-09)
  Kodstorlek: ingen. 74 byte mot 72 pa ett testfall -- samma LPM, skillnaden ar
  vem som skriver den.

  Konverteringsyta: 59 handskrivna RODATA-deklarationer (613 i gen/ = en
  generatorandring), ~88 handskrivna rostring_t/rochar-siter (909 i gen/), och
  116 ro_*-anrop som FORSVINNER. `#define __flash` tomt fungerar pa host.

  FANGAR DEN BUGGEN? Inte som default -- `__flash*` -> generisk ar TYST och
  genererar exakt felet (ldi r24,lo8(s_a)). Med -Waddr-space-convert varnar den;
  med -Werror=addr-space-convert gar den inte att skriva.

  DET STARKASTE ARGUMENTET: csp_seg_slot returnerar `char*` for BADE ROM och
  RAM. Med __flash GAR DEN INTE ATT SKRIVA -- tva adressrum kan inte dela
  returtyp. Dagens dyraste bugg blir inte upptackt, den blir osagbar.

  OCH: `make bare_all` bygger redan uno_bare och mega_bare, sa en AVR-
  kompileringskontroll kors i det vanliga svepet, utan target.

  FORBEHALL: __flash ar LPM = forsta 64 kB (__memx bortom, langsammare), och
  mega_bare ar redan 116 kB image.

  REKOMMENDATION: inte hela tradet. RODATA-tabellerna ar statisk lagfrekvent kod
  som ro_check nu bevakar. Men img_p_t + strangaccessorerna (~15 siter) betalar
  sig: det ar dar den dyra buggen satt och det enda stallet dar en pekare byter
  adressrum vid runtime.

## -Wshift-overflow=2 AR INLAGD (2026-09-09)
  Inte i -Wall, och den som betyder nagot nar `int` ar 16 bitar:

      warning: result of '1 << 15' requires 17 bits to represent,
               but 'int' only has 16 bits

  Den hade fangat MAX_INSTRS innan nagot kort startade. -Wall fangar bara
  varianten ETT steg upp (1 << 16, shift-count-overflow -- det var FIX_SCALE).

  MATT: noll varningar pa hela tradet, host och AVR. Gratis. Ligger nu i bade
  Makefile och Makefile.board.

  -Wconversion hade fangat mem_int_r(int), men ger 554 varningar over
  csp_rt/csp_repl/csp_compile -- en engangsrevision, inte en flagga att leva med.

## SEX BUGGAR VID FORSTA AVR-KORNINGEN NAGONSIN (2026-09-09)
  mega_bare ar den forsta AVR som kort CandySpeak -- uno/micro ryms inte, mega
  hade aldrig korts, och play/mkrzero/mkrnb1500 ar SAMD (ett adressrum). Den
  blinkar nu.

  I WOKWI, inte pa hardvara. avr8js kor den riktiga instruktionsstrommen, sa
  alla sex ar LOGIKFEL som en simulator ser lika bra som en part -- ingen av dem
  hangde pa timing eller elektricitet. Det som INTE ar provat ar just det:
  klockans noggrannhet (Wokwis kristall ar exakt), UART pa en riktig ledning,
  brown-out, EEPROM-timing och slitage, och pinnar med last pa. Ett riktigt kort
  kan fortfarande saga nagot nytt. Vagen dit var tre buggar av tre olika sorter, och ingen av dem
  hade kunnat hittas utan att kora pa riktigt:

    csp_str_byte           flash last som data        tva adressrum
    MAX_INSTRS/MAX_DECLS   1 << 15 pa 16-bitars int   heltalsbredd
    csp_system_ram_used    dubbelraknad .bss          portens bokforing
    pollad UART-RX         tappade tecken             fonstret dar ingen laser
    USART_RX_vect          fel vektornamn pa 2560     ISR som aldrig kor
    mem_int_r(int)         32768 -> -32768 vid ANROP  heltalsbredd igen

  TRE AV SEX ar samma sak: pa AVR ryms inte mer an 16 bitar i en `int` och inte
  mer an tva byte i en pekare. `1 << 15`, `mem_int_r(int)` och `(long)f` i
  csp_set_file_output -- alla tre osynliga pa host.

  Var och en har sin egen post nedan.

## POLLAD UART-RX TAPPADE TECKEN, OCH VEKTORN HADE FEL NAMN (2026-09-09)
  Porten laste UART:en pollat, medvetet, och motiverade det i sitt eget huvud
  ("CandySpeak already has a line buffer for input"). Argumentet haller for
  UTDATA -- vi ar den som skriver och inget gar forlorat av att vanta -- men
  inte for INDATA: mottagaren har tva byte hardvarubuffert, och slingan lamnar
  lasslingan for att kora en hel cykel OCH for att skriva ut ett kommandos svar.
  En /state-dump ar femton rader, och en terminal i radlage levererar hela
  kommandot som en skur. Tony sag prompten ata tecken.

  ATGARDAT: ISR + 32-byte-ring (atta cyklers marginal vid 38400). Full nar den
  ar full -- den slanger det NYA tecknet och sager inget, for det finns ingen
  mottryck att applicera inifran ett avbrott, och en ring som skriver over sitt
  aldsta tecken forstor borjan pa raden i stallet for slutet.

  OCH VEKTORN: ISR(USART_RX_vect) ar ratt pa 328p och FEL pa 2560, som numrerar
  sina fyra (USART0_RX_vect). Fel namn ar inget lankfel -- avr-gcc gor en vanlig
  funktion med det namnet, vektortabellen behaller sin default-hanterare, och
  ISR:en kor aldrig. Konsolen hade blivit HELT dov. Bara -Wmisspelled-isr sager
  det, vilket ar ett skal till att den har porten byggs med -Wall. Verifierat i
  elf:en efterat: __vector_25 pa megan, __vector_18 pa 328p.

## mem_int_r TRUNKERADE TAKET DEN SKULLE VISA (2026-09-09)
  Efter MAX_INSTRS-fixen sa kortet fortfarande `instr 0 -32768` -- men input
  fungerade, alltsa var taket ratt i imagen. Tony sag det pa en gang:

      static void mem_int_r(int v, int w)      // int, alltsa 16 bitar
      ...
      mem_int_r(limit, 9);                     // limit ar int32_t 32768

  Trunkeringen skedde vid ANROPET, innan funktionen sag nagot. Raden
  rapporterade alltsa den bugg som just fixats. csp_print_int tog redan
  ivalue_t, sa bara parametern och call-site-castarna behovde breddas.

## MAX_INSTRS VAR NEGATIV PA AVR (2026-09-09)
  Kortet sa `setup failed: out of memory` med ett TOMT program och 4184 byte
  ledig pool. Svaret stod i /memory-utskriften:

      instr           0        -32768
      decl            0        -32768

  Ett `-` (eller ett negativt tal) dar host sager 32768. mem_row skriver ut
  gransen som int32_t, sa den syntes.

      #define INSTR_BITS 15
      #define MAX_INSTRS (1 << INSTR_BITS)

  Pa AVR ar `int` SEXTON bitar, sa 1 << 15 ar INT_MIN. Varje vakt av formen

      if ((st->ps.nd - st->rom_nd) >= MAX_DECLS || !mem_fits(...))
          csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);

  jamfor ett int mot ett NEGATIVT tak och ar alltid sann. Forsta deklarationen
  setup forsokte lagga till avvisades. Samma sak i csp_rt.c:2273
  (segmentallokeringen), csp_compile.c:205 och rom_scan_instr:s
  `for (p = 0; p <= MAX_INSTRS; p++)`, som korde noll varv.

  FIXEN ar `1L`, och den fanns redan EN RAD OVANFOR:

      #define MAX_INDICES (1UL << INDEX_BITS)   // 1<<16: needs the long on a 16-bit int

  Nagon traffade fallan en gang, en bit hogre upp, och fixade bara den.
  Genomgang av alla andra `1 << *_BITS` i tradet: resten ar <= 7 bitar.

  SAMMA FAMILJ som FIX_SCALE (1 << 16) samma dag. Monstret: ett skift mot en
  bredd som ar 15 eller mer ar en AVR-bugg tills det star L eller UL i det.

## /memory SA `free 0` PA ETT KORT SOM HADE HUNDRATALS BYTE OVER (2026-09-09)
  AVR-portens csp_system_ram_used gjorde bara

      return csp_system_ram_capacity() - raw_free();

  raw_free() mats fran toppen av .bss, och BADE den statiska arenan och `state`
  ligger i .bss -- alltsa redan inne i den siffran. /memory skrev sedan ut dem
  igen i sina egna `struct`- och `buffers`-rader, ackumulatorn gick over partens
  RAM och `free` klampades till 0. Kortet hade ~2635 byte stack (Data 5557 av
  8192) och blinkade hela tiden.

  port/csp_lpcopen.c och port/csp_stm32.c har gjort subtraktionen sedan de
  skrevs (`ours = state.mem_limit + sizeof(csp_rt_t)`). AVR-porten var den enda
  som inte gjorde det. `state` flyttad upp i filen for att kunna namnas dar.

## csp_str_byte LASTE FLASH SOM DATA PA AVR (2026-09-09, Tony korde det i Wokwi)
  Megan sa `setup failed: out of memory` med en TOM ROM (10 decl, 33 instr) och
  en pool pa 4184 byte. `/memory` sa varfor:

      kortet:  string 61 / 128     names 61 / 512
      host:    string 61 / 4096    names 11 / 512

  Samma tal tva ganger. 61 namn i 61 byte text betyder att varje langdbyte last
  som 0, sa csp_str_recount:s vandring

      ofs += csp_str_byte(st, ofs) + 1;

  steg EN byte per strang. Sedan failade setup nar tabellerna dimensionerades ur
  ett namnantal fem ganger for stort.

  ROTEN: csp_str_byte ar den ENDA plats identifierartext las -- csp_str_len,
  csp_str_char, csp_str_ofs och csp_str_ncmp/eq/eq_ro ar alla byggda pa den --
  och den gjorde `return (uint8_t)*csp_ram_str_at(st, pos);`. csp_seg_slot
  lamnar `&rom_p.instr[i]` for en run som kom in med IMAGEN, alltsa en
  FLASH-adress pa AVR, och ett vanligt `*` laser datarummet pa samma nummer.

  csp_get_instr har alltid gjort det ratt (ro_instr for ROM-omradet). Det har ar
  samma uppdelning ett lager ner, och kommentarerna runt omkring pastod redan
  att lagret var AVR-sakert ("Segment-aware string helpers ... so they are
  AVR-PROGMEM-safe where csp_str_at's raw pointer is not") -- det var precis den
  primitiv de vilade pa som inte var det.

  FIXEN: testa `(h + 1) < rom_nn` och lasa med ro_byte. En runs payload-slottar
  ar sammanhangande, sa hela posten ligger pa en sida av rom_nn.

  VARFOR DEN LEGAT KVAR: ingen AVR har nagonsin kort CandySpeak. uno/micro ryms
  inte, mega hade aldrig korts, och play/mkrzero/mkrnb1500 ar SAMD -- ett
  adressrum.

  csp_str_at (ra pekare) ar fortfarande inte AVR-saker, men den anropas bara fran
  port/csp_dump.c (host) och en #ifdef DEBUG-rad, sa den vagen kor aldrig pa ett
  kort.

## IMAGE-REGISTRET FUNKAR INTE PA AVR (2026-09-09, Tony korde det i Wokwi)
  `/images` pa en mega sa:

      0: ROM gen=37900 size=274371596 rules=4186  (header CRC BAD)

  `csp_images` ar en ORPHAN-sektion -- inget linkerskript namnger den -- sa var
  den hamnar ar en heuristik. Pa AVR ar bada svaren fel, och bada ar matta:

    mega      (arduino-cli, 122 kB)  VMA 0x1dc72 -- i FLASH, och koden laser
                                     arrayen som DATA. En 16-bitars dataadress
                                     kan inte ens namna den.
    mega_bare (avr-gcc, 116 kB)      VMA 0x8002ca -- samma adress som .bss
                                     borjar pa. Posten kopieras inte, eller
                                     nollas direkt. Laser NULL.

  Att i stallet lasa den som flash hjalper inte: pgm_read_word ar LPM och nar
  forsta 64 kB, och sektionen hamnar bortom det pa precis de imager som ar stora
  nog att bry sig.

  ATGARDAT: CSP_NO_IMAGE_REGISTRY for __AVR__ i csp_config.h (CSP_IMAGE_REGISTRY
  slar pa den igen). Ingenting gar forlorat -- registret svarar pa "vad LANKADE
  det har bygget", vilket bara /images och A/B-valet fragar, och ingen AVR har
  definierar CSP_HAVE_FLASH sa det finns inga slots att valja mellan.
  csp_load_rom nar firmwarens egen image genom `rom_image` direkt.

      mega       122208 -> 121270   = -938
      mega_bare  116876 -> 115946   = -930
      uno_bare    33750 ->  33278   = -472   (101.6 %, 1022 byte over)

  Kommentaren i csp.h sa BADA sakerna: ett stycke beskrev exakt den har fallan
  ("measured at 0x17d30 on a 98 kB mega firmware, past the 64 kB that
  pgm_read_byte can reach") och nasta stycke sa att const-hela-vagen loser den.
  Det senare ar sant pa ARM och falskt pa AVR, och skillnaden ar inte
  kvalificeraren -- det ar att AVR har tva adressrum.

## FIX_SCALE VAR NOLL PA AVR (2026-09-09)
  `#define FIX_SCALE (1 << FIX_SHIFT)` med FIX_SHIFT 16. Pa AVR ar `int`
  SEXTON bitar, sa 1 << 16 ar odefinierat och gcc viker ihop det till noll --
  med en varning som rullat forbi i varje bygge.

  Foljden pa den enda familj som faktiskt kor koden:
  - FIX_CONST(x) gjorde varje float-literal till 0
  - fix_round adderade 0 och TRUNKERADE i stallet for att avrunda
  - FIX_MASK blev idel ettor

  Fixen ar `((int32_t)1 << FIX_SHIFT)`. Pa 32-bitarsmal ar de tva skrivsatten
  samma konstant, sa ingenting annat andras. Kostar 60 byte flash pa AVR --
  det ar avrundningen som nu faktiskt sker.

## FLOAT PA AVR VAR REDAN AV -- det som lag i imagen var ETT TECKEN (2026-09-09)
  Tony fragade om float gar att stanga av. Det ar det redan: csp_config.h satter
  USE_FIXPOINT 1 for __AVR__, __SAMD21G18A__ och __SAM3X8E__, sa fvalue_t ar
  Q16.16 -- en int32.

  Anda lag __floatsisf och __floatunsisf i uno-imagen. Skalet:

      static inline ivalue_t fsign(fvalue_t a)
      {
          return (a < 0.0) ? -1 : (a ? 1 : 0);
      }

  `0.0` ar en DOUBLE-literal. Under fixpoint befordras heltalet till double for
  jamforelsen, och hela flyttalsbiblioteket foljer med. Ett tecken:

      37758 -> 37498 = 260 B, och bada symbolerna helt borta.

  INGEN KOMPILATORVARNING FANGAR DET. Provat isolerat: -Wconversion,
  -Wfloat-conversion, -Wdouble-promotion, -Wall, -Wextra ger alla noll.
  Konverteringen ar vardebevarande, sa GCC ar tyst.

  DEN GUARD SOM SKULLE FUNGERA ar pa LANKNIVA, inte i kallan: en AVR-image far
  inte innehalla flyttalssymboler. En grep pa elf:en i boards_all fangar hela
  klassen -- den har och varje framtida oavsiktlig float -- i stallet for en
  syntaktisk form. Inte byggt.

## ARDUINO-KARNAN: 3350 B, och den behovs INTE (Tony fragade 2026-09-09).

    HardwareSerial + Print + UART-ISR + malloc/free   1380
    __vectors + _GLOBAL__sub_I + C++-runtime           ~300
    pinMode/digitalWrite + PROGMEM-tabellerna          ~330
    libgcc-matte (__floatsisf, __udivmod64, ...)      ~1300

  Skalet den ar dar ar att uno/micro/mega gar via Arduino CLI. Men LPC- och
  STM32-porterna ar BARE METAL -- ingen leverantorskarna alls -- och en dedikerad
  328p-slav ar precis det fallet: vi ager hela kortet, det finns inget skal att
  ga via ett portabilitetslager for tre pinnar och en UART.

  Vad en bare-metal AVR-port skulle ersatta det med:
    UDR0/UCSR0A direkt          i stallet for HardwareSerial + tva ringbuffertar
    PORTx/DDRx direkt           i stallet for pinMode/digitalWrite och deras
                                PROGMEM-uppslag per anrop
    TIMER0 overflow             i stallet for millis/micros
    en static arena             i stallet for malloc/free (CSP_ARENA_MALLOC ar
                                redan valfri -- de andra korten gor det)

  Grovt: 1400-2000 B av 3350 gar att ta bort direkt. libgcc-matten stannar sa
  lange programmet raknar med 32-bitars och float.

  RADEDITORN: GJORD 2026-09-09. CSP_LINE_SIMPLE i csp_config.h, pa som default
  under CSP_EXEC_ONLY (CSP_LINE_EDIT tar tillbaka editorn). Kollektorn ligger i
  botten av src/csp_line.c: samla till newline, eka, backspace, vagra en rad som
  inte fick plats. Ingen markor, ingen historik, ingen insattning mitt i raden.

  ESCAPE-SEKVENSER AVKODAS ANDA och slangs. Att hoppa over avkodaren ar inte
  samma sak som att inte ha en: ESC ar oskrivbart och forsvinner, men '[' och
  'A' bakom det ar vanliga tecken och skulle skrivas IN i raden.

  Flaggan andrar csp_line_t:s LAYOUT (ingen historik, ingen markor), sa den
  avgors i csp_config.h -- ur samma board-header och kommandorad varje fil ser
  -- och inte per .c-fil. csp_line.h inkluderar csp_config.h for just det.

  MATT pa uno_bare: 35126 -> 33748 = -1378 flash, -16 RAM. Uppskattningen var
  ~1500. Pa `uno exec` bara -150, och skalet ar att --gc-sections redan tagit
  det mesta dar.

  (Ursprunglig anteckning, Tony 2026-09-09:)
  Den ar redan borta ur uno-imagen -- under CSP_EXEC_ONLY refererar ingenting
  csp_line_*, sa --gc-sections tar den. Men den kommer TILLBAKA i samma stund
  kommandoslingan laggs till, och da ar det den har som ska laggas till:

    csp_line.c pa AVR, 1994 B kod:
      csp_line_input      696   dispatchern -- behover en liten variant
      csp_line_recall     182   HISTORIK, nej
      csp_line_replace    162   historikstod, nej
      csp_line_insert     142   insattning mitt i raden, nej
      csp_line_escape     138   PILTANGENTER, nej
      csp_line_done       122   ja -- kon och ater-matningen
      csp_line_erase_left 112   backspace, kanske
      csp_line_tail       102   omritning efter insattning, nej
      ovriga (markor)    ~330   nej

    En "samla till newline"-variant ar grovt raknat input ~150 + done 122 +
    space/room 54 + init 36 (+ backspace 112) = ~475 B mot 1994. UPPSKATTAT,
    inte matt -- den lilla varianten finns inte an.

  OCH SKALET ar starkare an storleken: pa en RELAYAD nod har redigeringen redan
  hant i andra anden. Piltangenter och historik pa en nod man nar genom en rutt
  ar inte bara onodiga, de ar FEL -- escape-sekvenserna skulle tolkas tva
  ganger, en gang av masterns editor och en gang av slavens.

  KOMMANDOSLINGAN Tony vill ha: /upgrade och "nagra till". Under CSP_EXEC_ONLY
  ar csp_repl.c en TOM translation unit, sa det finns ingen kommandolayer alls i
  dag -- det handlar om att lagga TILL en liten, inte skala ner den stora.
  Minsta meningsfulla set: /upgrade, /images, /boot, /state. Inget av dem
  behover parsern.

## Varfor add_state ar 516 B pa AVR (Tony fragade, matt 2026-09-09)
  Det ar inte "att lagga till en state". Det ar csp_states_set_name INLINAD.

  csp_states_t packar SEX namn i en 8-byte-deklaration: DECL_HEADER (17 bitar)
  plus fem falt a NAMEID_BITS = 9, alltsa 62 av 64. Inget av falten borjar pa en
  byte-grans -- de sitter pa bit 17, 26, 35, 44, 53 och stracker sig over tva
  eller tre byte var.

  Sattaren ar en switch med sex armar, och varje arm ar en LAS-MODIFIERA-SKRIV
  av ett 9-bitarsfalt pa en maskin med 1-bits skiftare och ingen 32-bitars ALU.
  Disassemblyn sager det rakt ut: 298 instruktioner, bara 11 av dem anrop, och
  ~94 ar andi/or/ldd/std/lsr/bst/bld/swap -- ren mask- och skiftaritmetik.

  Lassidan bevisar samma sak oberoende: csp_states_name ar 202 B for en switch
  som BARA plockar ut ett falt.

  ATGARDAT 2026-09-09, och formatet behovde INTE andras. Slot k borjar pa bit
  8 + k*9 -- probat, inte antaget -- sa sex konstanta armar kunde bli en
  generisk bit-loop i src/csp_states.c. Matt pa uno exec:

    add_state         516 -> 318
    csp_states_name   202 -> 0    (borta)
    nya decl_bits_get/set          194
    ------------------------------------
    imagen          37954 -> 37758   = 196 B

  ALLTSA MYCKET MINDRE AN OMRADET SPARADE. Accessorerna gick fran ~718 B till
  194, men add_state bar 318 B eget arbete kvar (lookup_string, num_states,
  next_decl_index, csp_get_decl som returnerar 8 byte PA VARDE).

  Och 196 B mot ett gap pa 5698 B ar en avrundning. Det ar `main` (5706),
  `eval_op` (3340) och `csp_rebuild` (2032) som ar problemet -- den har posten
  var ratt att fixa, men den ar inte dar gapet stangs.

  COCO:S TRICK, om mer behovs harifran: lat ALDRIG ett falt korsa en bytegrans.
  get_capture skickar fyra 10-bitarsvarden som fyra LAGA BYTE plus en svansbyte
  med de fyra hoga bitparen. Motsvarigheten har vore sex namn-lagbyte plus en
  byte med de sex nionde bitarna -- da blir accessorn en byteladdning och ett
  bittest. Men slot 0 ar DECL_COMMONs `name`, sa det bytet trafffar ALLA
  deklarationer, inte bara states.

## CoCo som CandySpeak (examples/coco/, skrivet 2026-09-09)
  Tre saker mappar rakt over fran apps/pdb, och en gor det inte:

    objektkatalogen  index:subindex -> 32-bitarsvarde AR en #param-vag.
                     `Up1` ar SET/GET(INDEX_ADC_UPPER, 1), och den ar redan
                     persistent, listad och sattbar vid en prompt.
    CAPTURE          iodata_t (flaggor + varden) AR en #buffer med #field.
    vackningen       ett villkor per pinne AR `changed(D0) && mask` och en
                     jamforelse pa en analog.

    SPI-RAMNINGEN    mappar INTE, och behover inte: originalet ar ett
                     byte-i-taget master-klockat utbyte med en svarskod i byten
                     EFTER kommandot. Med en transport ar posten ramen, och
                     bade SYN/ACK och det andra utbytet forsvinner. Vackningen
                     BLIR sandningen -- ingen avbrottspinne, ingen pollning.

  KVAR: paret kompilerar men jag fick inte en stimulus-korning att fyra av.
  `/live` FRYSER reglerna, sa poke ar fel verktyg; det ar `-F` som skriver parts
  med reglerna igang. Nasta steg dar, inte en bugg.

  UART UTAN SOCAT: ett korsat pty-par racker, se scratchpad/wire.py-receptet --
  tva pty:er och en pump som kopierar masters till varandra. socat finns inte pa
  den har maskinen.

## Routing ar Erlangs distribution i litet (Tony sag det 2026-09-09)
  Samma tva drag, och Tony har byggt bada forut:

  1. UPPSATTNING I DET UTTRYCKSFULLA LAGRET, DRIFT KORTSLUTEN I DET SNABBA.
     Erlang gor handskakningen i Erlang-kod och lamnar sedan over till porten;
     efter det gar meddelandena rakt genom drivrutinen utan Erlang per
     meddelande. `#route` ar exakt det: regler gor select/ack, och sedan bar
     rutten datat utan en regel i mitten.

  2. ANDPUNKTEN AR POLYMORF. Erlang kan ha en PORT eller en PROCESS som
     distributionscontroller (dist_ctrl_get_data/put_data, som inet_tls_dist
     anvander). route_pull/route_push dispatchar pa transport pa precis samma
     satt -- console, repl, udp, tcp, uart, can -- och rutten bryr sig inte.

  DET SOM SAKNAS, och Erlang visar formen: en rutt som INSTALLERAS vid
  uppkoppling, inte deklareras. Idag ar `#route A B` statisk. net_kernel valjer
  dist-modul och kopplar upp nar handskakningen gick igenom; motsvarigheten har
  ar `sys.Console = 5` -> runtime installerar rutten. Det ar samma sak som
  paketeringsavsnittet nedan efterfragar, sett fran andra hallet.

  TVA STALLEN DAR LIKNELSEN SLUTAR:
  - Erlang har en HANDSKAKNING MED COOKIE. Vart select har ingen autentisering
    alls. Pa en CAN-buss ar det rimligt; pa UDP broadcast ar det inte det.
  - Erlang har busy_dist_port och SUSPENDERAR avsandaren. Vi har inga processer
    att suspendera, sa vi valde "blockera aldrig, behall i mottagaren och forsok
    nasta cykel". Samma avsikt, annan mekanism.

## Hur fjarrkonsolen borde PAKETERAS (Tony fragade 2026-09-09)
  Idag ar relayet 6-10 handskrivna regler per nod plus en ramlayout man kan fa
  fel (och fick fel: kommandobyten framfor payloaden skrevs over av payloaden).
  Ingen ska behova skriva det per kort.

  DEN AVGORANDE OBSERVATIONEN: fjarrkonsolens varde ar som STORST precis nar
  programmet ar fel. Ligger relayet i programmets regler tar ett trasigt program
  konsolen med sig -- och da ar man tillbaka vid ISP-sladden. Det ar samma
  argument som gjorde att escape-tecknet maste ligga i C.

  Darav uppdelningen:

  SLAVSIDAN I C, och rest FORE programmet. En nod ska ga att na aven med tomt
  eller trasigt program. Bindningen hor hemma i BOARD-TERMS, inte i programmet
  -- det ar kortet som vet hur det ar natt:

    {console_link, {can, 16#7E0}}      %% eller {udp, 5000} / {tcp, 5000}

  -> genereras till csp_board.h -> runtime reser det i csp_setup. sys.Id ar
  redan adressen, sa select-protokollet behover ingen ny identitet.

  MASTERSIDAN SOM PROGRAM. Operatorens nod ar den man kan laga, sa den far vara
  regler. Och det ar dar valet av nod och eventuell egen policy hor hemma.

  I SYS-MODULEN, som Tony foreslog, blir ytan:

    sys.Console = 5     %% driv nod 5 (0 = ingen).  MASTERN satter denna
    sys.Remote          %% 1 medan nagon driver MIG.  Slaven satter, laser man

  Alternativet -- ett bibliotek i CandySpeak, lib/console.csp, konkatenerat via
  PROG som redan ar en LISTA -- kostar noll C och samlar ramlayouten pa ett
  granskat stalle. Men det loser inte "noden med det trasiga programmet", sa det
  ar ett komplement, inte svaret.

  KVAR ATT LOSA FORST, bada nedskrivna nedan: genomstromningen (rutten som
  runtime-objekt i stallet for en regel i mitten) och ramningen.

## TCP och UART som transporter (parkerat 2026-09-08)
  TCP BYGGT 2026-09-09. UART kvar. Vad TCP blev:
  - Samma yta som UDP, `tcp <port> [<ip>]`, och ETT grammatikblock for bada med
    nyckelordet som en choice -- tva separata block lat `udp 2 tcp 1` matcha
    tva ganger och rakningen se en. Nu fangar svans-kollen det i stallet.
  - Adressen bar samma tva betydelser: peer att acceptera pa `in`, destination
    pa `out`.
  - EN anslutning per port, allt ickeblockerande, EOF stanger och gar tillbaka
    till att lyssna -- sa en peer som startar om blir betjanad igen.
  - TVA saker som skiljer sig fran UDP och som bada kostade en bugg:
    flaggorna far INTE nollas fore en misslyckad send (en strom far inte tappa),
    och en `out`-buffert maste RINGA UPP fran forsta cykeln. Utan det oppnades
    socketen av forsta byten nagon skickade, skrevs in i en halvoppen socket och
    forsvann -- `sys.Serial` kom fram som `Serial`.
  - Kvar: en post delad over tva TCP-segment kommer fram delad. `.dlc` visar
    det. Langdprefix eller separator ar det som gor dem hela, och ingetdera
    ar byggt.

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
