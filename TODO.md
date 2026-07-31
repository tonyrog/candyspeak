# TODO

Prio-ordning, inte kronologisk. Grovt: först det som kan gå sönder ute på ett
kort, sedan det som låser upp resten, sedan FAILSAFE-trappan, sedan bekvämlighet
-- och de coola grejorna sist.

Klart-markerat flyttas till DONE.md, inte hit.


# 1. RÄTTELSER SOM BITER I FÄLT

## Emitter/CRC-kopplingen: normaliseringen måste delas (2026-07-23, aktualiserad 2026-07-31)
  `csp_dump.c` folder decl-sektionens CRC över `csp_get_decl()` med en HANDSKRIVEN
  lista av runtime-scratch-fält nollade (`is_mapped/bound/reg`, plus timerns
  `fired/running/_res`). Ändras emittern måste den listan följa med, annars blir
  det en tyst CRC-miss -> "ROM rejected" vid boot.

  DET HÄNDE (2026-07-31): instruktionssidan hade INGEN normalisering alls -- den
  antog att emittern skriver varje fält i armen. Den gjorde inte det: `.y` på
  ST/LD/STIMP/CHG, `.z` på arity-1-ALU och `.implicit` på OP_RULE och
  OP_INSTATE/NINSTATE föll bort, så en genererad bild med REGLER underkände sig
  själv vid boot. `.implicit` var dessutom en riktig funktionsförlust: den
  markerar den automatiska NORMAL+-wrappen runt en bar toppregel.

  Aldrig sett på hårdvara enbart för att båda `rom.c` i trädet var tomma bilder.

  Att göra: en delad `rom_decl_canonical(d)` OCH `rom_instr_canonical(i)` som
  BÅDE generatorn och verifieraren kallar. Då är det omöjligt att de driver isär,
  i stället för osannolikt. Allt i FAILSAFE-trappan står på den här grunden.

## State i EEPROM -- de halvfärdiga .sys-bitarna (2026-07-30)
  Tre symptom, en rot. `State` är en systemvariabel: den ska ligga i RAM och
  ALDRIG i EEPROM. Halva vägen dit är gjord (`CSP_BASE_*`, `sys_nd/nn/strp`),
  `.sys`-fältet på deklarationer påbörjat men index-mappningen kvar.

  - Ett lyckat `/save` persisterar State**s värde**. Sparar man medan man står i
    FAILSAFE kommer man tillbaka dit vid nästa boot.
  - `/reset` lämnar inte FAILSAFE. Koden som skriver `STATE_INIT` i båda sloten
    finns men är verkningslös -- `/state` läser State från något annat än den
    sloten.
  - `/save` räknar RAM-decls rått ("6 RAM decls" för tre användardeklarationer):
    State och ändmarkörer räknas med. Kosmetiskt, men det är samma räkning.

  Detta är den enda posten på listan som kan låsa ett kort i fält.

## #field mot en buffert utan transport ger nonsens (2026-07-31)
    #buffer B:4 out
    #field Bf:8 unsigned B[0..7]     -> Error: word  not a module
  Antingen ska det fungera (en vanlig RAM-buffert är en lika giltig vy-bas som
  en CAN-ram) eller ge ett begripligt fel. Felet som kommer nu är från en helt
  annan del av parsern.

## Buf[a..b] inne i en modul -- DECL_VIEW som modulmedlem?
  `make_buf_view` anropas från uttrycksparsningen och gör `csp_new_decl`, som
  lägger decl:en på `st->ps.nd` -- alltså INNE i modulkroppen om man är mitt i en
  modul. Uppsättningen i `csp_rt_start` hanterar `DECL_VIEW` bara i globalpasset
  och slår upp föräldern med rått decl-index. Misstänkt trasigt per instans, på
  samma sätt som `#buffer` var. EJ VERIFIERAT -- konstruera ett test först.

## Korrupt #disable-set i EEPROM => FAILSAFE i st f ROM? (2026-07-24)
  Idag: crc_dis-miss vid load -> avvisa HELA saven, fall tillbaka till
  ROM-baslinjen (se DONE + doc/DISABLE.md). Rimligt default (ROM = betrodd), MEN:
  disable-setet kan ha stängt av en FARLIG ROM-regel. Faller vi då tillbaka till
  "bara ROM" ÅTERAKTIVERAS just den regeln -> osäkert. För en korrupt
  disable-set specifikt är säkraste destinationen kanske FAILSAFE (reserverat
  sticky state), inte ROM. Väntar på FAILSAFE park-fallback.
  Dokumenterat som öppen fråga i doc/DISABLE.md "Failure mode".


# 2. INFRASTRUKTUR SOM LÅSER UPP RESTEN

## T_-prefix på tok_t (Tonys)
  Blockerar ESP32-bygget: `PULLUP`/`PULLDOWN` krockar med esp32-hal-gpio.h.
  Options är gjorda (`T_xyz`), resten kvar. Fälla att minnas: token-pasting
  (`op_##`, `f_##`) -- makron måste följa med i omdöpningen.

## Rebas-primitiv: csp_load_rom mot en GIVEN image-pekare
  Idag rebasar `csp_load_rom` runtime på den LÄNKADE `rom_image`. Gör den till en
  primitiv som tar vilken image som helst. Oberoende värdefull -- gör den först:
  - förbereder FAILSAFE-växlingen (steg 1 i trappan nedan),
  - gör ROM-recovery FUZZBAR (peka på en avsiktligt korrupt kopia och se att
    degraderingen faktiskt sker), vilket idag inte går att testa alls.

## En harness, inte två
  `.stdin`-stödet (`<test>.csp.stdin`, pipas in i REPL:en) sitter i `test.sh`,
  men `make test` kör `run_tests.escript` som inte matar stdin. Så
  `tests/unit/module_abort` bevisar ingenting under `make test`.
  Att göra: flytta `.stdin`-stödet till `csp_test.erl`.
  OBS: `tests/repl.sh` täcker numera EEPROM-rundturen (/save + /load), listnings-
  taggar, ROM-bilder och paste-vägen -- så det som faktiskt återstår är
  `module_abort`s stdin-fall.


# 3. FAILSAFE-TRAPPAN

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

## END-markörer => scan-baserad återhämtning vid korrupt header (2026-07-24)
  IDÉ (Tony). Idag täcker crc_hdr både räknarna (n_str/n_decl/n_instr/n_state)
  OCH sektions-CRC:erna. Failar crc_hdr avvisar vi HELT -- vi vågar inte lita på
  räknarna, så vi vet inte hur långa sektionerna är och kan inte ens beräkna
  sektions-CRC:erna. Metadata och innehållsintegritet sitter ihop.

  Förslag: frikoppla dem med END-markörer så längderna kan ÅTERSKAPAS genom att
  skanna uppifrån och ner, oberoende av headerns räknare:
    crc_hdr OK                          -> snabbvägen som idag.
    crc_hdr FAIL, skannade längder +
      sektions-CRC:er matchar           -> DATAN är intakt, bara metadatan
                                           ruttnade. KÖR med varning.
    crc_hdr FAIL + sektion X mismatchar -> avvisa/degradera som idag.

  Vad som krävs:
  - decls: DECL_END finns redan (n_decl räknas med terminatorn).
  - strängar: längdprefixade, självdelimiterande (slutar 0,0). OK.
  - INSTRUKTIONER: OP_END_MARK finns nu (self-CRC) -- kontrollera att en scan
    verkligen kan hitta antalet utan headern.
  - states: saknar terminator; antingen egen END eller förbli header-beroende
    (states ligger sist, minst kritiskt).

  Fällor att respektera:
  - Scanen MÅSTE fysiskt bindas av arrayens compile-time-storlek (rom_instr[N]);
    ALDRIG lita på en räknare. En flippad byte som förfalskar eller raderar en
    END-markör får aldrig läsa förbi arrayen.
  - En flippad DATA-byte kan skapa en falsk END -> fel längd -> sektions-CRC
    matchar inte -> vi avvisar (korrekt utfall). Mekanismen gör aldrig något
    VÄRRE, den lägger bara till återhämtning i "bara-headern-ruttnade"-fallet.
  - Mest värdefullt för EEPROM (muterbar, partiella skrivningar, bit-rot). För
    ROM/flash betyder korrupt header oftast trasig flash eller fel version --
    överlappar versionsavvisningen. Börja med EEPROM.


# 4. BEKVÄMLIGHET

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

## CAN, kvar att göra
  - CAN FD har diskreta DLC-värden (0-8,12,16,20,24,32,48,64). `.dlc` klampas
    bara till nbytes, den rundar inte upp till nästa giltiga FD-längd. Spelar
    ingen roll så länge Linux-backenden kör classic can_frame (max 8).
  - Konfigurera busshastighet från .csp? `#can speed 250000 ...`
  - Extended addresses: 29 bitar, så 0x80000000 kan signalera extended,
    0x40000000 rtr och 0x20000000 error frame. Behöver bussoptioner 1..4?

## Legacy CAN-bekvämligheten tillbaka ovanpå frame-modellen (2026-07-18)
  Borttaget: `csp_parse_legacy`, `make_can_rule`, `make_can_range`,
  `lookup_can_range` och dispatch-grenen för `<int> <int> ...`-rader (~183 rader).
  Formatet var
    0x218 0 0x01 0x01 0x00      // <frame-id> <byte> <mask> <on> <off>
  som genererade regler `OUT = k ? frame[bit] == c` med syntetiska anonyma
  bit-vyer. Det band mot ett konstant-index för frame-id:t, vilket inte längre
  finns -- fält binder mot en deklarerad `#buffer` nu.
  Att göra: när frame-modellen + syntaktiskt socker är klart, lägg tillbaka
  motsvarande bekvämlighet ovanpå den (en tabellrad som expanderar till vanliga
  regler mot namngivna fält). Sparad kopia finns i git-historiken.
  OBS `make_buf_view` hör INTE hit (den driver `Buf[a..b]`) och är kvar.


# 5. VERIFIERINGSSKULD

Inte buggar -- saker som byggts men inte setts fungera på järn.

- XOFF/XON (`serial_hold`/`serial_release`) syns bara på ett kort med RIKTIG
  UART. Över USB CDC blockeras värden av endpoint-backtrycket oavsett. Testa på
  mega: klistra in en längre fil i minicom med software flow control av och på.
- arduino-CAN:s globala `CAN` (SAMD/ESP32 on-die) är oprövad. MCP2515-vägen på
  rp2040 är körd mot riktig buss i tio timmar.


# DOKUMENTERADE KONSEKVENSER (inte åtgärder)

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

## Array notation (nästa release)
  Kan återanvända OBJEKT-kodningen -- en array är nästan samma sak: `index_t` är
  redan (obj, index) och `st_index()` gör `offs[OBJ(n)] + INDEX(n)`, dvs precis
  "bas + element". Ett arrayelement är en objektinstans med en medlem.
  Runtime-index (`Acc[INDEX]`) är då detsamma som att välja objekt vid körning,
  vilket OP_NEW/CURRENT redan gör -- `st->cur` + `offs[CURRENT]` är mekaniken.
  MEN det reaktiva måste fixas först: grafen har en kant per DEKLARATION, och
  `Acc[INDEX]` beror på ALLA element (vilket som läses avgörs vid körning).
  Antag antingen kant till hela arrayen (grovt men korrekt) eller enq per element.

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
  State-syntax för avbrott:

  #in ISR
    Buffer[I] = CREG
    I = I + 1
    State = RTI
  #end

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

## Systemvariabler, konfigurerbara
  Name = "Node1"
  ID   = 123
  IP   = 192.168.2.100

## Övrigt
  - Hur kombinerar man ROM + RAM => ny ROM-bas?
  - ROM-disable-flagga: ladda inte ROM-koden.
  - EEPROM-disable-flagga: ladda inte EEPROM-koden.
    Flaggorna lagras i EEPROM.
