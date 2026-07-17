- exit csp_linux after -d and -n or no program / no interaction

- display available EEPROM memory

- support EEPROM library for SAMD?

- Array notation  (nästa release)

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

- Memory

How can we use all available memory to rules and declarations
without affecting overhead?

- Interrupt

Can we run CandySpeek rules during interrupt,
is it possibel / feasible. At least have rules
that trigger on digital state change, analog sample compleation...

- How to combine ROM + RAM => new ROM base?

- ROM disable flag to kill off REAL firmware.

- Optimse rules. print rule then parse ?

- man borde kunna köra value och pin init i INIT också
  (kanske lägga initiering som kod istf i deklarations ?)

- /list disassembler: GJORT (2026-07-16). fcall index-argument (timeout(T) m.fl.)
  round-trippade variabelindexet genom en RENDERAD tal-sträng, vilket sprack när
  CURRENT-relativa index passerade int16 ("-2035" / "0xF80D" -> skräp-index ->
  SIGSEGV). csp_exprbuf_t bär nu råvärdet parallellt (regi[]/argi[], satt i
  OP_LI/OP_LIU, kopierat i OP_ARG) och exprbuf_fcall tar argi[i] direkt.

- /memory: visa HELHETEN för embedded (Tonys önskemål).
  Hookarna finns redan i csp_arduino.c: getTotalRAM() (per board-macro),
  freeRam() (sbrk på ARM, __brkval på AVR) och stack_used(). Saknas: en
  kärn-hook (csp_ram_total/csp_ram_free) så csp_rt.c kan visa dem, plus en
  linux-implementation (simulerad, jfr -E).
  Rader att visa:
    MCU:        RAM <total>  EEPROM <cap>  Flash <total>
    CandySpeak: kod-pool <CSP_CODE_BUDGET>, härlett <faktiskt>, reserv <stack>
  RESERV-FAKTOR (justerbar parameter): de härledda tabellerna mallocas ur det
  FRIA utrymmet, inte ur poolen -- och kön ensam kan vara 2 KB. Mätt på mega:
    budget  512 -> globals 3908 (47%), fritt 4284
    budget 1024 -> globals 4420 (53%), fritt 3772
    budget 2048 -> globals 5444 (66%), fritt 2748
    budget 3072 -> globals 6468 (78%), fritt 1724   <- kön får inte plats
  Utan reserv-räkning går CSP_CODE_BUDGET inte att välja per bräda på annat
  sätt än att mäta. Makefile.mega står på 1024 provisoriskt.

- EEPROM för SAMD: GJORT (2026-07-16) men EJ KÖRD PÅ HÅRDVARA.
  mkrzero/cpx har ingen EEPROM; nu emuleras en i en reserverad flash-region
  (csp_arduino.c, CSP_HAS_FLASH_EEPROM). Kostar 296 B RAM: 256 B row-buffer +
  FlashClass-objektet. Läsning kostar 0 (SAMD-flash är minnesmappad -> memcpy).
  Verifierat: bygger, och symbolerna visar eeprom_region 2048 B i FLASH,
  ee_row 256 B i RAM. RUNTIME-VERIFIERING KRÄVER BRÄDA: en (1) /save + en (1)
  /load, aldrig i loop.
  Varför inte bibliotekets FlashAsEEPROM: den håller RAM-skugga av HELA regionen
  (byte data[1024]) för att dess API tillåter spridda skrivningar med uppskjuten
  commit => read-modify-write. Vi strömmar sekventiellt och skriver om hela
  avbilden varje gång => radera up-front, buffra en row. Bara att LÄNKA
  biblioteket drog dessutom in dess `EEPROMClass EEPROM`-global = 1027 B RAM vi
  aldrig rör (statisk konstruktor överlever --gc-sections, samma fälla som weak
  rom_*). Därför är FlashClass vendrad som csp_flash_samd.{h,cpp} (LGPL-notisen
  kvar + proveniens); FlashAsEEPROM medvetet utelämnad.
  KVAR: SAMD51 har 8192-byte erase-granularitet -- regionen måste vara 8K-alignad
  där; SAMD21 (mkrzero) är 256 och berörs inte.
  Wear: EJ prioriterat, och ingen /save-varning heller (Tony: "Glöm Wear" /
  "vi struntar i varningen, det är ju ändå bara leksaker än så länge").
  OBS: kör ALDRIG stress-/loop-tester mot EEPROM eller flash.

- LICENSE-fil saknas i repot. Tony kör GPL 2/3 på sina projekt men det är inte
  deklarerat någonstans (ingen LICENSE, inga headers). Nu när LGPL-kod är vendrad
  (csp_flash_samd.*) bör GPL:n faktiskt skrivas ut -- LGPL-2.1 §3 tillåter att en
  kopia används under vanlig GPL, men det förutsätter att projektets licens finns.

- EEPROM-round-trip-test saknas i suiten -- DÄRFÖR slank en allvarlig bugg
  igenom (block-write/read av ram_decl efter att decl blivit nedåtväxande =
  läste/skrev förbi arenans topp). Verifierat manuellt, men csp_test.erl kan
  inte mata REPL-kommandon (-I är en per-cykel INPUT-fil, inte kommandon).
  Behövs: harness som kan köra /save + /load, eller ett shell-test.

- Reaktivt: regel tillagd UNDER DRIFT wire:as inte in i grafen.
  csp_process_persistent kör om rt_start vid decl-add, men för en ren
  regel-add när den inte är pausad står det "no rebuild needed, running
  state kept (fast interactive paste)" -- och csp_csr körs alltså inte.
  Sekventiellt spelar det ingen roll (csp_eval scannar alla instruktioner),
  men reaktivt kör csp_react BARA köade regler, och kön matas ur edg. Utan
  om-csr får den nya regeln inga kanter => fyrar aldrig.
  Syns i /memory: lägg till en regel under drift och instr växer (189->194)
  medan "reactive: rules=" står stilla.
  OBS: konsekvensen är INTE demonstrerad -- mitt försök att visa den var
  ogiltigt (REPL:en kör ingen cykel mellan kommandon, så varken den nya
  eller en fil-deklarerad regel re-fyrade). Verifiera först med en riktig
  cykel-drivande uppställning (-c/-F som testsuiten) innan fix.
  Fix, om den bekräftas: kör csp_csr även för regel-add (eller markera
  edited och gör det lazy vid nästa cykel).

- AVR: kärnan (csp.h/csp_rt.c) kompilerar nu med SAMMA bit-bredder som host
  (DECL_BITS/INSTR_BITS=11) -- de kostar inget RAM längre. Kvar för att
  faktiskt bygga UNO igen är DRIVRUTINEN: CandySpeak.ino:326 använder
  INPUT_PULLDOWN som bara finns på SAMD. OBJ_BITS/STRING_BITS är medvetet
  kvar små på AVR (de sizar offs/object/module resp. ram_str+exprbuf).
  AVR har CSP_CODE_BUDGET=512 B.

- MÄTT (2026-07-16), viktigt underlag för reserv-faktorn: /memory visar nu
  "derived" = vad de härledda tabellerna faktiskt kostar. För cpx_m:
    arena (kod)  988 byte
    derived     4420 byte   <- FYRA GÅNGER koden
  varav kön 2048 (46%), buf-tabellen 840, view 600, heap 560.
  Konsekvens: cpx_m får INTE plats på en mega (3772 byte fritt). Det är alltså
  inte kod-poolen som binder på små bräder -- det är de härledda tabellerna.
  Köns tighta gräns (nedan) är därmed den enskilt största vinsten.

- Kön BORTTAGEN (2026-07-17). Ersatt av två bitset över (ordinal,objekt) --
  kön och inq bar samma information, kön lade bara till en ordning, och det
  var FEL ordning (ändringsordning, inte regelordning) => se rule_order-testet.
  Bitsetet kan inte spilla över, så det tysta droppet är borta by construction.
  KVAR (nu en ren storleksfråga, inte korrekthet): nyckelrymden är
  n_rule << OBJ_BITS, dvs den reserverar alla 32 objekt-slots per regel fast
  en regel bara kan köas för sin egen moduls instanser. Tight vore en per-modul
  bas (som offs[] gör för leaves): rymden blir då summa över objekt av (regler i
  dess modul) = exakt D. För cpx_m: 200 B -> ~70 B. Värsta fallet slutar skala
  med produkten n_rule x MAX_OBJECTS.
 