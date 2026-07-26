# FIXES

## FAILSAFE = ett #module (INSIKT, Tony 2026-07-26) -- omformar trappan nedan
  Gör FAILSAFE till en `#module FAILSAFE` istället för ett `#in FAILSAFE`-block.
  En modul ÄR den självförsörjande enheten: egna decls, egen kod (ENTER..LEAVE),
  egna states, egen #in INIT, per-instans-lagring.
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
    NOLL eval-loop-ändringar. ALL ROM-maskineri (verify/recovery/version) återanvänds
    rekursivt.
  - Implicit SINGLETON: instansieras ej som objekt; runtime REBASAR på den (som på
    ROM). Dess #in INIT kör setup.
  - CONSTRAINT (möjliggöraren): FAILSAFE får INTE referera globala/andra-objekt-
    fält -> kompiletids-fel. Rebasen är giltig bara om skivan är självförsörjande.
  - FAILSAFE re-deklarerar sina egna pinnar (egen decl, samma fysiska pinne; main+
    FAILSAFE kör aldrig samtidigt). Pin-konflikt-check måste tillåta det / ej
    cross-checka (FAILSAFE kompileras som egen enhet).

  NYTT JOBB (under ytan):
  1. RUNTIME-REBAS-PRIMITIV: csp_load_rom på en GIVEN image-pekare (ej bara den
     länkade rom_header). Förbereder BÅDE FAILSAFE-växling OCH ROM-recovery-fuzzning
     (peka på korrupt kopia). Oberoende värdefullt -> gör detta först.
  2. AKTIVERINGS-MODELL: main kör normalt, FAILSAFE vilande/redo; vid fel/Panic/
     watchdog -> växla (repoint+rebuild). Boot: main failar verify -> boota FAILSAFE.
  3. LOKALISERA FAILSAFE vid korruption: namnet ligger i str. Reserverad modul-
     markör/flagga på DECL_MODULE (eller: FAILSAFE är en egen image -> hittas via
     sin egen rom_fs_header, ingen namn-matchning behövs).
  4. KOMPILERING (kan vänta): csp -C riktad på BARA #module FAILSAFE -> rom_
     failsafe.c (egen strängtabell, egna decl-index, eget pin-space). Återanvänder
     generatorn. Syntax/semantik oförändrad.
  KONSEKVENS: crc_failsafe, FAILSAFE_INIT, eget segment -> allt kollapsar till
  "FAILSAFE = andra ROM-image + rebas-primitiv". str+state self-verify KLART.

## FAILSAFE-som-recovery-target: hela trappan (2026-07-26)
  MÅL: vid korrupt ROM (header/kod/decls) hoppa till en verifierad FAILSAFE
  istället för dead/park. FAILSAFE måste kunna köra ISOLERAT -- den behöver sin
  EGEN skiva av instr + decls + str (inte bara koden). Segmentering är slut-
  svaret; per-sektions self-verify är substratet vi bygger först.

  INSIKT (Tony): om str-arean är OK kan FAILSAFE PRINTA en diagnostik ("FAILSAFE:
  sensor X fault") -- slår en tyst blink. Ger en extra degraderings-pinne: str OK
  men kod korrupt -> skriv ut vad du kan innan park (Erlang: logga + reboota).

  Trappan:
    header OK, sektioner OK          -> kör normalt
    header-crc rutten, END-markörer  -> kör normalt              KLART
    sektion korrupt, FAILSAFE-seg OK -> hoppa FAILSAFE           (kräver segment)
    ROM-FAILSAFE korrupt, EEPROM-patch OK -> den                (framtid)
    allt korrupt                     -> park (UART + watchdog)

  Steg (ordnade):
  1. Per-sektion self-verify KLART för ALLA FYRA sektioner (se DONE 2026-07-26):
     decl/instr END-markörer + str 0xFF-sentinel-trailer + state sentinel-state.
     Recovery header-oberoende. Substratet klart.  -- KLART
  2. crc_failsafe -> OMTOLKAT som MODUL-slice-crc (se insikten ovan): CRC över
     FAILSAFE-modulens decl-range (DECL_MODULE.n) + instr-range (OP_ENTER.num) +
     refererade strängar. Modulen är självdelimiterande -> inga nya markörer.
  3. FAILSAFE_INIT -- STRUKET. Modulens egna #in INIT tar det (gratis).
  4. FAILSAFE-modulens aktiverings-omkopplare ("takeover") + lokaliserings-markör
     (reserverad flagga på DECL_MODULE, eftersom namnet ligger i str). Se insikten.
  5. EEPROM-FAILSAFE-patch (andra kopian; max ETT ROM men en patch tillåts),
     park-fallback (UART + watchdog reboot).
  Se DONE: END-markörer, crc_failsafe-noten. Max-ett-#in-FAILSAFE finns redan
  nedan.

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

## Korrupt #disable-set i EEPROM => FAILSAFE i st f ROM? (2026-07-24)
  Idag: crc_dis-miss vid load -> avvisa HELA saven, fall tillbaka till ROM-
  baslinjen (se DONE + doc/DISABLE.md). Rimligt default (ROM = betrodd), MEN:
  disable-seten kan ha stangt av en FARLIG ROM-regel. Faller vi da tillbaka
  till "bara ROM" ATERAKTIVERAS just den regeln -> osakert. For en korrupt
  disable-set specifikt ar sakraste destinationen kanske FAILSAFE (reserverat
  sticky state), inte ROM. Vantar pa FAILSAFE park-fallback (se failsafe-noten).
  Dokumenterat som oppen fraga i doc/DISABLE.md "Failure mode".

## END-markörer => scan-baserad återhämtning vid korrupt header (2026-07-24)
  IDÉ (Tony). Idag: crc_hdr täcker räknarna (n_str/n_decl/n_instr/n_state) OCH
  sektions-CRC:erna. Failar crc_hdr avvisar vi HELT -- vi vågar inte lita på
  räknarna, så vi vet inte hur långa sektionerna är och kan inte ens beräkna
  sektions-CRC:erna. Metadata och innehålls-integritet sitter ihop.

  Förslag: frikoppla dem med END-markörer så längderna kan ÅTERSKAPAS genom att
  skanna uppifrån-ner, oberoende av headerns räknare. Då blir det:
    crc_hdr OK                    -> snabbvägen som idag.
    crc_hdr FAIL men skannade
      längder + sektions-CRC:er
      matchar                     -> DATAN är intakt, bara metadatan/crc_hdr
                                     ruttnade. KÖR med varning ("header CRC
                                     corrupt, sektioner verifierade via scan").
    crc_hdr FAIL + sektion X:s
      CRC mismatchar              -> sektion X (eller dess lagrade CRC) trasig
                                     -> avvisa/degradera som idag (för graf:
                                     sekventiell fallback, redan gjort).

  Vad som krävs:
  - decls: DECL_END finns redan (n_decl räknas med terminatorn).
  - strängar: längd-prefixade, självdelimiterande (slutar 0,0). OK.
  - INSTRUKTIONER: saknar terminal markör. Behövs en OP_END (eller motsvarande)
    så en scan kan hitta instruktionsantalet utan headern.
  - states: saknar terminator; antingen egen END eller förbli header-beroende
    (states är sist, minst kritiskt).

  Fällor att respektera:
  - Scanen MÅSTE fysiskt bindas av arrayens compile-time-storlek (rom_instr[N]);
    ALDRIG lita på en räknare. En flippad byte som förfalskar/raderar en END-
    markör får aldrig läsa förbi arrayen.
  - En flippad DATA-byte kan skapa en falsk END -> fel längd -> sektions-CRC
    matchar inte -> vi avvisar (korrekt utfall). Mekanismen gör aldrig något
    VÄRRE, den lägger bara till återhämtning i "bara-headern-ruttnade"-fallet.
  - Mest värdefullt för EEPROM (muterbar, partiella skrivningar, bit-rot). För
    ROM/flash betyder korrupt header oftast trasig flash eller fel version --
    överlappar version-avvisningen, mindre att vinna. Börja med EEPROM.

## Max ETT #in FAILSAFE block (2026-07-24)
  FAILSAFE-mekaniken finns (reserverat sticky state, #in FAILSAFE-block, se
  DONE). Kvar: parsern tillater flera #in FAILSAFE. Ska vara max ETT sa det ar
  entydigt utpekbart och verifierbart. csp_parse_in: om state==FAILSAFE och ett
  FAILSAFE-block redan finns -> fel. (Del av failsafe-arbetet; crc_failsafe och
  park-fallback aterstar ocksa -- se DONE-noten.)

## CRC: kvarvarande luckor + emitter-buggar (2026-07-23)
  Full ROM-CRC + EEPROM data_crc KLART (se DONE). Graf-CRC, rom_states-CRC
  (state_t PACKED) och #buffer-emittern ar OCKSA klara (se DONE 2026-07-24).
  Kvar:
  - GENERATOR-NORMALISERING KOPPLAD TILL EMITTERN: rom_image_crc folder
    rom_decl som den ar; generatorn maste nolla exakt de falt emittern INTE
    skriver (is_mapped/bound/reg + timer fired/running/_res). Andras emittern
    maste normaliseringen folja med, annars tyst CRC-miss -> ROM rejected.
    Sarbar. Ev. gor en delad rom_decl_canonical(d) som BADA anvander.
    (Nu extra relevant: #buffer-grenen lades till -- bf har inga runtime-scratch
    utover common is_mapped/bound/reg som redan nollas, sa den var safe, men
    monstret ar fortfarande sprott.)

## Listing of RAM rule, list as [ROM]

## `<-` och changed() fastnar på FÖRE-värdet när källan ändras exakt en gång.
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
  (`fresh = changed(A)`) -- den fördröjs lika mycket som värdet och hamnar i
  fas. Se examples/can_input.csp.
  AVGJORT (Tony, 2026-07-18): det här är INTE en bugg och ska inte "fixas".
  Semantiken är att input är input, och att man inte läser output förrän nästa
  cykel. Att `<-` fyrar på ändringen och läser förra cykelns värde är den
  regeln tillämpad konsekvent, inte ett undantag från den. Att låta `<-` läsa
  DOUT vore att böja semantiken för att ett testfall ska se snyggare ut.
  Står kvar här som DOKUMENTERAD konsekvens, inte som en åtgärd: engångsfallet
  är det som förvånar folk, och `fresh = changed(A)` är mönstret som löser det.
  OBS: att `rb = fa` (utan `?` och utan `<-`) inte fyrar reaktivt är av samma
  skäl korrekt -- det reaktiva ligger bakom `?`, och `X <- Expr ? Cond` tar med
  variabler i både Expr och Cond i kanterna. tests/unit/can_pack är seq-only av
  just det skälet: den använder vanliga `=`-regler.

## Villkor droppas TYST vid parse-fel i guarden.
  `Q = 1 ? undefinedname` och `Q = 2 ? A &&` svarar båda "OK" och lagras som
  OVILLKORLIGA regler (`Q=1`, `Q=2` i /list). En stavfel i en guard gör alltså
  en villkorad regel alltid-på -- tyst. Troligen samma rot som [ROM]/[RAM]-
  listningsbuggen överst: guard-delen tappas någonstans mellan parse och
  emission istället för att sätta ERR_SYNTAX.

## Stack/arena-marginalen: overvaka, ev. varna (2026-07-23)
  LOST: CSP_STACK_RESERVE 512 -> 2048 (se DONE). margin ar nu +617 med cpx.csp.
  Kvar som forbattring, inte bugg:
    - Lat /memory VARNA (inte bara visa) nar margin kryper under t.ex. 256 -- en
      rad "WARNING: stack near arena" sa man ser det utan att lasa siffran.
    - 2048 ar matt mot cpx.csp + korta REPL-rader. Ett program med moduler eller
      djupt nastlade uttryck kan ga djupare an push_imm gjorde. margin-raden ar
      livechecken; om den gar negativt pa ett riktigt program, hoj mer eller
      krymp djup-vagen (nasta kandidat: de tva nastlade token_t tv[24] i
      csp_process_line + csp_parse, ~288 byte -- linjen ar redan tokeniserad en
      gang, andra passet ar redundant).

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

## .stdin-stödet sitter i test.sh, men `make test` kör run_tests.escript.
  test.sh fick `<test>.csp.stdin` (pipas in i REPL:en), men den harnessen körs
  inte av `make test` -- escripten gör det, och den matar ingen stdin. Så
  tests/unit/module_abort kontrolleras bara av `bash test.sh`; under `make test`
  körs dess .csp utan kommandon och bevisar ingenting.
  Att göra: flytta .stdin-stödet till csp_test.erl så det finns EN harness.
  Då går EEPROM-round-trip (/save + /load) också att testa.

## Legacy CAN-hantering BORTTAGEN (2026-07-18) -- ska tillbaka på modellen
  Borttaget: csp_parse_legacy, make_can_rule, make_can_range, lookup_can_range
  och dispatch-grenen för `<int> <int> ...`-rader (~183 rader). Formatet var
    0x218 0 0x01 0x01 0x00      // <frame-id> <byte> <mask> <on> <off>
  som genererade regler `OUT = k ? frame[bit] == c` med syntetiska anonyma
  bit-vyer. Det band mot ett konstant-index för frame-id:t, vilket inte längre
  finns -- fält binder mot en deklarerad #buffer nu.
  Sparad kopia av den borttagna koden finns i git-historiken.
  Att göra: när frame-modellen + syntaktiskt socker är klart, lägg tillbaka
  motsvarande bekvämlighet ovanpå den (det blir enkelt då -- en tabellrad som
  expanderar till vanliga regler mot namngivna fält).
  OBS make_buf_view hör INTE hit (den driver Buf[a..b]) och är kvar.

## Buf[a..b] inne i en modul -- DECL_VIEW skapas som modulmedlem?
  make_buf_view anropas från uttrycksparsningen och gör csp_new_decl, som
  lägger decl:en på st->ps.nd -- alltså INNE i modulkroppen om man är mitt i en
  modul. Uppsättningen i csp_rt_start hanterar DECL_VIEW bara i globalpasset
  och slår upp föräldern med rått decl-index. Misstänkt trasigt per instans,
  på samma sätt som #buffer var. EJ verifierat -- konstruera ett test först.

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
  - CAN FD har diskreta DLC-värden (0-8,12,16,20,24,32,48,64). `.dlc` klampas
    bara till nbytes, den rundar inte upp till nästa giltiga FD-längd. Spelar
    ingen roll så länge Linux-backenden kör classic can_frame (max 8).
  - Arduino-backenden (arduino-CAN bakom CSP_HAS_CAN) är INTE körd på järn.
    Linux/vcan0 är verifierat i båda riktningarna.


# COOL STUFF

## POKE-PROPAGERING + REGEL-TRACE (debug-verktyg, drömt fram 2026-07-17)
  Idé: i /live-läge, poka ett värde och kör BARA de regler som beror på det --
  inget annat. Motorn gör redan 90%: en manuell tilldelning skulle anropa
  csp_enq_elist(ix) (köar beroende regler i pending-bitsetet) följt av EN
  csp_react(st) (drainar och kör dem + deras kaskad). Kräver att immediate-assign
  (csp_process_immediate / csp_dio_set-vägen i live) routas via enqueue istället
  för bara sätta värdet.
  Användningsfall (Tonys): en regel du trodde skulle fyra gör inte det (inget
  pling). Du tittar: "Led = 1 ? BtnA && X > 7". Du kollar X, sätter > X = 8, och
  ser om regeln fyrar nu. Interaktiv trigger-felsökning.
  TRACE ovanpå (fristående, gör FÖRST -- nyttigt i vanligt kör-läge också):
  /trace on|off. csp_react dequeuear regel-ordinal -> rule_ip -> kör; där, bakom
  flaggan, printa vilken regel som fyrar. Billig variant: regel-index/ip. Snygg
  variant: kör exprbuf-disassemblern (funkar nu) -> full regeltext.
  ÄRLIGA BEGRÄNSNINGAR: (1) timers/timeout(T) fyrar INTE av en poke -- triggern
  är timern, inte ett värde; behöver riktig tid (csp_input_timer). Poke når allt
  som hänger på VÄRDEN, inte det som väntar på TID. (2) states/#in gate:ar rätt
  (regel bakom State==ON fyrar bara om staten matchar) -- funkar, men man styr
  staten genom att poka State också.
  Hooks finns: csp_enq_elist, csp_react, rule_ip, exprbuf (regeltext), st->live.

## Array notation  (nästa release)

  Kan återanvända OBJEKT-kodningen -- en array är nästan samma sak: index_t är
  redan (obj, index) och st_index() gör offs[OBJ(n)] + INDEX(n), dvs precis
  "bas + element". Ett array-element är en objekt-instans med en medlem.
  Runtime-index (Acc[INDEX]) är då detsamma som att välja objekt vid körning,
  vilket OP_NEW/CURRENT redan gör -- st->cur + offs[CURRENT] är mekaniken.
  MEN det reaktiva måste fixas först: grafen har en kant per DEKLARATION, och
  Acc[INDEX] beror på ALLA element (vilket som läses avgörs vid körning). Antag
  antingen kant till hela arrayen (grovt men korrekt) eller enq per element.

  #digital D[5] in 0:1..5,7,9,13,15,17
  #variable Acc[3]
  #analog A[3]:10 in 0:1..3
  #variable INDEX = 0
  #timer Td 1000

  Acc[INDEX] <- Acc[INDEX] + A[INDEX]
  INDEX <- (INDEX + 1) % 3 ? timeout(Td)

Semantik view (expanded)

   #digital D0 in 0:1
   #digital D1 in 0:2
   #digital D2 in 0:3
   #digital D3 in 0:4
   #digital D4 in 0:5
   #variable Acc0
   #variable Acc1
   #variable Acc2
   #analog A0 in 0:1
   #analog A1 in 0:2
   #analog A2 in 0:3
   #variable INDEX = 0
   #timer Td 1000

   Acc0 <- Acc0 + A0 ? Index==0
   Acc1 <- Acc1 + A1 ? Index==1
   Acc2 <- Acc2 + A2 ? Index==2
   Index <- (Index + 1) ? Timeout(Td)

## Interrupt

Can we run CandySpeek rules during interrupt,
is it possibel / feasible. At least have rules
that trigger on digital state change, analog sample compleation...

What about using states?

#in ISR
  Buffer[I] = CREG
  I = I + 1
  State = RTI
#end

"We could queue interrupts (when possible) serv som interrupts
when needed in real isr, but queue the facr. Then schedule to
run ISR rules in ISR state until interrupt stack is empty!
(could work) then we would have minor troubles without running
runtime during interrupt time."

- atomic keyword

## Other

- How to combine ROM + RAM => new ROM base?

- ROM disable flag to kill off REAL firmware.

