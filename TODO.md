# TODO

Hur ska vi hantera skillnaden mellan constanter och parametrar,
dvs constanter man sätter vi t.ex instansiering av objekt.

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

## ihex-kommandokanal: binär styrning av en nod utan parser (IDÉ, 2026-07-31)
  En exec-only-nod har ingen parser -- den kan alltså inte ta emot CandySpeak
  som text. Men den behöver ändå konfigureras, patchas och inspekteras. Kanalen
  för det är binär.

  RAMFORMAT: Intel-HEX-poster, `:LLAAAATTDD..CC`. Inte APDU:s byte-ström
  (CLA INS P1 P2 Lc), trots att det var namnet idén föddes med -- kanalen är en
  DELAD UART med en människa på, och radorienterad ASCII vinner där:
  - går genom radläsaren oförändrad (kö, XOFF, för lång rad -- allt redan gjort),
  - `:` kan inte inleda en CandySpeak-rad, så diskrimineringen är gratis och
    hex kan blandas med vanlig prompt,
  - går att klistra in, och varje inbyggnadsverktyg spottar redan ur sig det,
  - längd + checksumma finns i formatet.
  APDU:s SEMANTIK (INS + parametrar) läggs i typbyten. Strukturen utan egen ram.

  BÖRJA MED IDENTIFIERA + LÄSA, inte skriva:
  1. ID -- svara med bildens `crc_hdr` (= `rom_fp`, det csp_eeprom redan
     använder), version och räknare. Utan det vet man inte att indexen betyder
     vad man tror, och då får man inte patcha. Säkerhet före funktion.
  2. LÄS -- dumpa decls/instrs/strängar binärt. Gör skrivvägen testbar (skriv,
     läs tillbaka, jämför) och låter ramformatet mogna på ofarlig trafik.
  3. SKRIV -- lägg till decl / instruktioner / sträng. Sedan commit/rebuild.
  4. Spara/ladda/rensa -- återanvänder eeprom-vägarna rakt av.

  POÄNGEN med läs-först: en nod kan inspekteras HELT med bara läskommandon,
  för disassemblern behöver inte finnas på kortet. Noden dumpar binärt, HOSTEN
  renderar `/list`. Det är exec-only-tänket taget hela vägen.

  SAMMA BYTES SOM EEPROM: en EEPROM-patch ÄR redan binära decls och
  instruktioner. Att skriva en decl över tråden och att skriva en till EEPROM är
  samma format -- ett format, två transporter. Designa kommandona så att de
  producerar det eeprom lagrar, inte något parallellt.

  UTVECKLAS PÅ HOSTEN: `make exec` ger en csp-exec att köra kommandona mot, så
  hela kommandouppsättningen kan byggas och testas i tests/repl.sh utan
  hårdvara. csp-exec-städningen är gjord (se DONE 2026-07-31).

  BEROENDE (Tony 2026-07-31): EEPROM och IAP är BÅDA nödvändiga på en riktig
  nod, och de löser olika halvor. EEPROM = litet, per nod, skrivs ofta,
  överlever omflashning -> konfiguration och små regelpatchar. IAP = hel bild,
  skrivs sällan -> byta ROM:en själv, eller en andra plats (= FAILSAFE-som-
  andra-image, grupp 3). Därför är `CSP_EXEC_ONLY` och `CSP_NO_EEPROM`
  oberoende flaggor: en riktig nod vill ha den första men inte den andra.

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

## #local -- KLART 2026-08-11, utom listnings-rundturen
  BYGGT: `#local a:16 = <uttryck>`. Enkelbuffrat löv via BUF_F_LOCAL -- csp_slot
  tvingar dir=DIN för det, så en kedja `sum -> twice -> plus1` löser sig i EN
  cykel i stället för tre. Flaggan sitter på BUFFERTEN, inte på csp_view_t: vy-
  tabellen är den största per program och en bredare flaggfält där hade växt
  varje löv, medan csp_slot ändå laddar buf-posten för .hp.
  Formeln kompileras till en vanlig alltid-sann regel emitterad vid
  deklarationen -- locals måste ändå deklareras före sin användning, så
  deklarationsordningen ÄR körordningen och ingen separat fas behövs.
  Tilldelning avvisas med ERR_ASSIGN_TO_LOCAL i coerce_assign (den enda punkt
  varje tilldelningsmål passerar); `cs.local_def` släpper igenom den enda
  tilldelning som deklarationen själv lägger ut.
  Tester: tests/unit/local + två fall i tests/repl.sh.

  KVAR: LISTNINGEN GÅR INTE ATT KLISTRA TILLBAKA. En local listas som
  `#local sum:32 integer` och formeln som en separat regel `sum=a+b`. Klistrar
  man in det avvisas regeln (tilldelning till en local). Rätt fix: sätt formeln
  PÅ deklarationsraden och undertryck den regeln i regel-listningen -- regeln som
  hör till en local är den vars lagring träffar just den deklarationen, och
  exprbuf kan redan rendera dess kropp. Tills dess skrivs ingen `= 0` ut, så
  listningen ljuger åtminstone inte.

## #local -- namngiven formel i ett scope (DESIGN AVGJORD, Tony 2026-08-11)
  Mellanberäkningar utan att gå via transaktionen. En `#local` är INTE en variabel
  man tilldelar -- den BINDER en formel, som ALISTER/erlister:s `def`.

    #local gx:16 = 512 - AccX
    #local a = (81*gx - 59*gy) / 100

  Formeln får referera allt som är deklarerat FÖRE den i samma scope (globalt
  eller objekt): variabler, inputs, andra `#local`. ALISTER begränsar `def` till
  inparametrar; vi vidgar till hela scopet.

  VARFÖR DEF OCH INTE EN TILLDELBAR TEMPORÄR: utan tilldelning finns ingen
  läs-före-skriv, ingen ordningskänslighet, ingen nollning vid cykelstart. Och
  eftersom formelns alla läsningar ser förra commiten är värdet KONSTANT genom
  hela cykeln -- referentiellt transparent. Det är det som gör konstruktionen
  billig att resonera om. Sigil-varianten (`$t`, kroppsskopad temporär) förkastad:
  den kräver nytt token, ny läsregel i skannern, och den måste in i runtime och
  disassemblering. En def gör inget av det.

  Namnet: `#local`, inte `#temp`. Det är ingen temporär lagring, det är en skopad
  definition -- och inuti en `#module` är "local" bokstavligt sant.

  IMPLEMENTATION (alternativ A, valt): PROLOG.
  Formlerna kompileras i deklarationsordning till en prolog som körs före
  reglerna varje cykel. Varje användning blir en vanlig laddning: noll ändringar
  i uttryckskompilatorn vid användningsstället, ingen ny opcode, och
  disassembleringen visar prologen som vanliga instruktioner.

  RÄTTELSE (2026-08-11, undersökt): "kostar EN plats, inte två" var FEL. DIN och
  DOUT är två halvor av EN allokering -- `heap[DIN]` är basen, `heap[DOUT]` är
  basen + halva, `heap_cap` är bytes PER HALVA. Varje löv upptar sin plats i
  båda oavsett, så det finns ingen RAM-vinst att hämta. Det som står kvar är:
  ingen post i commit-passet, aldrig i /state, aldrig i EEPROM, och läsbar i
  SAMMA cykel som den skrivs.

  SAMMA-CYKEL-LÄSNINGEN, bättre mekanism (funnen 2026-08-11 vid genomgång av
  csp_slot): halvan väljs av `dir` ENSAMT --
      csp_slot(st,v,dir) = heap[dir] + buf[v->buf].hp
  -- så en VY-FLAGGA som tvingar `dir = DIN` gör lövet enkelbuffrat på riktigt:
  läsning och skrivning träffar samma bytes, ingen prolog-FAS behövs, ingen
  `st->local[]`-lista, inget kopieringspass. En gren på ett fält som ändå är
  laddat. `csp_view_t.flags` finns redan (VIEW_F_*); kolla att VIEW_F_BITS har
  plats. Commit blir en no-op för dem (dset -> kopiera slot till sig själv).
  Ersätter den tidigare planen med prolog-fas + kopiering; SEMANTIKEN är
  oförändrad, bara mekaniken enklare.
  Utvärderingen kan då vara en helt vanlig alltid-sann regel emitterad vid
  deklarationen: locals måste ändå deklareras före sin användning (inga
  framåtreferenser), så deklarationsordningen ÄR körordningen.
  - Aldrig i `/state`, aldrig i EEPROM-patchen.
  - I en `#module` kör prologen per objekt. Faller ut.
  - Orena anrop i formeln körs exakt en gång per cykel. Väldefinierat.

  Alternativ B (inline-expansion vid varje användning) har IDENTISK semantik och
  kostar noll RAM, men kompilatorn måste behålla formeln i återemitterbar form
  och sparar varken AST eller tokens idag. Dessutom dupliceras koden per
  användning och orena anrop skulle köras en gång per användning. Förkastat.

  BEHÖVER INGEN NY DECL-TYP. `DECL_AVAIL` står på 14 och `DECL_END_MARK` på 0xf --
  det finns exakt EN ledig, och den ska inte gå hit. En local är en
  `DECL_VARIABLE` med `local:1`. Noll decl-typer, noll bytes, all befintlig
  variabelhantering oförändrad -- och det är också det SÄKRA valet: en ny
  decl-typ hade krävt en ny gren i varje switch över decl-typer (setup_decl,
  csp_dump, csp_list_decl, REPL-listningen, estimate...), och en missad gren är
  precis den buggklass som bitit gång på gång här.
  OBS: `DECL_HEADER` hade `_reserved:2`, men `cont` (arrayer) tog den ena. Den
  här tar den andra -- efteråt finns NOLL lediga bitar i headern och EN ledig
  decl-typ. Nästa flagga måste alltså hitta plats någon annanstans.

  LÅS DESSA VID IMPLEMENTATION:
  - FÖRBJUD FRAMÅTREFERENSER. En `#local` får bara referera det som redan
    deklarerats. Då är cykler (`a = b+1` och `b = a+1`) omöjliga PER KONSTRUKTION
    i stället för att kräva en cykeldetektor, och prologens ordning ÄR
    källordningen. Gratis på två ställen.
  - BEHÅLL `:res`. Inte pynt: `#local a:8 = x + y` är en avhuggning man annars
    skriver för hand, och `GET_RES`-klippet finns redan.
  - HOPPA ÖVER endian. Den hör hemma på vyer över bytes; en local är ett skalärt
    värde. (Tony: "behöver egentligen inte :res och endian" -- res är ändå värd
    den, endian inte.)
  - `a = ...` där `a` är en `#local` behöver ETT EGET FEL, inte "unknown
    variable". Det är misstaget alla gör en gång.
  - `<-`-GRAFEN är enda stället A är mer jobb än B: kanterna ska vara FORMELNS
    indata, inte platsen. Med inline hade det fallit ut automatiskt; med prolog
    måste det kopplas explicit när localen expanderas. Missas det blir en regel
    som beror på en local aldrig reaktivt väckt.

  Motiverande fall: examples/cpx_ball2.csp räknar `512-AccX` och `AccY-512` i tio
  regler.

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

## LPC-porten (csp_lpcopen.c) -- klar grund, fyra stubbar kvar (2026-08-13)
  Mot LPCOpen-drivrutinerna i nxp_lpcopen/, alltså ett lager under Arduino: inget
  core, inga bibliotek. KLART och verifierat: GPIO in/ut/config, ADC-in med
  res-skalning (samma signed-default som CPX), SysTick-klocka (ms + us),
  UART-konsol med XON/XOFF, minnesrapport, huvudloopen, on-chip EEPROM på
  17xx/40xx.

  PORTKONVENTIONEN blir äntligen ärlig här: `0:13` ÄR GPIO-port 0 pin 13, chipets
  egen numrering -- på Arduino måste `port` hittas på. En #analog väljer perifer
  via porten: 15 = ADC (pin = kanal), 13 = DAC. PWM går på `pwm`-flaggan som förr.

  STUBBAR att fylla i (alla har en fungerande no-op-kropp + kommentar som säger
  vad en riktig gör, sök på "STUB" i filen):
  - `csp_lpc_pin_mux()` -- IOCON (11xx/15xx/17xx/40xx) eller SCU (18xx/43xx).
    Detta är också där pullup/pulldown hör hemma: de sitter INTE i GPIO-blocket,
    så en `in pullup` funkar som vanlig ingång tills muxen sätter den.
  - `csp_lpc_pwm_write()` -- SCT eller timer-match, board-beroende.
  - `csp_lpc_dac_write()` -- två rader när man vet vilken familj.
  - `csp_lpc_board_init()` -- klockor, ström till periferi, extern oscillator.
  - CAN: tre olika API:er (Chip_CAN_ 17xx/40xx, Chip_CCAN_ 18xx/43xx, ROM-CAN
    15xx). Stubbarna är no-bus, vilket är ett fungerande läge.
  - EEPROM på 18xx/43xx: minnesmappad läsning, sidvis skrivning. Buffra en sida
    som SAMD-flashbackenden i csp_arduino.c buffrar en rad.
  - 15xx-ADC:n är sekvenserbaserad, egen init/läsväg.

  TESTET: tests/lpcstub/ deklarerar LPCOpen-API:t med rätt signaturer, så
  `make test` BYGGER OCH LÄNKAR porten mot kärnan med bara gcc -- och kör den:
  stubbens __WFI anropar SysTick_Handler så idle-väntan fullbordas, och dess UART
  läser stdin. Fångar båda sätten en port ruttnar (kärnan anropar något ingen
  implementerar; två filer definierar samma sak). Arduino-porten har inget
  motsvarande och kan inte lätt få det -- den behöver arduino-cli.

  LPC1754 (Makefile.lpc1754) = 175x/6x-familjen: samma periferi som 177x/8x MEN
  UTAN EEPROM. eeprom_17xx_40xx.h ligger i dess drivrutinskatalog vilket får det
  att se annorlunda ut -- chip_lpc175x_6x.h definierar aldrig LPC_EEPROM. Den
  grenen är därför NO_EEPROM: /save säger ärligt att det inte går. Vill man ha
  persistens där är IAP-flash vägen (iap.h finns).

  DUPLICERING, medveten: csp_setup/csp_input/csp_output är nästan identiska med
  csp_arduino.c:s. Walken över st->nio, cfg-kollen och DIN/DOUT-hanteringen är
  runtime-kontrakt, inte board-detalj. Bryt ut till en delad csp_io.c NÄR en
  tredje port dyker upp -- två kopior är där dupliceringen syns men fortfarande
  är billigare än en abstraktion anpassad efter ett urval på två.

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

  System objekt
  
#module Sys
  #variable NodeName string         = "Node1"
  #variable NodeID:32 unsigned      = 123
  #variable NodeIP:32 inet          = 192.168.2.100
  #constant NodeVersion:32 version  = 1.0.25
#endif
#Sys sys

sys.NodeName = "office"
sys.NodeName = "home"
...
sys.NodeID kan användas som CAN node id, där det används.

## Övrigt
  - Hur kombinerar man ROM + RAM => ny ROM-bas?
  - ROM-disable-flagga: ladda inte ROM-koden.
  - EEPROM-disable-flagga: ladda inte EEPROM-koden.
    Flaggorna lagras i EEPROM.
