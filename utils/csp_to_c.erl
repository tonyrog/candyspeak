%%% @doc
%%%   CandySpeak's COMPILED program to C -- from the instruction stream, not
%%%   the source.
%%%
%%%     csp_to_c:file("prog.csp", "prog.c")     compile with ./csp, translate
%%%     csp_to_c:file("prog.dump", "prog.c")    a dump made earlier
%%%
%%%   The input is `./csp -P', which prints the program as Erlang terms: the
%%%   declarations, every instruction, and the tables the runtime builds from
%%%   them. A .csp is compiled first and its dump kept beside the output
%%%   (prog.dump), so the two can be read side by side.
%%%
%%%   The instruction set is REGISTER code, so the translation is one C
%%%   statement per instruction and a label per jump target:
%%%
%%%     {instr,68,'LT',[r2,r0,r1]}         r2 = CSP_LIB_BOOL(r0 < r1);
%%%     {instr,70,'RULE',[r2,5,implicit]}  if (!(CSP_ON(1) && GATE && r2)) goto L75;
%%%     {instr,74,'ST',[r0,{v,10}]}        out->A = r0;
%%%     {instr,76,'ENTER','M',...}          static void M_run(M_t* in, M_t* out)
%%%     {instr,85,'NEW',"M","m1",...}       M_run(&in->m1, &out->m1);
%%%
%%%   The C it writes keeps the contract of utils/candyspeak_c.erl --
%%%   csp_lib_setup/csp_lib_step, csp_in/csp_out, the host name table -- so
%%%   port/csp_lib_host.c runs it and tests/clib_oracle.escript can hold it
%%%   against ./csp. It is not meant to be READ the way candyspeak_c's output
%%%   is: `r2 = r0 < r1' is the program after the compiler has had it. It shows
%%%   that the bytecode IS the semantics, and its templates are what a JIT's
%%%   would be.
%%%
%%%   A DRAFT. Not yet: parts (LDP/STP), arrays (SETOX), floats, buffers and
%%%   fields, latch/progress/trunc/round, the reactive mode. Each is refused by
%%%   name. An instance's own `> m.P = v' patch is not in the dump, so it is
%%%   not here either -- a global #param's is, through its `value'.
%%% @end
-module(csp_to_c).

-export([file/2, main/1]).

-define(IND, "    ").

main([In]) -> main([In, filename:rootname(In) ++ ".c"]);
main([In, Out]) ->
    case file(In, Out) of
        ok -> halt(0);
        {error, E} -> io:format(standard_error, "~s: ~p\n", [In, E]), halt(1)
    end.

file(In, Out) ->
    case dump(In, Out) of
        {ok, Terms} ->
            try gen(model(Terms), In) of
                Text -> file:write_file(Out, Text)
            catch
                throw:{unsupported, What} ->
                    io:format(standard_error, "~s: not translated to C: ~s\n",
                              [In, What]),
                    {error, unsupported}
            end;
        Error -> Error
    end.

%% A .csp is compiled by csp (CSP=... to name another, else the one in the
%% tree this module came from, else ./csp), and the dump it printed is kept
%% beside the output. One cycle is run so a #param's value is the one a `>'
%% line in the program set.
dump(In, Out) ->
    case filename:extension(In) of
        ".csp" ->
            Csp = csp_exe(),
            Dump = filename:rootname(Out) ++ ".dump",
            Out1 = os:cmd(Csp ++ " -n -P -c 1 " ++ In ++ " 2>&1"),
            %% The terms only: a `>' line in the program echoes its value on
            %% the same stdout, and so does anything the cycle printed.
            Keep = [L || L <- string:split(Out1, "\n", all), is_dump_line(L)],
            case [L || L <- Keep, lists:prefix("{instr", L)] of
                [] ->
                    io:format(standard_error, "~s: ~s gave no program:\n~s",
                              [In, Csp, Out1]),
                    {error, no_dump};
                _ ->
                    ok = file:write_file(Dump, lists:join("\n", Keep)),
                    file:consult(Dump)
            end;
        _ ->
            file:consult(In)
    end.

csp_exe() ->
    Tree = filename:dirname(filename:dirname(code:which(?MODULE))),
    Found = [C || C <- [os:getenv("CSP"), filename:join(Tree, "csp"), "./csp"],
                  C =/= false, filelib:is_regular(C)],
    case Found of
        [C | _] -> C;
        [] -> "csp"
    end.

is_dump_line([${ | _]) -> true;
is_dump_line([$], $} | _]) -> true;
is_dump_line([$%, $% | _]) -> true;
is_dump_line([$\s | T]) -> is_dump_line(string:trim(T, leading));
is_dump_line(_) -> false.

%% ------------------------------------------------------------------
%% The model: the dump, indexed
%% ------------------------------------------------------------------
%%
%%   decls    index -> {Kind, Name, Opts}        every declaration, flat
%%   owner    index -> module name | main        where a declaration lives
%%   modules  name -> [index]                    members, in order
%%   objects  number -> {Module, Instance}       {2,v,20} is object 2's
%%   main     [index]                            Main's own declarations
%%   code     [instr]                            Main's instructions
%%   bodies   module -> [instr]                  each module's ENTER..LEAVE
%%   states   [{Value, Name}]
%%   strings  handle -> text

model(Terms) ->
    M0 = #{decls => #{}, owner => #{}, modules => #{}, objects => #{},
           main => [], code => [], bodies => #{}, states => [],
           strings => #{}, input => [], output => []},
    M1 = lists:foldl(fun term/2, M0, Terms),
    M1#{main := lists:reverse(maps:get(main, M1)),
        code := lists:reverse(maps:get(code, M1)),
        ruleno => number_rules(Terms)}.

term({decl, I, module, Name, Members}, M) ->
    Mod = atom_to_list(Name),
    lists:foldl(fun(D, Acc) -> member(Mod, D, Acc) end,
                M#{modules := (maps:get(modules, M))#{Mod => []},
                   order => maps:get(order, M, []) ++ [{I, Mod}]}, Members);
term({decl, I, object, Mod, Inst}, M) ->
    M#{decls := (maps:get(decls, M))#{I => {object, atom_to_list(Inst),
                                            [{module, atom_to_list(Mod)}]}},
       main := [I | maps:get(main, M)]};
term({decl, _, states, L}, M) ->
    M#{states := maps:get(states, M) ++ L};
term({decl, _, 'end'}, M) -> M;
term({decl, I, Kind, Name, Opts}, M) ->
    M#{decls := (maps:get(decls, M))#{I => {Kind, Name, Opts}},
       main := [I | maps:get(main, M)]};
term({instr, _, 'ENTER', Mod, _, Body}, M) ->
    M#{bodies := (maps:get(bodies, M))#{atom_to_list(Mod) => Body}};
term({instr, _, _, _} = I, M) ->
    M#{code := [I | maps:get(code, M)]};
term({instr, _, 'NEW', _, _, _} = I, M) ->
    M#{code := [I | maps:get(code, M)]};
term({instr, _, _} = I, M) ->
    M#{code := [I | maps:get(code, M)]};
term({input, L}, M) -> M#{input := L};
term({disabled, L}, M) -> M#{disabled => L};
term({settings, L}, M) -> M#{settings => L};
term({output, L}, M) -> M#{output := L};
term({strings, L}, M) -> M#{strings := maps:from_list(L)};
term({object, [global | L]}, M) ->
    Objs = maps:from_list(
             [{N, {atom_to_list(Mod), Q}}
              || {N, {Mod, _, {q, Q}}} <- lists:zip(lists:seq(1, length(L)), L)]),
    M#{objects := Objs};
term(_, M) -> M.

member(_, {decl, _, 'end'}, M) -> M;
member(Mod, {decl, I, Kind, Name, Opts}, M) ->
    Mods = maps:get(modules, M),
    M#{decls := (maps:get(decls, M))#{I => {Kind, Name, Opts}},
       owner := (maps:get(owner, M))#{I => Mod},
       modules := Mods#{Mod => maps:get(Mod, Mods) ++ [I]}};
member(_, _, M) -> M.

%% Rule N is the Nth RULE in instruction order -- module bodies included,
%% where their ENTER stands. The runtime's numbering, by construction.
number_rules(Terms) ->
    Is = flat_instrs([T || {instr, _, _, _} = T <- Terms] ++
                     [T || {instr, _, 'ENTER', _, _, _} = T <- Terms] ++
                     [T || {instr, _, 'NEW', _, _, _} = T <- Terms] ++
                     [T || {instr, _, _} = T <- Terms]),
    Rules = lists:sort([N || {instr, N, 'RULE', _} <- Is]),
    maps:from_list(lists:zip(Rules, lists:seq(1, length(Rules)))).

flat_instrs(L) ->
    lists:flatmap(fun({instr, _, 'ENTER', _, _, Body} = E) -> [E | Body];
                     (I) -> [I] end, L).

decl(I, M) -> maps:get(I, maps:get(decls, M)).

%% The name a field gets: the declaration's, or d<index> for one the compiler
%% left nameless -- a #local formula, a timer's start time.
fname(I, M) ->
    case decl(I, M) of
        {_, "", _} -> "d" ++ integer_to_list(I);
        {_, Name, _} -> Name
    end.

%% The Sys module and its instance are the runtime's, not the program's.
skip(I, M) ->
    case decl(I, M) of
        {object, "sys", _} -> true;
        {_, _, _} -> timer_start(I, M)
    end.

%% A timer is two declarations: the timer, and an unnamed variable after it
%% holding when it was started. csp_timer_t has both.
timer_start(I, M) ->
    case maps:find(I - 1, maps:get(decls, M)) of
        {ok, {timer, _, _}} -> true;
        _ -> false
    end.

%% ------------------------------------------------------------------
%% The program
%% ------------------------------------------------------------------

gen(M, In) ->
    %% In declaration order: a module is declared before anything instantiates
    %% it, so its struct comes first.
    Mods = [Mod || {_, Mod} <- lists:keysort(1, maps:get(order, M, [])), Mod =/= "Sys"],
    [header(In, M),
     [struct(Mod, maps:get(Mod, maps:get(modules, M)), M) || Mod <- Mods],
     struct("Main", maps:get(main, M), M),
     "static Main_t csp_in, csp_out;\n\n",
     [module(Mod, M) || Mod <- Mods],
     module("Main", M),
     driver(M)].

header(In, M) ->
    ["// Generated by utils/csp_to_c.erl from the COMPILED program -- the\n",
     "// instruction stream of ", In, ", one C statement per instruction.\n",
     "// Do not edit. utils/candyspeak_c.erl translates the source instead,\n",
     "// and that is the one to read.\n\n",
     "#include <stdint.h>\n#include <string.h>\n#include \"csp_lib.h\"\n",
     "#if defined(CSP_LIB_HOST)\n#include \"csp_lib_host.h\"\n#endif\n\n",
     "static uint32_t csp_now;\nstatic uint32_t csp_cycle;\n\n",
     "enum {\n",
     [[?IND, Name, " = ", integer_to_list(V), ",\n"] || {V, Name} <- maps:get(states, M)],
     "};\n\n"].

struct(Name, Is, M) ->
    ["typedef struct {\n",
     case lists:any(fun(I) -> element(2, decl(I, M)) =:= "State" end, Is) of
         true -> [];
         false -> [?IND, "int32_t State;\n"]
     end,
     [field(I, M) || I <- Is, not skip(I, M)],
     "} ", Name, "_t;\n\n"].

field(I, M) ->
    N = fname(I, M),
    case decl(I, M) of
        {object, _, Opts} -> [?IND, proplists:get_value(module, Opts), "_t ", N, ";\n"];
        {timer, _, _} -> [?IND, "csp_timer_t ", N, ";\n"];
        {digital, _, _} -> [?IND, "uint32_t ", N, ":1;\n"];
        {Kind, _, Opts} when Kind =:= variable; Kind =:= local;
                             Kind =:= constant; Kind =:= analog ->
            [?IND, ctype(Opts, N), " ", N, width(Opts), ";\n"];
        {Kind, _, _} ->
            throw({unsupported, io_lib:format("declaration ~p ~s", [Kind, N])})
    end.

ctype(Opts, N) ->
    case proplists:get_value(type, Opts, integer) of
        integer -> "int32_t";
        unsigned -> "uint32_t";
        T -> throw({unsupported, io_lib:format("~p ~s", [T, N])})
    end.

width(Opts) ->
    case proplists:get_value(size, Opts, 32) of
        32 -> "";
        W -> [":", integer_to_list(W)]
    end.

%% ------------------------------------------------------------------
%% A module: init, timer and pin walks, and the run function
%% ------------------------------------------------------------------

module(Name, M) ->
    Is = case Name of
             "Main" -> maps:get(main, M);
             _ -> maps:get(Name, maps:get(modules, M))
         end,
    Code = case Name of
               "Main" -> maps:get(code, M);
               _ -> maps:get(Name, maps:get(bodies, M), [])
           end,
    Ctx = case Name of "Main" -> main; _ -> {module, Name} end,
    T = [Name, "_t"],
    [init(Name, Is, M),
     walk(Name, "timers_in", Is, M),
     walk(Name, "timers_out", Is, M),
     wait_fn(Name, Is, M),
     case Name of
         "Main" -> ["#if !defined(CSP_LIB_HOST)\n", io(M), "#endif\n\n"];
         _ -> []
     end,
     "static void ", Name, "_run(", T, "* in, ", T, "* out)\n{\n",
     body(Code, Ctx, M),
     ?IND, "if ((in->State == INIT) && (out->State == INIT))\n",
     ?IND, ?IND, "out->State = NORMAL;\n",
     ?IND, "if (in->State == FAILSAFE)\n",
     ?IND, ?IND, "out->State = FAILSAFE;\n",
     "}\n\n"].

%% The declared values, and then -- in Main_init, where every path starts --
%% what the program's `>' lines set, from the settings store.
init(Name, Is, M) ->
    ["static void ", Name, "_init(", Name, "_t* s)\n{\n",
     ?IND, "memset(s, 0, sizeof(*s));\n",
     [init1(I, M) || I <- Is, not skip(I, M)],
     case Name of "Main" -> instance_params(M); _ -> [] end,
     "}\n\n"].

%% The settings store: what the program's `>' lines set, by path. A value
%% (part 0) goes into the field the path names; a config part -- `.period',
%% `.pin' -- is not translated yet.
instance_params(M) ->
    [case Part of
         0 -> [?IND, "s->", Path, " = ", num(V), ";\n"];
         _ -> throw({unsupported, io_lib:format("setting ~s part ~p", [Path, Part])})
     end || {Path, Part, V} <- maps:get(settings, M, [])].

init1(I, M) ->
    N = fname(I, M),
    case decl(I, M) of
        {object, _, Opts} ->
            [?IND, proplists:get_value(module, Opts), "_init(&s->", N, ");\n"];
        %% `#timer T p = 1' is RUNNING from setup, as the runtime starts it --
        %% so an instance line that writes T in INIT does not stop it.
        {timer, _, Opts} ->
            [?IND, "s->", N, ".period = ", num(proplists:get_value(period, Opts)), ";\n",
             case proplists:get_value(value, Opts, 0) of
                 0 -> [];
                 _ -> [?IND, "s->", N, ".val = s->", N, ".running = 1;\n"]
             end];
        {Kind, _, Opts} when Kind =:= variable; Kind =:= local; Kind =:= constant ->
            case proplists:get_value(init, Opts, 0) of
                0 -> [];
                V -> [?IND, "s->", N, " = ", num(V), ";\n"]
            end;
        _ -> []
    end.

num(V) when is_integer(V), V >= 16#80000000 -> integer_to_list(V) ++ "u";
num(V) when is_integer(V) -> integer_to_list(V);
num(V) -> throw({unsupported, io_lib:format("value ~p", [V])}).

walk(Name, What, Is, M) ->
    T = [Name, "_t"],
    Lib = case What of "timers_in" -> "csp_lib_timer_in"; _ -> "csp_lib_timer_out" end,
    ["static void ", Name, "_", What, "(", T, "* in, ", T, "* out, uint32_t now)\n{\n",
     [case decl(I, M) of
          {timer, _, _} ->
              N = fname(I, M),
              [?IND, Lib, "(&in->", N, ", &out->", N, ", now);\n"];
          {object, N, Opts} ->
              [?IND, proplists:get_value(module, Opts), "_", What,
               "(&in->", N, ", &out->", N, ", now);\n"];
          _ -> []
      end || I <- Is, not skip(I, M)],
     ?IND, "(void)in; (void)out; (void)now;\n",
     "}\n\n"].

wait_fn(Name, Is, M) ->
    ["static void ", Name, "_wait(", Name, "_t* s, uint32_t now, uint32_t* w)\n{\n",
     [case decl(I, M) of
          {timer, _, _} ->
              N = fname(I, M),
              [?IND, "if (s->", N, ".running) {\n",
               ?IND, ?IND, "uint32_t dt = now - s->", N, ".t0;\n",
               ?IND, ?IND, "uint32_t r = (dt >= s->", N, ".period) ? 0 : s->",
               N, ".period - dt;\n",
               ?IND, ?IND, "if (r < *w) *w = r;\n",
               ?IND, "}\n"];
          {object, N, Opts} ->
              [?IND, proplists:get_value(module, Opts), "_wait(&s->", N, ", now, w);\n"];
          _ -> []
      end || I <- Is, not skip(I, M)],
     ?IND, "(void)s; (void)now; (void)w;\n",
     "}\n\n"].

%% The pins, from the runtime's own device lists.
io(M) ->
    Pins = [I || I <- maps:get(main, M), element(1, decl(I, M)) =:= digital orelse
                                         element(1, decl(I, M)) =:= analog],
    ["static void Main_config(Main_t* s)\n{\n",
     [pin_cfg(I, M) || I <- Pins],
     ?IND, "(void)s;\n}\n\n",
     "static void Main_input(Main_t* s)\n{\n",
     [pin_in(I, M) || {K, I} <- maps:get(input, M), K =:= d orelse K =:= a],
     ?IND, "(void)s;\n}\n\n",
     "static void Main_output(Main_t* s)\n{\n",
     [pin_out(I, M) || {K, I} <- maps:get(output, M), K =:= d orelse K =:= a],
     ?IND, "(void)s;\n}\n\n"].

pin(Opts) ->
    [integer_to_list(proplists:get_value(port, Opts, 0)), ", ",
     integer_to_list(proplists:get_value(pin, Opts))].

cdir(in) -> "CSP_CHIP_IN";
cdir(out) -> "CSP_CHIP_OUT";
cdir(inout) -> "CSP_CHIP_IN | CSP_CHIP_OUT";
cdir(_) -> "0".

pin_cfg(I, M) ->
    case decl(I, M) of
        {digital, _, Opts} ->
            [?IND, "csp_chip_dcfg(", pin(Opts), ", ", cdir(proplists:get_value(dir, Opts)), ", ",
             case proplists:get_value(pull, Opts) of
                 pullup -> "CSP_CHIP_PULLUP";
                 pulldown -> "CSP_CHIP_PULLDOWN";
                 _ -> "0"
             end, ");\n"];
        {analog, _, Opts} ->
            [?IND, "csp_chip_acfg(", pin(Opts), ", ", cdir(proplists:get_value(dir, Opts)), ", ",
             case proplists:get_value(pwm, Opts) of true -> "1"; _ -> "0" end, ");\n"]
    end.

asgn(Opts) -> case proplists:get_value(type, Opts) of unsigned -> "0"; _ -> "1" end.

pin_in(I, M) ->
    N = fname(I, M),
    case decl(I, M) of
        {digital, _, Opts} -> [?IND, "s->", N, " = csp_chip_din(", pin(Opts), ");\n"];
        {analog, _, Opts} ->
            [?IND, "s->", N, " = csp_lib_ain(csp_chip_ain(", pin(Opts), "), ",
             integer_to_list(proplists:get_value(size, Opts, 10)), ", ", asgn(Opts), ");\n"]
    end.

pin_out(I, M) ->
    N = fname(I, M),
    case decl(I, M) of
        {digital, _, Opts} -> [?IND, "csp_chip_dout(", pin(Opts), ", s->", N, ");\n"];
        {analog, _, Opts} ->
            [?IND, "csp_chip_aout(", pin(Opts), ", csp_lib_aout(s->", N, ", ",
             integer_to_list(proplists:get_value(size, Opts, 10)), ", ", asgn(Opts), "));\n"]
    end.

%% ------------------------------------------------------------------
%% The instructions
%% ------------------------------------------------------------------
%%
%% Registers are C locals, r0..r15; ARG slots are a0..a7. A jump is a goto to
%% the label of its target. `consts' follows what an LI put in a register, for
%% the two calls whose argument is not a value but a NAME: changed(X) is
%% passed X's declaration index, println("text") a string handle.

body(Code, Ctx, M) ->
    Targets = targets(Code),
    Regs = lists:usort([R || I <- Code, R <- regs(I)]),
    Args = lists:usort([A || {instr, _, 'ARG', [_, A]} <- Code]),
    End = case Code of [] -> 0; _ -> instr_no(lists:last(Code)) + 1 end,
    {Lines, _} = lists:mapfoldl(fun(I, C) -> instr(I, C, Ctx, M) end, #{}, Code),
    [[[?IND, "int32_t ", lists:join(", ", [[atom_to_list(R), " = 0"] || R <- Regs]), ";\n"]
      || Regs =/= []],
     [[?IND, "int32_t ", lists:join(", ", [["a", integer_to_list(A), " = 0"] || A <- Args]), ";\n"]
      || Args =/= []],
     [[case lists:member(instr_no(I), Targets) of
           true -> ["L", integer_to_list(instr_no(I)), ":\n"];
           false -> []
       end, L] || {I, L} <- lists:zip(Code, Lines)],
     case lists:any(fun(T) -> T >= End end, Targets) of
         true -> ["L", integer_to_list(End), ":\n", ?IND, ";\n"];
         false -> []
     end,
     [[?IND, "(void)", atom_to_list(R), ";\n"] || R <- Regs],
     [[?IND, "(void)a", integer_to_list(A), ";\n"] || A <- Args]].

instr_no(I) -> element(2, I).

%% Every jump lands on an instruction, or one past the last. nxt 0 is an
%% unclosed block, which the runtime treats as the end of the program.
targets(Code) ->
    End = case Code of [] -> 0; _ -> instr_no(lists:last(Code)) + 1 end,
    lists:usort(
      [case Nxt of 0 -> End; _ -> N + Nxt end
       || {instr, N, Op, Args} <- Code,
          Nxt <- case {Op, Args} of
                     {'RULE', [_, X | _]} -> [X];
                     {'INSTATE', [_, _, X]} -> [X];
                     {'NINSTATE', [_, _, X]} -> [X];
                     _ -> []
                 end]).

regs(I) when is_tuple(I) -> [A || A <- lists:flatten([tuple_to_list(I)]), is_reg(A)];
regs(_) -> [].

is_reg(A) when is_atom(A) ->
    case atom_to_list(A) of
        [$r | D] -> D =/= [] andalso lists:all(fun(C) -> C >= $0 andalso C =< $9 end, D);
        _ -> false
    end;
is_reg(_) -> false.

r(R) -> atom_to_list(R).

line(S) -> [?IND, S, "\n"].

%% One instruction -> {C, consts after it}.
instr({instr, _, 'SEGMENT', _}, C, _, _) -> {[], C};
instr({instr, _, 'SETO', _}, C, _, _) -> {[], C};      % the next access names it
instr({instr, _, 'NOP'}, C, _, _) -> {[], C};
instr({instr, _, 'LEAVE', _, _}, C, _, _) -> {[], C};   % the function's end
instr({instr, _, 'NEXT', _}, C, _, _) -> {"\n", C};     % a rule ends
instr({instr, _, 'NEW', Mod, Inst, _}, C, Ctx, _) ->
    {line([Mod, "_run(&", rd_base(Ctx), Inst, ", &", wr_base(Ctx), Inst, ");"]), C};
instr({instr, _, 'LI', [X, V]}, C, _, _) ->
    {line([r(X), " = ", integer_to_list(V), ";"]), C#{X => V}};
instr({instr, _, 'LIU', [X, V]}, C, _, _) ->
    {line([r(X), " = ", integer_to_list(V), ";"]), maps:remove(X, C)};
instr({instr, _, 'LIH', [X, V]}, C, _, _) ->
    {line([r(X), " = (int32_t)((uint32_t)", r(X), " | ((uint32_t)", integer_to_list(V),
           "u << 16));"]), maps:remove(X, C)};
instr({instr, _, 'LD', [X, Ref]}, C, Ctx, M) ->
    {line([r(X), " = ", read(Ref, Ctx, M), ";"]), maps:remove(X, C)};
instr({instr, _, Op, [X, Ref]}, C, Ctx, M) when Op =:= 'ST'; Op =:= 'STIMP' ->
    {line(write(Ref, r(X), Ctx, M)), C};
instr({instr, _, 'STI', [Ref, V]}, C, Ctx, M) ->
    {line(sti(Ref, V, Ctx, M)), C};
instr({instr, _, 'TMO', [X, Ref]}, C, Ctx, M) ->
    {line([r(X), " = CSP_LIB_BOOL(", copy(Ctx, path(Ref, Ctx, M), in), ".fired);"]),
     maps:remove(X, C)};
instr({instr, _, 'CHG', [X, Ref]}, C, Ctx, M) ->
    %% True on the first cycle whatever changed: that is what seeds a `<-'.
    {line([r(X), " |= CSP_LIB_BOOL(csp_cycle == 1 || ", read(Ref, Ctx, M), " != ",
           read_out(Ref, Ctx, M), ");"]),
     maps:remove(X, C)};
instr({instr, _, 'ARG', [X, A]}, C, _, _) ->
    {line(["a", integer_to_list(A), " = ", r(X), ";"]),
     C#{{arg, A} => maps:get(X, C, undefined)}};
instr({instr, _, 'CALL', [X, F, Types]}, C, Ctx, M) ->
    {call(r(X), atom_to_list(F), Types, C, Ctx, M), maps:remove(X, C)};
instr({instr, N, 'RULE', [X, Nxt | Imp]}, C, Ctx, M) ->
    No = integer_to_list(maps:get(N, maps:get(ruleno, M))),
    Gate = case Imp of
               [implicit] ->
                   S = state_ref(Ctx),
                   [" && (", S, " == INIT || ", S, " == NORMAL)"];
               [] -> []
           end,
    {line(["if (!(CSP_ON(", No, ")", Gate, " && ", r(X), ")) goto L",
           integer_to_list(N + Nxt), ";"]), C};
instr({instr, N, 'INSTATE', [X, S, Nxt]}, C, _, _) ->
    {line(["if (", r(X), " != ", integer_to_list(S), ") goto L", jump(N, Nxt), ";"]), C};
instr({instr, N, 'NINSTATE', [X, S, Nxt]}, C, _, _) ->
    {line(["if (", r(X), " == ", integer_to_list(S), ") goto L", jump(N, Nxt), ";"]), C};
instr({instr, _, 'LDP', [X, Ref, P]}, C, Ctx, M) ->
    {line([r(X), " = (int32_t)", copy(Ctx, path(Ref, Ctx, M), in), ".", part(Ref, P, M), ";"]),
     maps:remove(X, C)};
instr({instr, _, 'STP', [X, Ref, P]}, C, Ctx, M) ->
    {line([copy(Ctx, path(Ref, Ctx, M), out), ".", part(Ref, P, M), " = ", r(X), ";"]), C};
instr({instr, _, Op, [X, Y]}, C, _, _) when is_atom(Y) ->
    E = case Op of
            'MOV' -> r(Y);
            'NOT' -> ["CSP_LIB_BOOL(!", r(Y), ")"];
            'NEG' -> ["-", r(Y)];
            'BNOT' -> ["~", r(Y)];
            _ -> throw({unsupported, atom_to_list(Op)})
        end,
    {line([r(X), " = ", E, ";"]), maps:remove(X, C)};
instr({instr, _, Op, [X, Y, Z | U]}, C, _, _) when is_atom(Y), is_atom(Z) ->
    Un = U =:= [unsigned],
    Ry = r(Y), Rz = r(Z),
    UY = ["(uint32_t)", Ry], UZ = ["(uint32_t)", Rz],
    E = case {Op, Un} of
            {'ADD', _} -> [Ry, " + ", Rz];
            {'SUB', _} -> [Ry, " - ", Rz];
            {'MUL', _} -> [Ry, " * ", Rz];
            {'BAND', _} -> [Ry, " & ", Rz];
            {'BOR', _} -> [Ry, " | ", Rz];
            {'BXOR', _} -> [Ry, " ^ ", Rz];
            {'SLA', _} -> ["(int32_t)((uint32_t)", Ry, " << ", Rz, ")"];
            {'SRA', false} -> [Ry, " >> ", Rz];
            {'SRA', true} -> ["(int32_t)(", UY, " >> ", Rz, ")"];
            {'AND', _} -> ["CSP_LIB_BOOL(", Ry, " && ", Rz, ")"];
            {'OR', _} -> ["CSP_LIB_BOOL(", Ry, " || ", Rz, ")"];
            {'LT', false} -> ["CSP_LIB_BOOL(", Ry, " < ", Rz, ")"];
            {'LT', true} -> ["CSP_LIB_BOOL(", UY, " < ", UZ, ")"];
            {'LTE', false} -> ["CSP_LIB_BOOL(", Ry, " <= ", Rz, ")"];
            {'LTE', true} -> ["CSP_LIB_BOOL(", UY, " <= ", UZ, ")"];
            {'EQEQ', _} -> ["CSP_LIB_BOOL(", Ry, " == ", Rz, ")"];
            {'NEQ', _} -> ["CSP_LIB_BOOL(", Ry, " != ", Rz, ")"];
            {'DIV', false} -> ["csp_lib_div(", Ry, ", ", Rz, ")"];
            {'DIV', true} -> ["(int32_t)csp_lib_divu(", UY, ", ", UZ, ")"];
            {'REM', false} -> ["csp_lib_rem(", Ry, ", ", Rz, ")"];
            {'REM', true} -> ["(int32_t)csp_lib_remu(", UY, ", ", UZ, ")"];
            _ -> throw({unsupported, atom_to_list(Op)})
        end,
    {line([r(X), " = ", E, ";"]), maps:remove(X, C)};
instr(I, _, _, _) ->
    throw({unsupported, io_lib:format("~p", [I])}).

%% A part of a declaration, as csp_part_t numbers them: so far a timer's.
part(Ref, P, M) ->
    {_, _, N} = path(Ref, x, M),
    case {decl(N, M), P} of
        {{timer, _, _}, 8} -> "period";
        {{timer, _, _}, 9} -> "fired";
        {{Kind, Name, _}, _} ->
            throw({unsupported, io_lib:format("part ~p of ~p ~s", [P, Kind, Name])})
    end.

jump(N, 0) -> integer_to_list(N + 1000000);   % never: see targets/1
jump(N, Nxt) -> integer_to_list(N + Nxt).

%% The State a bare rule is gated on: the program's, from anywhere -- the
%% runtime reads it through st->gsx.
state_ref(main) -> "in->State";
state_ref({module, _}) -> "csp_in.State".

%% ------------------------------------------------------------------
%% Memory
%% ------------------------------------------------------------------
%%
%%   {v,N} {c,N} {d,N} {a,N} {t,N}   Main's declaration N
%%   {cur,K,N}                        member N of the instance being run
%%   {O,K,N}                          member N of object O (after a SETO)
%%
%% A read is the committed copy, a write the working one -- except a #local,
%% which the runtime single-buffers: written, it is readable at once, so it
%% is written to both.

rd_base(main) -> "in->";
rd_base({module, _}) -> "in->".
wr_base(main) -> "out->";
wr_base({module, _}) -> "out->".

%% Main's declaration N from wherever we are: a module reaches the globals
%% through the program's two copies.
gbase(main, in) -> "in->";
gbase(main, out) -> "out->";
gbase({module, _}, in) -> "csp_in.";
gbase({module, _}, out) -> "csp_out.".

rd_base(Ctx, I, M) ->
    case maps:find(I, maps:get(owner, M)) of
        {ok, _} -> "in->";
        error -> gbase(Ctx, in)
    end.

%% The C path of a reference, without its copy: "A", "m1.V".
path({cur, _, N}, _, M) -> {member, fname(N, M), N};
path({O, _, N}, _, M) when is_integer(O) ->
    {Mod, Q} = maps:get(O, maps:get(objects, M)),
    _ = Mod,
    {object, fname(Q, M) ++ "." ++ fname(N, M), N};
path({_, N}, _, M) -> {global, fname(N, M), N}.

copy(_Ctx, {member, P, _}, Which) -> [case Which of in -> "in->"; out -> "out->" end, P];
copy(Ctx, {_, P, _}, Which) -> [gbase(Ctx, Which), P].

read(Ref, Ctx, M) ->
    {_, _, N} = Path = path(Ref, Ctx, M),
    case decl(N, M) of
        {timer, _, _} -> ["(int32_t)", copy(Ctx, Path, in), ".val"];
        {local, _, _} -> ["(int32_t)", copy(Ctx, Path, in)];
        {_, _, _} -> ["(int32_t)", copy(Ctx, Path, in)]
    end.

read_out(Ref, Ctx, M) -> ["(int32_t)", copy(Ctx, path(Ref, Ctx, M), out)].

write(Ref, V, Ctx, M) ->
    {_, _, N} = Path = path(Ref, Ctx, M),
    case decl(N, M) of
        {timer, _, _} -> [copy(Ctx, Path, out), ".val = (uint8_t)(", V, " & 1);"];
        {local, _, _} -> [copy(Ctx, Path, in), " = ", copy(Ctx, Path, out), " = ", V, ";"];
        {_, _, _} -> [copy(Ctx, Path, out), " = ", V, ";"]
    end.

%% A store of a constant. To the State variable it is sticky: once FAILSAFE,
%% only FAILSAFE may be written.
sti(Ref, V, Ctx, M) ->
    {_, _, N} = Path = path(Ref, Ctx, M),
    case decl(N, M) of
        {variable, "State", _} when V =/= 2 ->
            ["if (", copy(Ctx, Path, in), " != FAILSAFE) ",
             write(Ref, integer_to_list(V), Ctx, M)];
        _ -> write(Ref, integer_to_list(V), Ctx, M)
    end.

%% ------------------------------------------------------------------
%% Calls
%% ------------------------------------------------------------------

arg(I) -> ["a", integer_to_list(I)].

%% The type of argument I, from the call's type word: four bits each, the
%% first argument lowest. V_STRING is 4, V_UNSIGNED 2.
argtype(Types, I) -> (Types bsr (4 * I)) band 15.

nargs(Types) -> length([I || I <- lists:seq(0, 7), argtype(Types, I) =/= 0]).

%% The declaration a name-taking call was handed (an LI before the ARG).
named(I, C) ->
    case maps:get({arg, I}, C, undefined) of
        undefined -> throw({unsupported, "a call whose argument is not a name"});
        D -> D
    end.

ref_of(D, M) ->
    case maps:find(D, maps:get(owner, M)) of
        {ok, _} -> {cur, v, D};
        error -> {v, D}
    end.

call(X, "changed", _, C, Ctx, M) ->
    Ref = ref_of(named(0, C), M),
    line([X, " = CSP_LIB_BOOL(", read(Ref, Ctx, M), " != ", read_out(Ref, Ctx, M), ");"]);
%% fn_rising and fn_falling as the runtime has them, 1 or 0 -- including which
%% way round, which reads backwards and is worth a look in csp_rt.c.
call(X, "rising", _, C, Ctx, M) ->
    Ref = ref_of(named(0, C), M),
    line([X, " = !", read_out(Ref, Ctx, M), " && ", read(Ref, Ctx, M), ";"]);
call(X, "falling", _, C, Ctx, M) ->
    Ref = ref_of(named(0, C), M),
    line([X, " = ", read_out(Ref, Ctx, M), " && !", read(Ref, Ctx, M), ";"]);
call(X, "elapsed", _, C, Ctx, M) ->
    T = named(0, C),
    B = [rd_base(Ctx, T, M), fname(T, M)],
    line([X, " = (int32_t)(", B, ".running ? csp_now - ", B, ".t0 : ", B, ".period);"]);
call(X, "min", _, _, _, _) -> line([X, " = csp_lib_min(a0, a1);"]);
call(X, "max", _, _, _, _) -> line([X, " = csp_lib_max(a0, a1);"]);
call(X, "abs", _, _, _, _) -> line([X, " = csp_lib_abs(a0);"]);
call(X, "sign", _, _, _, _) -> line([X, " = (a0 > 0) - (a0 < 0);"]);
call(X, "clip", _, _, _, _) -> line([X, " = csp_lib_clip(a0, a1, a2);"]);
call(X, "tick", _, _, _, _) -> line([X, " = (int32_t)csp_now;"]);
call(X, "cycle", _, _, _, _) -> line([X, " = (int32_t)csp_cycle;"]);
call(X, P, Types, C, _, M) when P =:= "print"; P =:= "println" ->
    [[case argtype(Types, I) of
          4 -> line(["csp_lib_puts(", io_lib:format("~p", [string(named(I, C), M)]), ");"]);
          2 -> line(["csp_lib_putu((uint32_t)", arg(I), ");"]);
          _ -> line(["csp_lib_puti(", arg(I), ");"])
      end || I <- lists:seq(0, nargs(Types) - 1)],
     [line("csp_lib_nl();") || P =:= "println"],
     line([X, " = 0;"])];
call(_, F, _, _, _, _) -> throw({unsupported, F ++ "()"}).

string(H, M) ->
    case maps:find(H, maps:get(strings, M)) of
        {ok, S} -> S;
        error -> throw({unsupported, io_lib:format("string handle ~p", [H])})
    end.

%% ------------------------------------------------------------------
%% The driver: utils/candyspeak_c.erl's, so the same harness runs both
%% ------------------------------------------------------------------

driver(M) ->
    ["const uint16_t csp_lib_nrules = ", integer_to_list(maps:size(maps:get(ruleno, M))), ";\n\n",
     "void csp_lib_setup(void)\n{\n",
     [[?IND, "csp_lib_disable(", integer_to_list(N), ");\n"] || N <- maps:get(disabled, M, [])],
     ?IND, "Main_init(&csp_in);\n",
     ?IND, "csp_out = csp_in;\n",
     "#if !defined(CSP_LIB_HOST)\n",
     ?IND, "Main_config(&csp_in);\n",
     "#endif\n",
     "}\n\n",
     "int csp_lib_step(uint32_t now, uint32_t* wait)\n{\n",
     ?IND, "int changed;\n",
     ?IND, "csp_now = now;\n",
     ?IND, "csp_cycle++;\n",
     ?IND, "Main_timers_in(&csp_in, &csp_out, now);\n",
     "#if !defined(CSP_LIB_HOST)\n",
     ?IND, "Main_input(&csp_out);\n",
     "#else\n",
     ?IND, "csp_lib_host_input();\n",
     "#endif\n",
     ?IND, "Main_run(&csp_in, &csp_out);\n",
     ?IND, "changed = memcmp(&csp_in, &csp_out, sizeof(csp_in)) != 0;\n",
     ?IND, "csp_in = csp_out;\n",
     "#if !defined(CSP_LIB_HOST)\n",
     ?IND, "Main_output(&csp_in);\n",
     "#endif\n",
     ?IND, "Main_timers_out(&csp_in, &csp_out, now);\n",
     ?IND, "*wait = 0xFFFFFFFFu;\n",
     ?IND, "Main_wait(&csp_in, now, wait);\n",
     ?IND, "return changed;\n",
     "}\n\n",
     names(M),
     "#if defined(CSP_LIB_MAIN)\n",
     "int main(void)\n{\n",
     ?IND, "uint32_t wait;\n",
     ?IND, "csp_chip_init();\n",
     ?IND, "csp_lib_setup();\n",
     ?IND, "for (;;)\n",
     ?IND, ?IND, "(void)csp_lib_step(csp_chip_millis(), &wait);\n",
     "}\n",
     "#endif\n"].

%% Every named scalar, by its runtime name, for the host harness and the
%% oracle. A nameless one -- d17 -- has no runtime name to be compared under.
names(M) ->
    Vars = ["State" | leaves(maps:get(main, M), "", M)],
    ["#if defined(CSP_LIB_HOST)\n",
     [["static int32_t get_", id(N), "(void) { return (int32_t)csp_in.", N, "; }\n",
       "static void set_", id(N), "(int32_t v) { csp_out.", N, " = v; }\n"] || N <- Vars],
     "const csp_lib_name_t csp_lib_names[] = {\n",
     [[?IND, "{ \"", N, "\", get_", id(N), ", set_", id(N), " },\n"] || N <- Vars],
     ?IND, "{ 0, 0, 0 }\n};\n",
     "#endif\n\n"].

leaves(Is, Pre, M) ->
    lists:flatmap(
      fun(I) ->
              case decl(I, M) of
                  {object, N, Opts} ->
                      Mod = proplists:get_value(module, Opts),
                      leaves(maps:get(Mod, maps:get(modules, M)), Pre ++ N ++ ".", M);
                  {_, "", _} -> [];
                  {timer, _, _} -> [];
                  {Kind, N, _} when Kind =:= variable; Kind =:= local; Kind =:= constant;
                                    Kind =:= digital; Kind =:= analog ->
                      case skip(I, M) orelse (N =:= "State" andalso Pre =:= "") of
                          true -> [];
                          false -> [Pre ++ N]
                      end;
                  _ -> []
              end
      end, [I || I <- Is, not skip(I, M)]).

id(S) -> [case Ch of $. -> $_; _ -> Ch end || Ch <- S].
