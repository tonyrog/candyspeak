
# BUGS - remove when fixed

## Listing of RAM rule, list as [ROM]

[ROM] Led=1 ? BtnA&&BtnB
[RAM] Led=0                                                                     

Last row should be Led=0 ? !BtnA || !BntB

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
  - `.dlc` som part vore nästa naturliga: skicka färre byte än ramens storlek.
    Nu skickas alltid nbytes.
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

- How to combine ROM + RAM => new ROM base?

- ROM disable flag to kill off REAL firmware.

What about using states?

#in ISR
  Buffer[I] = CREG
  I = I + 1
  State = RTI
#end

- atomic keyword

