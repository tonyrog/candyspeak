# Parsern: en bisonfil som källa

`utils/candyspeak.y` är grammatiken, och både C-kompilatorn och Erlang-verktygen
parsar med den. Varje regel har en **action**, ett namn i stället för kod
(`| expr PLUS expr { add }`). Regler med samma action delar namnet, så det blir
153 namn för 280 regler, numrerade 0..255 i den ordning de först står
(`candyspeak_parse_tab:act_names/0`). Bison räknar bara fram automaten, och
tabellerna skriver vi själva ur `bison --xml`.

| fil                              | vad                                                  |
|----------------------------------|------------------------------------------------------|
| `utils/candyspeak.y`             | grammatiken, källan                                  |
| `utils/candyspeak_parse_act.erl` | Erlang-sidans actions: `act(Namn, Värden)` bygger AST:t |
| `utils/candyspeak_parse.erl`     | Erlang-motorn: `parse/1`, `parse/2`, `format_error/1` |
| `utils/candyspeak_parse_tab.erl` | Erlang-tabellerna (genererad, `lrgen.py`)            |
| `src/csp_lr.c`, `include/csp_lr.h` | C-motorn                                           |
| `gen/csp_lr_*`                   | C-tabellerna, actions och tokenkoder (genererade, `lrgen_c.py`) |
| `utils/lrgen.py`, `lrgen_c.py`, `lrpack.py` | generatorerna; `lrpack.py` komprimerar C-tabellerna |
| `tests/lr/`                      | C mot Erlang: `lrtest.c`, `check.escript`, extra program |

    make lr          # bison + python3: gen/csp_lr_* och candyspeak_parse_tab.erl
    make lr_check    # C-motorn mot Erlang-motorn (ingår i make test)

De genererade filerna checkas in. Att generera om är en utvecklaråtgärd, som
för `strings.tab`. En grammatikändring görs i `candyspeak.y` och vid behov i
`candyspeak_parse_act.erl`, följt av `make lr`, `make lr_check` och
`make compile_check`.

`file` är vänsterrekursiv, så att parserstacken håller sig liten oavsett
antal rader. Satserna samlas baklänges och `program` vänder dem.
Konflikterna (4 shift/reduce, 1 reduce/reduce) löses som yecc gjorde: shift,
respektive den regel som står först.

## C

`lrgen_c.py` skriver `gen/csp_lr_tab.h` med `lrpack.py`:s komprimering på nivå
4. Varje reduceringsklass har också ett action-nummer, och tillsammans med
tabellerna skrivs `gen/csp_lr_act.h` (`A_add` …), `gen/csp_lr_act.def` och
`gen/csp_lr_tokens.def` (tokenkoderna i grammatikens ordning). Grammatiken har
220 tillstånd, och tillsammans med goto-reduce-klasserna är det fler än en byte
rymmer, så goto-målen har 16 bitar.

Motorn har en värdestack. Ett shift lägger tokenets värde på stacken, och ett
reduce anropar `reduce(ctx, act, v, n)` och lägger resultatet i stället för de
n värdena. På atmega2560 tar motor och tabeller 3,4 kB. Stacken tar
1 + sizeof(lr_val_t) byte per nivå och har `LR_DEPTH` 40 nivåer, vilket räcker
till omkring 35 nästlade parenteser. Kompilatorn tillåter i dag 10.

`make lr_check` skickar Erlang-scannerns tokens till `tmp/lrtest`. Båda sidor
bygger ett generiskt träd `(action barn ...)`, och träden jämförs för varje
program, helt och med högst 200 rader avklippta på mitten. Fall som är djupare
än `LR_DEPTH` (`tests/lr/nest50.csp`, `nest200.csp`) räknas för sig.

## I kompilatorn

`csp_parse.c` inkluderar motorn och kör den på en rad i taget:
`csp_lr_line(tv, n, &sm)` parsar raden som ett program med en sats. Ett ord
eller nyckelord slås upp på stavningen (nyckelords-token bär sin text), och
`<<` eller `>>` följt av `=` blir `<<=`/`>>=`. Ett värde är token-intervallet
symbolen täcker. Satsens action och dess symbolers intervall hamnar i
`csp_stmt_t`, och varje tilldelning (`A_assign`, `A_assign_prev`,
`A_assign_none`) i `sm.part[]`.

`csp_parse` i `csp_compile.c` väljer på `sm.act`, och `csp_parse_X` läser
sina delar ur intervallen: namn, `[n]`, `:bitar`, options (`parse_opts` över
intervallet), pinnar och transport. Uttryck förblir intervall som
`csp_expr` kompilerar, och de läses en gång innan, så att strängarna hamnar
i samma ordning som förut (`make compile_check`).

En rad som grammatiken inte tar ger ett kort `syntax error` innan något
deklareras. På Erlang-sidan säger felet vilket token det stannade vid,
skrivet som yecc skrev det.
