#!/usr/bin/env escript
%% NO -sname. It used to say `%%! -sname gen_chips`, which starts a DISTRIBUTED
%% node under a fixed name -- and two of those cannot exist at once. A parallel
%% make (or a `make test` running alongside a board build) then had one
%% invocation die with "the name gen_chips@host seems to be in use", and because
%% every call site is a $(shell ...) that death became an EMPTY STRING.
%%
%% Which is how `objcopy --change-addresses=` ends up with no address and writes
%% a hex file for 0 -- a firmware that flashes cleanly and runs nowhere. This
%% script reads files and prints text; it never needed distribution.
%%
%% chips/<vendor>/*.terms -> csp_chip.c + csp_chip.h, for ONE chip.
%%
%% Generated per build, not checked in: a target carries the geometry of the
%% part it IS, not a table of thirty it is not. That is also what lets this do
%% arithmetic the C compiler cannot -- solving a PLL against a board's crystal,
%% and failing the BUILD when the numbers do not meet rather than at boot.
%%
%%     escript utils/gen_chips.erl lpc2129 csp_chip.c csp_chip.h
%%     escript utils/gen_chips.erl --list
%%
%% The files are read as a set: a {chip,...} names a {group,...}, a group names
%% a {family,...}, and each level only states what it changes. Nothing repeats.

-mode(compile).

%% --list [RE], --boards [RE]. The pattern is a regexp over the NAME, because
%% "which parts are 212x" and "which boards use a 1754" are the questions
%% actually asked of a table this size, and grepping the output loses the second
%% line of each entry.
main(["--list"]) -> main(["--list", "."]);
main(["--boards"]) -> main(["--boards", "."]);
main(["--boards", RE]) ->
    Db = load(),
    {ok, MP} = re:compile(RE, [caseless]),
    Bs = [{N, P} || {board, N, P} <- Db, match(MP, N)],
    (Bs =:= []) andalso io:format("no board matches ~s~n", [RE]),
    [show_board(Db, N, P) || {N, P} <- lists:sort(Bs)],
    ok;
%% --check [RE]: hold every board against its chip. This is the whole point of
%% the terms -- data a script can disagree with. Three distinct verdicts,
%% because they are three different situations:
%%
%%   ERROR    the pin is in the table and does not have that function. The
%%            board is wrong and the build should not proceed.
%%   UNKNOWN  the pin is not in the table. The TABLE is incomplete, not the
%%            board -- say so and carry on.
%%   MISSING  a peripheral is enabled with no pins muxed for it, or pins are
%%            muxed for one that is not enabled. Either way something was half
%%            done, and neither half fails on its own.
main(["--check"]) -> main(["--check", "."]);
main(["--check", RE]) ->
    Db = load(),
    {ok, MP} = re:compile(RE, [caseless]),
    Bs = [{N, P} || {board, N, P} <- Db, match(MP, N)],
    N = lists:sum([check_board(Db, B) || B <- lists:sort(Bs)]),
    case N of
	0 -> io:format("~w board(s): ok~n", [length(Bs)]);
	_ -> io:format("~w problem(s)~n", [N]), halt(1)
    end;
main(["--list", RE]) ->
    Db = load(),
    {ok, MP} = re:compile(RE, [caseless]),
    %% `usable` is what the sector table adds up to. Where it is less than the
    %% part's size the difference is the boot block, and seeing both is how you
    %% notice a table that is quietly wrong.
    [begin
	 G = resolve(Db, P),
	 Use = lists:sum(kv(G, sectors, [])) div 1024,
	 NS = length(kv(G, sectors, [])),
	 Geom = case NS of
		    %% No table: the part's flash is managed elsewhere (an
		    %% Arduino core), so say what it erases in and where
		    %% settings go rather than print two zeroes.
		    0 -> f("~w-byte pages, store in ~s",
			   [kv(G, page, 0), kv(G, store, '?')]);
		    _ -> f("~wK usable, ~w sectors", [Use, NS])
		end,
	 Per = [f(" ~s~w", [K, N]) || {K, N} <- kv(G, peripherals, [])],
	 io:format("~-10s ~-11s ~4wK flash (~s) ~3wK ram~n           ~s~n",
		   [C, kv(G, group, '?'), kv(P, flash_kb, 0), Geom,
		    kv(P, ram_kb, 0), Per])
     end || {C, P} <- chips(Db), match(MP, C)],
    ok;
%% --all: every part in one table, for the HOST tools (--devices, --ld). A
%% target gets one chip; a tool that has to name any of them gets the lot. Both
%% come out of the same terms, which is the point -- the hand-written copy this
%% replaces had the LPC1754 at 160K when the part has 128.
main(["--all", CFile]) ->
    Db = load(),
    Chips = [{N, resolve(Db, P)} || {N, P} <- chips(Db)],
    %% A part with no region map is geometry only: nothing can be written to it
    %% until someone decides where things go, which is a decision and not a
    %% default. nregion == 0 makes every csp_region_find miss.
    ok = file:write_file(CFile, all_source(Chips)),
    io:format("csp_chips: ~w parts~n", [length(Chips)]),
    ok;
%% --device BOARD [MAP] CFILE: the part THIS firmware runs on, as ONE table.
%%
%% --all emits every part, for a host tool that has to name any of them. A board
%% needs exactly one, and it needs the accessor with it: csp_flash.c asks
%% csp_device() for the geometry, and on the host that lives in port/csp_devices.c
%% -- which a target does not link. Without this the first thing to actually call
%% a flash function fails to link, and it took an upgrade command to find that.
%%
%% Same source, same map, same numbers as --ld. Which is the point: the linker
%% script and the region table the firmware carries cannot disagree.
main(["--device", Name, CFile]) -> main(["--device", Name, "", CFile]);
main(["--device", Name, Want, CFile]) ->
    Db = load(),
    case target(Db, Name, Want) of
	false -> halt(1);
	G -> ok = file:write_file(CFile, device_source(Name, G))
    end;
%% --ld=PART: the linker script. In the SCRIPT and not in the C tool, because
%% the answer is arithmetic over the terms and nothing at run time needs it --
%% a 35-part table compiled into a host binary to serve one command-line flag
%% is a table that has to be regenerated, linked and kept in step for no reason.
main(["--ld", Name]) -> main(["--ld", Name, ""]);
main(["--ld", Name, Want]) ->
    Db = load(),
    case target(Db, Name, Want) of
	false -> halt(1);
	G -> io:put_chars(ld_script(list_to_atom(Name), G))
    end;
%% Which maps a board offers, and which one is live.
main(["--maps", Name]) ->
    Db = load(),
    case lookup(Db, board, list_to_atom(Name)) of
	false -> io:format(standard_error, "no such board '~s'~n", [Name]),
		 halt(1);
	P ->
	    Ms = [{N, R} || {map, N, R} <- P],
	    Act = active_map_name(P),
	    case Ms of
		[] -> io:format("~s: no board maps; uses the chip's~n", [Name]);
		_ -> [io:format("~s~-12s ~w region(s)~n",
				[case N =:= Act of true -> "* "; false -> "  " end,
				 N, length(R)]) || {N, R} <- Ms]
	    end
    end;
%% One fact, one line, nothing else on stdout. For a Makefile, which should not
%% be parsing a listing meant for a person -- and which pipes it through `head`,
%% closing the pipe and leaving escript to die on EPIPE with a page of Erlang.
main(["--chip-of", Name]) ->
    Db = load(),
    case lookup(Db, board, list_to_atom(Name)) of
	false -> io:format(standard_error, "no such board '~s'~n", [Name]), halt(1);
	P -> io:format("~s~n", [kv(P, chip, '?')])
    end;
%% arm7 or cm3, from the family's entry symbol -- the same fact the linker
%% script needs, asked a different way.
%% Where the runtime region starts. What a raw .bin has to be flashed AT, and
%% what an ihex has to be shifted BY -- the two questions a .bin cannot answer
%% about itself.
main(["--load-addr", Name]) -> main(["--load-addr", Name, ""]);
main(["--load-addr", Name, Want]) ->
    Db = load(),
    case target(Db, Name, Want) of
	false -> halt(1);
	G ->
	    Sectors = kv(G, sectors, []),
	    Base = kv(G, flash_base, 0),
	    First = case [A || {runtime, A, _} <- kv(G, map, [])] of
			[A|_] -> A;
			[] -> 0
		    end,
	    io:format("0x~8.16.0B~n",
		      [Base + lists:sum(lists:sublist(Sectors, First))])
    end;
%% Where a board's terms file lives. The build goes NEXT TO IT rather than into
%% one shared tree: a pile of firmware images that all look alike is how one
%% ends up flashing the wrong board, and a path that names the board is a much
%% better guard than remembering which build was which.
%% The name of the map a build would use, so the output directory can be named
%% after it. Without this the variant is the literal MAP= string, which is empty
%% for a default build -- and "build/" tells you nothing about what is in it.
main(["--map-of", Name]) -> main(["--map-of", Name, ""]);
main(["--map-of", Name, Want]) ->
    Db = load(),
    case lookup(Db, board, list_to_atom(Name)) of
	false -> io:format("default~n");         %% a chip, or no such board
	P ->
	    case {[N || {map, N, _} <- P], Want} of
		{[], _} -> io:format("default~n");   %% uses the chip's map
		{_, ""} -> io:format("~w~n", [active_map_name(P)]);
		{Ns, W} ->
		    case [N || N <- Ns, atom_to_list(N) =:= W] of
			[N|_] -> io:format("~w~n", [N]);
			[] -> io:format(standard_error,
					"gen_chips: board '~s' has no map '~s'~n",
					[Name, W]),
			      halt(1)
		    end
	    end
    end;
%% One fact per query, the way --chip-of and --arch-of already work: the
%% Makefile asks, and a missing answer is an empty string it can test for.
%% Every board of one kind, one name per line. `make boards_all` walks this
%% instead of globbing CandySpeak/Makefile.* -- the terms are the list now.
%% --sketch-yaml <out>: ONE file with a profile per Arduino board.
%%
%% A sketch.yaml lives in the sketch folder and every board shares one sketch,
%% so this is one file with ten profiles rather than ten files. `arduino-cli
%% compile -m <board>` then builds against the PINNED core and library versions
%% instead of whatever happens to be installed -- which is the same argument the
%% terms files make everywhere else: a dependency should be data, not a fact
%% about this machine.
main(["--sketch-yaml", Out]) ->
    Bs = [{N, P} || {board, N, P} <- load(),
                    kv(P, toolchain, bare) =:= arduino_cli],
    ok = file:write_file(Out, sketch_yaml(lists:sort(Bs))),
    io:format("~s: ~w profile(s)~n", [Out, length(Bs)]);
main(["--boards-with", TC]) ->
    Want = list_to_atom(TC),
    [io:format("~s~n", [N])
     || {board, N, P} <- load(), kv(P, toolchain, bare) =:= Want],
    ok;
main(["--toolchain-of", Name]) ->
    io:format("~s~n", [atom_to_list(kv(board_of(Name), toolchain, bare))]);
main(["--fqbn-of", Name]) ->
    io:format("~s~n", [kv(board_of(Name), fqbn, "")]);
main(["--port-of", Name]) ->
    io:format("~s~n", [kv(board_of(Name), port, "")]);
main(["--nm-of", Name]) ->
    io:format("~s~n", [kv(board_of(Name), nm, "nm")]);
main(["--cflags-of", Name]) ->
    io:format("~s~n", [kv(board_of(Name), cflags, "")]);
main(["--ldflags-of", Name]) ->
    io:format("~s~n", [kv(board_of(Name), ldflags, "")]);
main(["--optimize-of", Name]) ->
    io:format("~s~n", [kv(board_of(Name), optimize, "-Os")]);
main(["--dir-of", Name]) ->
    case term_file(board, list_to_atom(Name)) of
	false ->
	    io:format(standard_error, "gen_chips: no such board '~s'~n", [Name]),
	    halt(1);
	F -> io:format("~s~n", [filename:dirname(F)])
    end;
main(["--arch-of", Name]) ->
    Db = load(),
    Chip = case lookup(Db, board, list_to_atom(Name)) of
	       false -> list_to_atom(Name);        %% a chip name works too
	       P -> kv(P, chip, '?')
	   end,
    case lookup(Db, chip, Chip) of
	false -> io:format(standard_error, "no such chip '~s'~n", [Chip]), halt(1);
	CP ->
	    G = resolve(Db, CP),
	    io:format("~s~n", [case kv(G, entry, '_start') of
				   'ResetISR' -> "cm3";
				   _ -> "arm7"
			       end])
    end;
%% --board <name> <out.h>: the pin mux and the power bits, generated.
%%
%% Two things a board file states in prose and nobody translates twice:
%%   {pin, 'P0.25', rd1}  ->  a PINSEL field set to the function's index
%%   {enable, [can1,...]} ->  a PCONP word with everything else CLEARED
%%
%% Cleared, not left alone: the reset value has several peripherals powered, so
%% "not mentioned" has to mean "off" or the default is to run everything.
main(["--board", Name, HFile]) ->
    Db = load(),
    B = list_to_atom(Name),
    case lookup(Db, board, B) of
	false ->
	    io:format(standard_error, "gen_chips: no such board '~s'~n", [Name]),
	    halt(1);
	P ->
	    %% Refuse to generate for a board that does not check out. A
	    %% generated pin mux from a bad description is a board that comes up
	    %% almost working, which is the expensive kind.
	    case check_board(Db, {B, P}) of
		0 -> ok;
		N -> io:format(standard_error,
			       "gen_chips: ~s has ~w problem(s); not generating~n",
			       [Name, N]),
		     halt(1)
	    end,
	    case kv(P, toolchain, bare) of
		arduino_cli ->
		    ok = file:write_file(HFile, arduino_header(B, P)),
		    io:format("~s: ~w defines, fqbn ~s~n",
			      [Name, length(kv(P, define, [])),
			       kv(P, fqbn, "?")]);
		_ ->
		    G = resolve(Db, lookup(Db, chip, kv(P, chip, '?'))),
		    ok = file:write_file(HFile,
					 board_header(B, P, G, pin_table(Db, G),
						      eeprom_of(Db, P))),
		    io:format("~s: ~w pins, PCONP 0x~8.16.0B~n",
			      [Name, length([X || {pin,_,_} = X <- P]),
			       pconp(P, G)])
	    end
    end;
main(["--help"|What]) -> help(What), halt(0);
main(["-h"|What]) -> help(What), halt(0);
main([Chip, CFile, HFile]) ->
    Db = load(),
    Name = list_to_atom(Chip),
    case lookup(Db, chip, Name) of
	false ->
	    io:format(standard_error, "gen_chips: no such chip '~s'~n", [Chip]),
	    io:format(standard_error, "  try: escript utils/gen_chips.erl --list~n", []),
	    halt(1);
	P ->
	    G = resolve(Db, P),
	    ok = file:write_file(HFile, header(Name, G)),
	    ok = file:write_file(CFile, source(Name, G)),
	    io:format("~s: ~w sectors, ~wK flash, ~wK ram~n",
		      [Chip, length(kv(G, sectors, [])),
		       kv(G, flash_kb, 0), kv(G, ram_kb, 0)])
    end;
main(_) -> help(usage), halt(1).

help(_) ->
    io:format(standard_error,
	      "usage: gen_chips.erl <chip> <out.c> <out.h>~n"
	      "  options: "
              "       --help | -h~n"
	      "       --list [regexp]~n"
	      "       --boards [regexp]~n"
	      "       --ld <chip>~n"
	      "       --check [regexp]~n"
	      "       --board <board> <out.h>~n"
              "       --all <file>~n"
	      "       --maps~n"
	      "       --chip-of <name>~n"
	      "       --load-addr <name>~n"
	      "       --load-addr <name> <want>~n"
	      "       --map-of <name>~n"
	      "       --map-of <name> <want>~n"
	      "       --sketch-yaml <file>~n"
	      "       --boards-with <tool-chain>~n"
              "       --toolchain-of <name>~n"
              "       --fqbn-of <name>~n"
              "       --port-of <name>~n"
              "       --nm-of <name>~n"
              "       --cflags-of <name>~n"
              "       --ldflags-of <name>~n"
              "       --optimize-of <name>~n"
              "       --dir-of <name>~n"
              "       --arch-of <name>~n"
              , []).

%% Resolve a NAME -- board or chip -- into the property list to generate from,
%% with the right region map already substituted.
%%
%% A MAP IS A BOARD FACT, not a chip one. The same LPC1754 with a USB boot
%% loader in its low sectors has a different map from one without, and the
%% silicon cannot tell you which. So a board may carry its own {map, N, [...]}
%% entries and name one with {active_map, N}; a board that carries none falls
%% back to its chip's, which is right for a part used plainly.
target(Db, Name, Want) ->
    A = list_to_atom(Name),
    case lookup(Db, board, A) of
	false ->
	    case lookup(Db, chip, A) of
		false ->
		    io:format(standard_error,
			      "gen_chips: no such board or chip '~s'~n", [Name]),
		    false;
		CP -> resolve(Db, CP)
	    end;
	P ->
	    Chip = kv(P, chip, '?'),
	    case lookup(Db, chip, Chip) of
		false ->
		    io:format(standard_error,
			      "gen_chips: board '~s' names chip '~w', "
			      "which does not exist~n", [Name, Chip]),
		    false;
		CP ->
		    G = resolve(Db, CP),
		    case board_map(P, Want) of
			false ->
			    io:format(standard_error,
				      "gen_chips: board '~s' has no map '~s'~n",
				      [Name, Want]),
			    false;
			[] -> G;                     %% no board maps: chip's
			R -> [{map, R} | G]          %% kv takes the head
		    end
	    end
    end.

%% The board's maps, and which is live. `Want` overrides {active_map,...} so a
%% build can say `MAP=usb_boot` without editing the board file.
board_map(P, Want) ->
    Ms = [{N, R} || {map, N, R} <- P],
    case {Ms, Want} of
	{[], _} -> [];
	{_, ""} ->
	    Act = active_map_name(P),
	    case [R || {N, R} <- Ms, N =:= Act] of
		[R|_] -> R;
		[] -> element(2, hd(Ms))         %% no active_map: the first
	    end;
	{_, W} ->
	    case [R || {N, R} <- Ms, atom_to_list(N) =:= W] of
		[R|_] -> R;
		[] -> false
	    end
    end.

active_map_name(P) ->
    case kv(P, active_map, undefined) of
	undefined -> case [N || {map, N, _} <- P] of
			 [N|_] -> N;
			 [] -> undefined
		     end;
	N -> N
    end.

%% --- board generation --------------------------------------------------------

%% The PCONP word. Everything the board asked for, and nothing else.
pconp(P, G) ->
    Bits = kv(G, pconp, []),
    Always = kv(G, pconp_always, []),
    lists:foldl(fun(Dev, Acc) ->
			case kv(Bits, Dev, undefined) of
			    undefined -> Acc;   %% no bit: see pconp_always
			    N -> Acc bor (1 bsl N)
			end
		end, 0, kv(P, enable, []) -- Always).

%% The board's {eeprom, [...]} joined with the PART's geometry from
%% chips/i2c/eeproms.terms. Board props first, so a board can override a number
%% -- a slower bus because of cable length, say -- without a new part entry.
%%
%% An unknown part is a BUILD error and not a silent skip: the board asked for
%% persistent storage, and a firmware that comes up with /save quietly doing
%% nothing is the failure this whole generator exists to prevent.
eeprom_of(Db, P) ->
    case kv(P, eeprom, undefined) of
	undefined -> undefined;
	EP ->
	    case kv(EP, part, undefined) of
		undefined -> undefined;         %% on-chip eeprom: no part named
		Part ->
		    case lookup(Db, eeprom, Part) of
			false ->
			    io:format(standard_error,
				      "gen_chips: unknown eeprom part ~p "
				      "(see chips/i2c/eeproms.terms)~n", [Part]),
			    halt(1);
			Geo -> {Part, EP ++ Geo}
		    end
	    end
    end.

%% i2c0 -> LPC_I2C0. The bus is named in the board terms the way the pins are.
ee_bus(Bus) -> "LPC_" ++ string:uppercase(atom_to_list(Bus)).

eeprom_defs(undefined) -> [];
eeprom_defs({Part, Q}) ->
    Bytes = kv(Q, bytes, 0),
    Bank  = kv(Q, bank_bits, 0),
    %% The device must be able to REACH what the board claims it has. A 24C16
    %% described as 32 KiB would address the first 2 KiB four times over and
    %% /save would appear to work while overwriting itself.
    Reach = (1 bsl (8 * kv(Q, addr_bytes, 2) + Bank)),
    (Bytes > Reach) andalso
	begin
	    io:format(standard_error,
		      "gen_chips: ~p: {bytes,~w} is more than the ~w bytes "
		      "~w address byte(s) plus ~w bank bit(s) can reach~n",
		      [Part, Bytes, Reach, kv(Q, addr_bytes, 2), Bank]),
	    halt(1)
	end,
    f("~n// Persistent store: a ~p on ~p. Geometry from chips/i2c/eeproms.terms;~n"
      "// see csp_eeprom_i2c.c for what the driver does with each number.~n"
      "#define CSP_EEPROM_I2C       1~n"
      "#define CSP_EEPROM_I2C_BUS   ~s~n"
      "#define CSP_EEPROM_I2C_ADDR  0x~2.16.0B~n"
      "#define CSP_EEPROM_BYTES     ~w~n"
      "#define CSP_EEPROM_PAGE      ~w~n"
      "#define CSP_EEPROM_ADDR_BYTES ~w~n"
      "#define CSP_EEPROM_BANK_BITS ~w~n"
      "#define CSP_EEPROM_HZ        ~w~n"
      "#define CSP_EEPROM_WRITE_MS  ~w~n",
      [Part, kv(Q, bus, i2c0), ee_bus(kv(Q, bus, i2c0)),
       kv(Q, base, 16#A0) bor (kv(Q, e2, 0) bsl 3),
       Bytes, kv(Q, page, 32), kv(Q, addr_bytes, 2), Bank,
       kv(Q, hz, 100000), kv(Q, write_ms, 10)]).

%% The board's proplist, or die saying which name was not found. Every --*-of
%% verb wants exactly this.
%% An Arduino board has no chip to resolve and no clock to state -- the core
%% owns both -- so listing it against the chip database printed "no such chip"
%% for every one of them. What identifies it is the FQBN.
show_board(_Db, N, P) when is_atom(N) ->
    case kv(P, toolchain, bare) of
	arduino_cli ->
	    io:format("~-12s ~s~n", [N, kv(P, fqbn, "?")]),
	    io:format("             arduino-cli~s~n",
		      [case kv(P, define, []) of
			   [] -> "";
			   D -> f(", ~w define(s)", [length(D)])
		       end]);
	_ -> show_bare(_Db, N, P)
    end.

show_bare(Db, N, P) ->
    Chip = kv(P, chip, '?'),
    %% The chip is shown RESOLVED, not as written: a board naming a part that
    %% does not exist is the mistake worth catching, and it is invisible until
    %% something tries to build it.
    Geo = case lookup(Db, chip, Chip) of
	      false -> "*** no such chip ***";
	      CP -> f("~wK flash, ~wK ram",
		      [kv(CP, flash_kb, 0), kv(CP, ram_kb, 0)])
	  end,
    io:format("~-12s ~-10s ~s~n", [N, Chip, Geo]),
    io:format("             ~w MHz core, ~w MHz xtal, arena ~w~n",
	      [kv(P, core, 0) div 1000000, kv(P, xtal, 0) div 1000000,
	       kv(P, code_budget, '-')]),
    case kv(P, enable, []) of
	[] -> ok;
	E -> io:format("             enable:~s~n", [[f(" ~w",[X]) || X <- E]])
    end.

sketch_yaml(Bs) ->
    ["# generated by `gen_chips.erl --sketch-yaml` -- do not edit.\n"
     "# boards/<name>/<name>.terms is the source; one profile per board.\n"
     "#\n"
     "# Build one with:  arduino-cli compile -m <board>\n"
     "# (Makefile.board does this for you.)\n"
     "#\n"
     "# The profile pins the CORE and LIBRARY versions. It cannot carry the\n"
     "# -I/-D build properties -- those stay on the command line -- so what it\n"
     "# buys is reproducibility, not fewer flags.\n"
     "profiles:\n",
     [profile(N, P) || {N, P} <- Bs]].

profile(N, P) ->
    [f("  ~s:\n", [N]),
     f("    fqbn: ~s\n", [kv(P, fqbn, "?")]),
     %% A THIRD-PARTY platform needs its index URL in the profile. Without it
     %% arduino-cli tries to DOWNLOAD the pinned version from the indexes it
     %% knows, finds nothing, and fails with "platform not installed" -- even
     %% though `core list` shows it installed. The bundled arduino:* platforms
     %% need no URL.
     case [{I, V, U} || {platform, I, V, U} <- P] ++
	  [{I, V, none} || {platform, I, V} <- P] of
	 [] -> [];
	 [{Id, Ver, none} | _] ->
	     f("    platforms:\n      - platform: ~s (~s)\n", [Id, Ver]);
	 [{Id, Ver, Url} | _] ->
	     f("    platforms:\n      - platform: ~s (~s)\n"
	       "        platform_index_url: ~s\n", [Id, Ver, Url])
     end,
     case kv(P, libraries, []) of
	 [] -> [];
	 Ls -> ["    libraries:\n",
		[f("      - ~s (~s)\n", [Nm, V]) || {Nm, V} <- Ls]]
     end].

board_of(Name) ->
    case lookup(load(), board, list_to_atom(Name)) of
	false ->
	    io:format(standard_error, "gen_chips: no such board '~s'~n", [Name]),
	    halt(1);
	P -> P
    end.

%% The header for a board arduino-cli builds.
%%
%% No pin mux and no power word -- the core does both -- so what is left is the
%% settings that used to sit in boards/<name>.h by hand. Generating it means the
%% FQBN and the defines come from ONE file, and a board can no longer describe
%% itself two ways that disagree.
%%
%% #ifndef around each define: a define given on the command line (EXTRA=, or a
%% one-off -D) still wins, which is how the exec-only and diagnostic builds work.
arduino_header(Name, P) ->
    U = string:uppercase(atom_to_list(Name)),
    [f("// generated by `gen_chips.erl --board ~s` -- do not edit.~n"
       "// boards/~s.terms is the source.~n"
       "~n#ifndef __CSP_BOARD_~s_H__~n#define __CSP_BOARD_~s_H__~n~n",
       [Name, Name, U, U]),
     f("#define CSP_BOARD_NAME \"~s\"~n~n", [Name]),
     [arduino_define(D) || D <- kv(P, define, [])],
     f("~n#ifndef SUPPORT_REACTIVE~n#define SUPPORT_REACTIVE ~w~n#endif~n",
       [kv(P, support_reactive, 0)]),
     f("#ifndef USE_STATISTICS~n#define USE_STATISTICS   ~w~n#endif~n",
       [kv(P, use_statistics, 0)]),
     f("~n#endif~n", [])].

arduino_define({K, V}) when is_integer(V) ->
    f("#ifndef ~s~n#define ~s ~w~n#endif~n", [K, K, V]);
arduino_define({K, V}) ->
    f("#ifndef ~s~n#define ~s ~s~n#endif~n", [K, K, V]);
arduino_define(K) ->
    f("#ifndef ~s~n#define ~s 1~n#endif~n", [K, K]).

board_header(Name, P, G, Pins, EE) ->
    Muxed = [{Pin, Fn} || {pin, Pin, Fn} <- P],
    Chip = kv(P, chip, '?'),
    [f("// generated by `gen_chips.erl --board ~s` -- do not edit.~n"
       "// boards/~s.terms is the source.~n"
       "//~n"
       "// The pin mux and the power word, which are the two things that are~n"
       "// stated in a board description and then translated by hand into~n"
       "// register writes. Doing it here means a pin that cannot have the~n"
       "// function asked for is a BUILD error -- see `make check-boards`.~n"
       "~n#ifndef __CSP_BOARD_~s_H__~n#define __CSP_BOARD_~s_H__~n~n",
       [Name, Name, string:uppercase(atom_to_list(Name)),
	string:uppercase(atom_to_list(Name))]),
     f("#define CSP_BOARD_NAME  \"~s\"~n"
       "#define CSP_CHIP        ~s~n"
       "#define CSP_XTAL_HZ     ~w~n"
       "#define CSP_CORE_HZ     ~w~n"
       "#define CSP_PCLK_DIV    ~w~n",
       [Name, Chip, kv(P, xtal, 0), kv(P, core, 0), kv(P, pclk_div, 4)]),
     %% Total RAM, from the CHIP. csp_lpcopen.c cannot derive it -- _vStackTop
     %% is the top of one bank and these parts scatter RAM across several -- so
     %% it reports 0 for "nobody told me", which makes the whole /memory block
     %% read as zeroes. The terms know; say so.
     f("#define CSP_LPC_RAM     ~w~n", [kv(G, ram_kb, 0) * 1024]),
     case kv(P, code_budget, undefined) of
	 undefined -> [];
	 CB -> f("#define CSP_CODE_BUDGET ~w~n", [CB])
     end,
     %% The settings store lives in the runtime STRUCT, so its size is board RAM
     %% spent whether or not anything is stored. csp.h picks 128 for ARDUINO and
     %% 1024 otherwise -- and "otherwise" is the host, which can be careless.
     %% A board that never says so takes the host's figure: on a 16K part that
     %% is a kilobyte of the struct, gone.
     %%
     %% An entry is 8 + strlen(path) bytes, so 256 is around twenty settings.
     case kv(P, settings_bytes, undefined) of
	 undefined -> [];
	 SB -> f("#define CSP_SETTINGS_BYTES ~w~n", [SB])
     end,
     case kv(P, no_eeprom, false) of
	 true -> f("#define CSP_NO_EEPROM   1~n", []);
	 _ -> []
     end,
     eeprom_defs(EE),
     %% The ADC map, derived from the pins the board muxed to the converter.
     %% A pin named 'ad0.2' (17xx) or 'ain2' (LPC2000) IS channel 2 -- the
     %% number is in the function name, so it does not have to be stated twice.
     adc_map([{Pin, F} || {pin, Pin, F} <- P]),
     case kv(P, boot_led, undefined) of
	 undefined -> [];
	 Led0 ->
	     %% {boot_led, Pin} or {boot_led, Pin, active_low}. The polarity is
	     %% a wiring fact and it is not guessable: an LED to ground lights
	     %% when the pin is high, one to Vcc when it is low, and both are
	     %% ordinary. Getting it backwards makes "off" look like a board
	     %% that is stuck on.
	     {Led, Act} = case Led0 of
			      {L, active_low} -> {L, 0};
			      {L, active_high} -> {L, 1};
			      L -> {L, 1}
			  end,
	     {LP, LB} = pin_split(Led),
	     f("\n// A LED for the boot code, before anything else exists.\n"
	       "#define CSP_BOOT_LED_PORT ~w\n"
	       "#define CSP_BOOT_LED_PIN  ~w\n"
	       "#define CSP_BOOT_LED_ON   ~w\n", [LP, LB, Act])
     end,
     %% Blink how far setup got. Costs a second of blinking at every boot, so it
     %% is opt-in per board -- but on a board that will not talk it is the only
     %% way to find out where it stopped.
     case kv(P, boot_marks, false) of
	 true -> f("#define CSP_BOOT_BLINK  1~n", []);
	 _ -> []
     end,
     %% The startup blink -- three flashes and then the core clock as a count.
     %% ON by default: on a board that has never talked it is the only thing
     %% that separates "not running" from "running, console wrong", and it
     %% reports the clock over a channel the clock cannot corrupt.
     %%
     %% Once the board DOES talk it is four seconds of waiting at every flash,
     %% and the banner says the same clock in figures. {boot_blink, false} then.
     %% It turns off only the startup blink: the boot marks and the exception
     %% blink still use the LED, which is why this is not the same as dropping
     %% {boot_led,...}.
     case kv(P, boot_blink, true) of
	 false -> f("#define CSP_BOOT_BLINK_OFF 1~n", []);
	 _ -> []
     end,
     case kv(P, console, undefined) of
	 {U, Baud} ->
	     %% The console's own pins, re-applied at UART init. Found by
	     %% matching the console's txd/rxd against what the board muxed --
	     %% so it says nothing when the board never named them.
	     UN = string:lowercase(atom_to_list(U)),
	     Tx = list_to_atom("txd" ++ lists:nthtail(4, UN)),
	     Rx = list_to_atom("rxd" ++ lists:nthtail(4, UN)),
	     Cp = [{Pin, F} || {pin, Pin, F} <- P, (F =:= Tx) orelse (F =:= Rx)],
	     [f("#define CSP_LPC_UART    LPC_~s~n"
		"#define CSP_LPC_BAUD    ~w~n",
		[string:uppercase(atom_to_list(U)), Baud]),
	      case Cp of
		  [] -> [];
		  _ -> f("#define CSP_LPC_CONSOLE_PINMUX do { ~s } while (0)~n",
			 [[begin
			       {Po, B} = pin_split(Pin),
			       Fs = element(1, {[], []}),
			       _ = Fs,
			       f("Chip_IOCON_PinMux(LPC_IOCON_ARG, ~w, ~w, 0, ~w); ",
				 [Po, B, console_func(F)])
			   end || {Pin, F} <- Cp]])
	      end];
	 _ -> []
     end,
     %% The bus csp_lpcopen.c actually opens. It looks for CSP_CAN_PORT and
     %% CSP_CAN_BITRATE -- unnumbered -- and without both it compiles the no-bus
     %% stub instead: init succeeds, recv always says "nothing", and the node is
     %% silent in a way that looks exactly like working.
     %%
     %% The numbered ones below are the whole picture, for when more than one
     %% bus is supported. The first {can,...} is the one that gets opened.
     case lists:sort([{N, O} || {can, N, O} <- P]) of
	 [] -> [];
	 [{N1, O1}|_] ->
	     f("#define CSP_CAN_PORT    LPC_CAN~w~n"
	       "#define CSP_CAN_BITRATE ~w~n", [N1, kv(O1, bitrate, 0)])
     end,
     [f("#define CSP_CAN~w_BITRATE ~w~n", [N, kv(Opts, bitrate, 0)])
      || {can, N, Opts} <- P],
     f("~n// PCONP: the peripherals this board uses. Written WHOLE, so anything~n"
       "// not in {enable,...} ends up powered down rather than left at its~n"
       "// reset value -- several of these come up on.~n"
       "#define CSP_PCONP_VALUE 0x~8.16.0BUL~n~n", [pconp(P, G)]),
     f("// Pin mux, as (port, bit, function) triples for csp_board_init.~n"
       "#define CSP_BOARD_PINS \\~n", []),
     pin_macro(Muxed, Pins),
     f("~n#define CSP_BOARD_NPINS ~w~n~n#endif~n", [length(Muxed)])].

%% txd0/rxd0 are function 1 on every LPC2000 and 17xx UART pin this covers.
%% Stated rather than looked up because the pin table is keyed by family and
%% this runs before that is resolved; check-boards validates the pin anyway.
console_func(_) -> 1.

%% 'ad0.2' -> 2, 'ain2' -> 2, anything else -> false.
adc_chan(F) ->
    case atom_to_list(F) of
	"ad0." ++ N -> catch_int(N);
	"ain" ++ N  -> catch_int(N);
	_ -> false
    end.

catch_int(S) -> case string:to_integer(S) of {N, ""} -> N; _ -> false end.

adc_map(Muxed) ->
    Ch = [{Pin, C} || {Pin, F} <- Muxed, (C = adc_chan(F)) =/= false],
    case Ch of
	[] -> [];
	_ ->
	    [f("\n// Which ADC channel each muxed pin is, so a .csp can name the\n"
	       "// connector pin (`in 0:25`) instead of the channel (`15:2`).\n"
	       "#define CSP_BOARD_ADC \\\n", []),
	     [begin
		  {Po, B} = pin_split(Pin),
		  f("    { ~w, ~2w, ~w }~s~n",
		    [Po, B, C, case I < length(Ch) of true -> ", \\"; false -> "" end])
	      end || {I, {Pin, C}} <- lists:zip(lists:seq(1, length(Ch)), Ch)],
	     f("#define CSP_BOARD_NADC ~w~n", [length(Ch)])]
    end.

pin_macro([], _) -> f("    /* none */~n", []);
pin_macro(Muxed, Pins) ->
    N = length(Muxed),
    [begin
	 {Port, Bit} = pin_split(Pin),
	 Fs = case [X || {Po, Bi, X} <- Pins, Po =:= Port, Bi =:= Bit] of
		  [X|_] -> X; [] -> []
	      end,
	 F = case index_of(Fn, Fs) of false -> 0; I -> I end,
	 %% Every line but the last carries a continuation. Without it the
	 %% macro is ONE LINE long and everything after it is stray syntax at
	 %% file scope -- which the compiler does report, but as a hundred
	 %% errors starting well after the cause.
	 f("    { ~w, ~2w, ~w }~s  /* ~s = ~w */~s~n",
	   [Port, Bit, F, case I0 < N of true -> ","; false -> " " end,
	    atom_to_list(Pin), Fn,
	    case I0 < N of true -> " \\"; false -> "" end])
     end || {I0, {Pin, Fn}} <- lists:zip(lists:seq(1, N), Muxed)].

%% --- checking ----------------------------------------------------------------

check_board(Db, {Name, P}) ->
    case kv(P, toolchain, bare) of
	arduino_cli -> check_arduino(Name, P);
	_ -> check_bare(Db, {Name, P})
    end.

%% An Arduino board names no chip and mixes no pins: arduino-cli owns the core,
%% the mux and the link. What CAN be wrong here is the description itself, and
%% an FQBN that is merely absent would surface as an arduino-cli usage error
%% three layers down.
check_arduino(Name, P) ->
    case kv(P, fqbn, undefined) of
	undefined ->
	    io:format("~s: ERROR {toolchain, arduino_cli} but no {fqbn, \"...\"}~n",
		      [Name]), 1;
	F when is_list(F) -> check_platform(Name, P, F);
	F ->
	    io:format("~s: ERROR {fqbn, ~p} must be a string~n", [Name, F]), 1
    end.

%% An FQBN is <packager>:<arch>:<board>, and the platform it needs is the first
%% two fields. Stating the platform separately is what pins its VERSION, so the
%% two have to agree -- a profile naming arduino:avr for a samd board builds the
%% wrong core, and arduino-cli would report that as a missing board.
check_platform(Name, P, Fqbn) ->
    case [{I, V} || {platform, I, V} <- P] ++
	 [{I, V} || {platform, I, V, _} <- P] of
	[] -> 0;                      %% no version pinned; allowed
	[{Id, _Ver} | _] ->
	    case string:lexemes(Fqbn, ":") of
		[Pk, Ar | _] ->
		    Want = Pk ++ ":" ++ Ar,
		    case Id =:= Want of
			true -> 0;
			false ->
			    io:format("~s: ERROR {platform, \"~s\", ...} does not "
				      "match fqbn ~s (wanted ~s)~n",
				      [Name, Id, Fqbn, Want]), 1
		    end;
		_ ->
		    io:format("~s: ERROR fqbn ~s is not "
			      "<packager>:<arch>:<board>~n", [Name, Fqbn]), 1
	    end
    end.

check_bare(Db, {Name, P}) ->
    Chip = kv(P, chip, '?'),
    case lookup(Db, chip, Chip) of
	false ->
	    io:format("~s: ERROR no such chip '~w'~n", [Name, Chip]),
	    1;
	CP ->
	    G = resolve(Db, CP),
	    Pins = pin_table(Db, G),
	    Muxed = [{Pin, Fn} || {pin, Pin, Fn} <- P],
	    lists:sum([check_pin(Name, Pins, Pin, Fn) || {Pin, Fn} <- Muxed]) +
		check_dup(Name, Muxed) +
		check_clock(Name, P, G) +
		check_enable(Name, P, Muxed)
    end.

pin_table(Db, G) ->
    case kv(G, pin_table, undefined) of
	undefined -> [];
	T -> case [L || {pins, F, L} <- Db, F =:= T] of
		 [L|_] -> L;
		 [] -> []
	     end
    end.

%% 'P0.5' -> {0, 5}. The board writes what is printed on the silk screen.
pin_split(Atom) ->
    case string:lexemes(atom_to_list(Atom), "Pp.") of
	[A, B] -> {list_to_integer(A), list_to_integer(B)};
	_ -> error
    end.

check_pin(Board, Pins, Pin, Want) ->
    case pin_split(Pin) of
	error ->
	    io:format("~s: ERROR ~w is not a pin name (want P<port>.<bit>)~n",
		      [Board, Pin]), 1;
	{Port, Bit} ->
	    case [Fs || {Po, Bi, Fs} <- Pins, Po =:= Port, Bi =:= Bit] of
		[] ->
		    io:format("~s: UNKNOWN ~w -- not in the pin table, "
			      "cannot verify ~w~n", [Board, Pin, Want]),
		    0;                 %% the table's gap, not the board's fault
		[Fs|_] ->
		    case index_of(Want, Fs) of
			false ->
			    io:format("~s: ERROR ~w cannot be ~w (it can be:~s)~n",
				      [Board, Pin, Want,
				       [f(" ~w",[X]) || X <- Fs, X =/= '-']]),
			    1;
			_ -> 0
		    end
	    end
    end.

%% One pin, two jobs. Silicon has no opinion -- the second PINSEL write simply
%% wins -- so this is invisible at run time and shows up as one of the two
%% functions not working. The old cfg.h had exactly this: P0.20 is the CAN2
%% warning LED and the co-controller's slave select, with a comment saying so.
check_dup(Board, Muxed) ->
    Pins = [P || {P, _} <- Muxed],
    Dups = lists:usort([P || P <- Pins, count(P, Pins) > 1]),
    [io:format("~s: ERROR ~w is assigned twice (~s)~n",
	       [Board, P, [f(" ~w",[F]) || {Q, F} <- Muxed, Q =:= P]])
     || P <- Dups],
    length(Dups).

count(X, L) -> length([Y || Y <- L, Y =:= X]).

index_of(X, L) -> index_of(X, L, 0).
index_of(_, [], _) -> false;
index_of(X, [X|_], I) -> I;
index_of(X, [_|T], I) -> index_of(X, T, I+1).

%% The core clock against what the part is rated for. A board asking for more
%% than the silicon does is a board that runs until it warms up.
check_clock(Board, P, G) ->
    Want = kv(P, core, 0),
    %% Nested under {pll,...}, where it belongs -- it is a property of the PLL
    %% and not a loose number. Looking only at the top level found nothing and
    %% passed everything, which is the quiet way a check stops checking.
    Max = kv(kv(G, pll, []), cclk_max, 0),
    case (Max > 0) andalso (Want > Max) of
	true ->
	    io:format("~s: ERROR core ~w MHz exceeds the part's ~w MHz~n",
		      [Board, Want div 1000000, Max div 1000000]),
	    1;
	false -> 0
    end.

%% Enabled but never muxed, or muxed but never enabled. Both are half a job,
%% and neither half complains on its own: an unmuxed CAN controller is powered
%% and silent, a muxed one that is not powered ignores every write.
check_enable(Board, P, Muxed) ->
    Enabled = kv(P, enable, []),
    Funcs = [F || {_, F} <- Muxed],
    Need = [{can1, [rd1, td1]}, {can2, [rd2, td2]},
	    {uart0, [txd0, rxd0]}, {uart1, [txd1, rxd1]},
	    {uart2, [txd2, rxd2]}, {uart3, [txd3, rxd3]}],
    lists:sum(
      [begin
	   On = lists:member(Dev, Enabled),
	   Has = [X || X <- Pins, lists:member(X, Funcs)],
	   if On andalso (Has =:= []) ->
		   io:format("~s: MISSING ~w is enabled but no pins are muxed "
			     "for it~n", [Board, Dev]), 1;
	      (not On) andalso (Has =/= []) ->
		   io:format("~s: MISSING pins muxed for ~w but it is not in "
			     "{enable,...}~n", [Board, Dev]), 1;
	      true -> 0
	   end
       end || {Dev, Pins} <- Need]).

%% --- reading -----------------------------------------------------------------

%% Where terms are read from, and the ORDER IS THE PRECEDENCE: lookup/3 takes
%% the head, so whatever comes first wins. Private definitions therefore
%% override public ones, which is what makes a private repo useful -- a board
%% that must not be published can shadow a placeholder here.
%%
%%   $CSP_PATH   colon-separated, first, for a repo cloned anywhere
%%   private/    a clone parked inside the tree, gitignored
%%   chips/ boards/  the public tree
%%
%% Shadowing is REPORTED. A private definition quietly replacing a public one
%% is exactly the kind of thing that costs an afternoon when an edit to the
%% wrong copy has no effect.
term_files() ->
    Extra = case os:getenv("CSP_PATH") of
		false -> [];
		"" -> [];
		Path -> [D || D <- string:lexemes(Path, ":"), D =/= ""]
	    end,
    %% append, NOT flatten: a string IS a list, so flattening a list of file
    %% names concatenates them into one long char list.
    lists:append(
      [filelib:wildcard(D ++ "/**/*.terms") || D <- Extra] ++
      [filelib:wildcard("private/**/*.terms"),
       filelib:wildcard("chips/*/*.terms"),
       %% Both shapes: a board may be one file in boards/, or a DIRECTORY of
       %% its own holding the terms, its pins.csp and its build output. The
       %% second is what a board with more than one file wants, and it is the
       %% same shape a private repo has.
       filelib:wildcard("boards/*.terms"),
       filelib:wildcard("boards/*/*.terms")]).

load() ->
    Files = term_files(),
    Tagged = [{F, consult(F)} || F <- Files],
    warn_shadowed(Tagged),
    lists:flatten([Ts || {_, Ts} <- Tagged]).

%% Which file defines {Kind, Name}. The first in search order, which is the one
%% that wins -- see term_files/0 for why private comes before public.
term_file(Kind, Name) ->
    Hit = [F || F <- term_files(),
		lists:any(fun({K, N, _}) -> (K =:= Kind) andalso (N =:= Name);
			     (_) -> false
			  end, consult(F))],
    case Hit of
	[F|_] -> F;
	[] -> false
    end.

%% Two files defining the same {Kind, Name}. The first wins; say which.
warn_shadowed(Tagged) ->
    Named = [{{K, N}, F} || {F, Ts} <- Tagged, {K, N, _} <- Ts],
    Dups = [{Key, [F || {K2, F} <- Named, K2 =:= Key]}
	    || Key <- lists:usort([K || {K, _} <- Named])],
    [io:format(standard_error,
	       "gen_chips: ~w ~w defined in ~s -- shadowing ~s~n",
	       [K, N, hd(Fs), string:join(tl(Fs), ", ")])
     || {{K, N}, Fs} <- Dups, length(Fs) > 1],
    ok.

%% Name matching for the listings. An atom, so it comes back to a string first;
%% caseless because nobody types LPC2129 the same way twice.
match(MP, Name) ->
    re:run(atom_to_list(Name), MP, [{capture, none}]) =:= match.

consult(F) ->
    case file:consult(F) of
	{ok, Ts} -> Ts;
	{error, E} ->
	    io:format(standard_error, "gen_chips: ~s: ~p~n", [F, E]),
	    halt(1)
    end.

%% A {Kind, Name, Props} term by kind and name.
lookup(Db, Kind, Name) ->
    case [P || {K, N, P} <- Db, K =:= Kind, N =:= Name] of
	[P|_] -> P;
	[]    -> false
    end.

chips(Db) -> [{N, P} || {chip, N, P} <- Db].

kv(Props, K, Default) ->
    case lists:keyfind(K, 1, Props) of
	{K, V} -> V;
	false  -> Default
    end.

%% Flatten chip -> group -> family into one property list, nearest first: a
%% chip that states ram_base overrides its family's, and the lookup that finds
%% it first wins because kv/3 takes the head.
resolve(Db, ChipProps) ->
    GName = kv(ChipProps, group, undefined),
    GProps = case lookup(Db, group, GName) of false -> []; G -> G end,
    FName = kv(GProps, family, kv(ChipProps, family, undefined)),
    FProps = case lookup(Db, family, FName) of false -> []; F -> F end,
    Map = case lookup(Db, map, GName) of false -> []; M -> M end,
    %% A chip takes a PREFIX of its group's table. One geometry serves an
    %% LPC2131 (8 sectors, 32K) and an LPC2138 (27, 500K) -- iap_lpc.c carried
    %% the count per part for exactly this reason.
    All = expand(kv(GProps, sectors, [])),
    NSect = kv(ChipProps, nsect, length(All)),
    Sectors = lists:sublist(All, NSect),
    check_size(ChipProps, GProps, Sectors),
    %% csp_sectors_t.n and csp_region_t.first/last are uint8_t: this describes
    %% erase units we MANAGE, and a part with more than 255 of them is one whose
    %% flash somebody else owns (an AVR page, a SAMD row). Those parts carry no
    %% table at all -- refuse rather than emit a truncated one, which is what
    %% 1024 rows silently became.
    (length(Sectors) > 255) andalso
	begin
	    io:format(standard_error,
		      "gen_chips: ~w sectors is more than the 255 a region map "
		      "can address; a part whose flash is managed elsewhere "
		      "should carry {page,...} and no sector table~n",
		      [length(Sectors)]),
	    halt(1)
	end,
    %% DERIVED FIRST. kv/3 takes the head, so the expanded sector list has to
    %% come before the group's raw run-length form or the lookup finds the runs
    %% -- which generated `{8,8192}, {2,65536}` into a uint32_t array and called
    %% it three sectors.
    [{sectors, Sectors}, {map, Map}, {group, GName}, {family, FName}] ++
	ChipProps ++ GProps ++ FProps.

%% The sector table must FIT the part.
%%
%% sum(sectors) <= flash_kb, and no more than that is claimed. A table bigger
%% than the part is definitely wrong -- it is what the hand-written LPC1754
%% entry was, 160K of sectors on a 128K device -- while a table SMALLER than the
%% advertised size is normal and how much smaller is not uniform: an LPC2138 is
%% 500K of 512 because the boot block overlaps the top of its flash, an LPC2131
%% is 32K of 32 because its boot block lives elsewhere in the address space.
%%
%% So the check is the half that is certain. The shortfall is reported by
%% --list, where a number that looks wrong can be looked up, rather than being
%% asserted here from a rule that does not hold across the family.
check_size(Chip, Group, Sectors) ->
    Sum = lists:sum(Sectors),
    Want = kv(Chip, flash_kb, 0) * 1024,
    _ = Group,
    case (Want =:= 0) orelse (Sum =< Want) of
	true -> ok;
	false ->
	    io:format(standard_error,
		      "gen_chips: id ~.16B: ~w sectors = ~wK, which does not fit "
		      "a ~wK part~n",
		      [kv(Chip, id, 0), length(Sectors), Sum div 1024,
		       Want div 1024]),
	    halt(1)
    end.

%% [{8,8192},{2,65536}] -> a flat list of sizes. The runs are how a data sheet
%% states it and how a person checks it; the flat list is what C indexes.
expand(Runs) ->
    lists:flatten([lists:duplicate(N, Sz) || {N, Sz} <- Runs]).

%% --- writing -----------------------------------------------------------------

header(Chip, G) ->
    [banner(Chip),
     "#ifndef __CSP_CHIP_H__\n#define __CSP_CHIP_H__\n\n",
     "#include \"csp_flash.h\"\n\n",
     f("#define CSP_CHIP_NAME       \"~s\"\n", [Chip]),
     f("#define CSP_CHIP_GROUP      \"~s\"\n", [kv(G, group, '?')]),
     f("#define CSP_CHIP_FAMILY     \"~s\"\n", [kv(G, family, '?')]),
     f("#define CSP_CHIP_ID         0x~8.16.0BUL\n", [kv(G, id, 0)]),
     f("#define CSP_FLASH_BASE      0x~8.16.0BUL\n", [kv(G, flash_base, 0)]),
     f("#define CSP_FLASH_SECTORS   ~w\n", [length(kv(G, sectors, []))]),
     f("#define CSP_RAM_BASE        0x~8.16.0BUL\n", [kv(G, ram_base, 0)]),
     f("#define CSP_RAM_BYTES       ~w\n", [kv(G, ram_kb, 0) * 1024]),
     f("#define CSP_RAM_RESERVED    ~w\n", [kv(G, ram_reserve, 0)]),
     "\n// IAP: the entry point and the command numbers, out of the family file.\n",
     f("#define CSP_IAP_ENTRY       0x~8.16.0BUL\n", [kv(G, iap_entry, 0)]),
     [f("#define CSP_IAP_~s~*s ~w\n",
	[string:uppercase(atom_to_list(K)),
	 12 - length(atom_to_list(K)), "", V])
      || {K, V} <- kv(G, iap, [])],
     "\n// How many of each peripheral the PART has. Not how many are wired: a\n"
     "// board with one transceiver on a two-CAN part says so itself, and that\n"
     "// belongs in boards/, not here. This is the ceiling.\n",
     [f("#define CSP_HAS_~s~*s ~w\n",
	[string:uppercase(atom_to_list(K)),
	 12 - length(atom_to_list(K)), "", N])
      || {K, N} <- kv(G, peripherals, [])],
     case kv(G, store, undefined) of
	 undefined -> [];
	 Store -> f("\n// Where settings live on this part.\n"
		    "#define CSP_STORE_~s      1\n",
		    [string:uppercase(atom_to_list(Store))])
     end,
     "\nextern const csp_device_t csp_chip;\n\n",
     "#endif\n"].

source(Chip, G) ->
    Sectors = kv(G, sectors, []),
    Map = kv(G, map, []),
    [banner(Chip),
     "#include \"csp_chip.h\"\n\n",
     "static const uint32_t sect[] = {\n",
     rows(Sectors),
     "};\n\n",
     "static const csp_region_t reg[] = {\n",
     [region(R) || R <- Map],
     "};\n\n",
     f("const csp_device_t csp_chip = {\n"
       "    \"~s\",\n"
       "    { CSP_FLASH_BASE, sect, CSP_FLASH_SECTORS },\n"
       "    reg, ~w,\n"
       "    CSP_RAM_BYTES, CSP_RAM_BASE, CSP_RAM_RESERVED,\n"
       "    \"~s\", \"~s\"\n"
       "};\n", [Chip, length(Map),
		 kv(G, entry, '_start'), kv(G, vectors, '.vectors')])].

%% Eight to a line, so the shape of the table is visible: a run of small
%% sectors and then the big ones is the thing a reader is checking for.
rows([]) -> [];
rows(L) ->
    {Head, Tail} = lists:split(min(8, length(L)), L),
    [ "   ", [f(" ~w,", [S]) || S <- Head], "\n" | rows(Tail) ].

region({runtime, A, B}) ->
    f("    { \"runtime\", ~2w, ~2w, CSP_REG_RUNTIME },\n", [A, B]);
region({store, A, B}) ->
    f("    { \"store\",   ~2w, ~2w, CSP_REG_STORE   },\n", [A, B]);
region({patch, A, B}) ->
    f("    { \"eeprom\",  ~2w, ~2w, CSP_REG_PATCH   },\n", [A, B]);
region({app, Name, A, B}) ->
    %% 7 is strlen("runtime"), the longest fixed name, so an app slot lines its
    %% sector numbers up with the other two rows whatever it is called.
    f("    { \"~s\",~*s ~2w, ~2w, CSP_REG_APP     },\n",
      [Name, 7 - length(Name), "", A, B]).

device_source(Name, G) ->
    [banner(Name),
     "#include \"csp_flash.h\"\n\n",
     part(Name, G),
     "// Held in a variable rather than returned directly: the host harness\n"
     "// switches parts at run time, and a target that never calls\n"
     "// csp_device_set keeps the one it was built for.\n",
     f("static const csp_device_t* active = &csp_dev_~s;\n\n", [Name]),
     "const csp_device_t* csp_device(void)\n"
     "{\n"
     "    return active;\n"
     "}\n\n"
     "void csp_device_set(const csp_device_t* d)\n"
     "{\n"
     "    if (d != 0)\n"
     "\tactive = d;\n"
     "}\n"].

all_source(Chips) ->
    [banner("every part"),
     "#include \"csp_flash.h\"\n\n",
     [part(N, G) || {N, G} <- Chips],
     "\nstatic const csp_device_t* const parts[] = {\n",
     [f("    &csp_dev_~s,\n", [N]) || {N, _} <- Chips],
     "};\n\n",
     "int csp_part_count(void) { return "
     ++ integer_to_list(length(Chips)) ++ "; }\n",
     "const csp_device_t* csp_part_at(int i)\n"
     "{\n"
     "    if ((i < 0) || (i >= csp_part_count())) return 0;\n"
     "    return parts[i];\n"
     "}\n"].

part(Name, G) ->
    Sectors = kv(G, sectors, []),
    Map = kv(G, map, []),
    [f("// ~s -- ~s, ~w sectors\n", [Name, kv(G, group, '?'), length(Sectors)]),
     f("static const uint32_t sect_~s[] = {\n", [Name]),
     rows(Sectors),
     "};\n",
     case Map of
	 [] -> f("#define reg_~s 0\n#define nreg_~s 0\n", [Name, Name]);
	 _  -> [f("static const csp_region_t reg_~s[] = {\n", [Name]),
		[region(R) || R <- Map],
		"};\n",
		f("#define nreg_~s ~w\n", [Name, length(Map)])]
     end,
     f("const csp_device_t csp_dev_~s = {\n"
       "    \"~s\",\n"
       "    { 0x~8.16.0BUL, sect_~s, ~w },\n"
       "    reg_~s, nreg_~s,\n"
       "    ~w, 0x~8.16.0BUL, ~w,\n"
       "    \"~s\", \"~s\"\n"
       "};\n\n",
       [Name, Name, kv(G, flash_base, 0), Name, length(Sectors),
	Name, Name,
	kv(G, ram_kb, 0) * 1024, kv(G, ram_base, 0), kv(G, ram_reserve, 0),
	kv(G, entry, '_start'), kv(G, vectors, '.vectors')])].

%% One MEMORY entry per region, plus the RAM, plus the SECTIONS that define the
%% symbols startup expects.
%%
%% Generated from the region map because the two say the SAME THING -- `runtime
%% 0..7` and `LENGTH = 0x10000` -- and two copies of a statement drift. This way
%% the LINKER enforces the map: an interpreter that outgrows its region fails to
%% link, loudly, instead of being flashed over the top of an application slot.
ld_script(Chip, G) ->
    Regions = kv(G, map, []),
    Sectors = kv(G, sectors, []),
    Base = kv(G, flash_base, 0),
    [f("/* generated by `gen_chips.erl --ld ~s` -- do not edit.~n"
       " * The region map in chips/<vendor>/*.terms is the source. */~n"
       "MEMORY~n{~n", [Chip]),
     [ld_region(R, Sectors, Base) || R <- Regions],
     %% The reserve comes off the BOTTOM, where the part keeps whatever it keeps
     %% -- 64 bytes of remapped vectors on an LPC2000. Stating it here rather
     %% than in a hand-written script is the same argument as the flash regions.
     f("  ~-8s ~-4s : ORIGIN = 0x~8.16.0B, LENGTH = 0x~8.16.0B~s~n}~n~n",
       ["DATA", "(rw)",
	kv(G, ram_base, 0) + kv(G, ram_reserve, 0),
	kv(G, ram_kb, 0)*1024 - kv(G, ram_reserve, 0),
	case kv(G, ram_reserve, 0) of
	    0 -> "";
	    N -> f("   /* ~w reserved at the bottom */", [N])
	end]),
     ld_sections(G, Regions)].

ld_region({K, A, B}, Sectors, Base) -> ld_region_(reg_name(K), K, A, B, Sectors, Base);
ld_region({app, N, A, B}, Sectors, Base) -> ld_region_(N, app, A, B, Sectors, Base).

reg_name(runtime) -> "runtime";
reg_name(store)   -> "store";
reg_name(patch)   -> "eeprom".

ld_region_(Name, Kind, A, B, Sectors, Base) ->
    Off = lists:sum(lists:sublist(Sectors, A)),
    Len = lists:sum(lists:sublist(Sectors, A+1, B-A+1)),
    f("  ~-8s ~-4s : ORIGIN = 0x~8.16.0B, LENGTH = 0x~8.16.0B   /* sector~s ~w~s */~n",
      [Name, case Kind of runtime -> "(rx)"; _ -> "(r)" end,
       Base + Off, Len,
       case A =:= B of true -> ""; false -> "s" end, A,
       case A =:= B of true -> ""; false -> f("..~w", [B]) end]).

ld_sections(G, Regions) ->
    case [R || R <- Regions, element(1, R) =:= runtime] of
	[] -> [];      %% geometry only: MEMORY alone is the answer
	_ ->
	    f("ENTRY(~s)~n~n"
	      "SECTIONS~n"
	      "{~n"
	      "  /* The vector table has to be the first thing at the flash~n"
	      "   * base: the core fetches reset from offset 0 and nothing~n"
	      "   * relocates it. */~n"
	      "  .text : {~n"
	      "    KEEP(*(~s))~n"
	      "    *(.text*)~n"
	      "    *(.rodata*)~n"
	      "    *(.glue_7) *(.glue_7t)~n"
	      "    . = ALIGN(4);~n"
	      "    /* The image registry: an ORPHAN section otherwise, placed~n"
	      "     * by a heuristic that differs by ld version -- and one that~n"
	      "     * put it on top of .data here. KEEP because nothing~n"
	      "     * references it: it is found by walking the two symbols. */~n"
	      "    __start_csp_images = .;~n"
	      "    KEEP(*(csp_images))~n"
	      "    __stop_csp_images = .;~n"
	      "    . = ALIGN(4);~n"
	      "    /* Unwind tables. Never used -- nothing here throws -- but~n"
	      "     * the compiler emits them and they are orphans too. */~n"
	      "    *(.ARM.exidx*)~n"
	      "    *(.ARM.extab*)~n"
	      "  } > runtime~n"
	      "  _data_load = .;~n~n"
	      "  .data : AT (_data_load) {~n"
	      "    _data_start = .;~n"
	      "    *(.data*)~n"
	      "    . = ALIGN(4);~n"
	      "    _data_end = .;~n"
	      "  } > DATA~n~n"
	      "  .bss (NOLOAD) : {~n"
	      "    _bss_start = .;~n"
	      "    *(.bss*)~n"
	      "    *(COMMON)~n"
	      "    . = ALIGN(4);~n"
	      "    _bss_end = .;~n"
	      "  } > DATA~n~n"
	      "  /* The stack grows DOWN from the top of RAM. */~n"
	      "  _stack_top = 0x~8.16.0B;~n"
	      "  _heap_start = _bss_end;~n~n"
	      "  /* The same two under the names an LPCXpresso script uses:~n"
	      "   * csp_lpcopen.c is written against the LPC ecosystem and asks~n"
	      "   * for these, and a port should not have to care which script~n"
	      "   * produced the numbers. */~n"
	      "  _pvHeapStart = _heap_start;~n"
	      "  _vStackTop   = _stack_top;~n"
	      "}~n",
	      [kv(G, entry, '_start'), kv(G, vectors, '.vectors'),
	       kv(G, ram_base, 0) + kv(G, ram_kb, 0)*1024])
    end.

banner(Chip) ->
    f("// GENERATED by utils/gen_chips.erl for ~s -- do not edit.\n"
      "// The source is chips/<vendor>/*.terms; edit those.\n\n", [Chip]).

f(Fmt, Args) -> io_lib:format(Fmt, Args).
