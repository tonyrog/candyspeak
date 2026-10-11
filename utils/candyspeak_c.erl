%%% @doc
%%%   CandySpeak to C, against csp_lib.h and csp_chip_io.h.
%%%
%%%   The program becomes one struct per module and a pair of them per cycle,
%%%   `in` (committed, what rules read) and `out` (what rules write) -- the
%%%   runtime's DIN and DOUT. A cycle is:
%%%
%%%     timers in -> inputs -> rules -> commit (in = out) -> outputs -> timers out
%%%
%%%   the same order as port/csp_avr.c's main loop, so a program means the same
%%%   thing translated as interpreted. Widths are C bit-fields: a store wraps to
%%%   the declared width the way the runtime's does.
%%%
%%%     candyspeak_c:file("prog.csp")            -> prog.c beside it
%%%     candyspeak_c:file("prog.csp", "out.c")
%%%
%%%   With -DCSP_LIB_MAIN the file carries main(): setup, then step forever on
%%%   csp_chip_millis -- Arduino's setup()/loop(), on csp_chip_*. Without it a
%%%   harness drives csp_lib_setup/csp_lib_step (tools/csp_lib_host.c).
%%%
%%%   NOT YET: arrays, float, transports other than `in can', events, `.pin`
%%%   set per instance. Each is refused by name rather than
%%%   translated wrong.
%%% @end
-module(candyspeak_c).

-export([file/1, file/2, file/3, main/1]).

-define(IND, "    ").

%% Flags first, then the files: `--graph In [Out]'.
main(Args) ->
    {Flags, Files} = lists:partition(fun("--" ++ _) -> true; (_) -> false end, Args),
    Opts = [opt(F) || F <- Flags],
    {In, Out} = case Files of
                    [I] -> {I, filename:rootname(I) ++ ".c"};
                    [I, O] -> {I, O};
                    _ -> usage()
                end,
    case file(In, Out, Opts) of
        ok -> halt(0);
        {error, E} -> io:format(standard_error, "~s: ~p\n", [In, E]), halt(1)
    end.

%% graph: every (rule, instance) a case of one switch, for a driver that runs
%% them in order or as a dependency graph wakes them.
opt("--graph") -> graph;
opt(F) -> io:format(standard_error, "unknown option ~s\n", [F]), usage().

usage() ->
    io:format(standard_error, "usage: ~s [--graph] in.csp [out.c]\n", [?MODULE]),
    halt(2).

%% What each option asks of the translation, kept for the walk.
options(Opts) ->
    put(csp_graph, lists:member(graph, Opts)),
    ok.

graph() -> get(csp_graph) =:= true.

file(In) ->
    file(In, filename:rootname(In) ++ ".c").

file(In, Out) ->
    file(In, Out, []).

file(In, Out, Opts) ->
    case candyspeak:build(In) of
        {ok, Main} ->
            try options(Opts), gen(Main, In) of
                Text -> file:write_file(Out, Text)
            catch
                throw:{unsupported, What, Ln} ->
                    io:format(standard_error, "~s:~w: not translated to C: ~s\n",
                              [In, Ln, What]),
                    {error, unsupported}
            end;
        Error ->
            Error
    end.

%% ------------------------------------------------------------------
%% The program
%% ------------------------------------------------------------------

gen({{module, _, {'WORD', _, "Main"}}, Bound, Decls0}, In) ->
    put(csp_scope, 0),
    Decls = scope(Decls0, #{}),
    Mods = modules(Decls, #{}),
    States = [S || {'WORD', _, S} <- maps:get(states, Bound, [])],
    Main = {"Main", Decls},
    Order = module_order(Main, Mods),
    %% #define and #constant at the top are emitted as C #defines, so they are
    %% in scope in every module -- an imported library's constants included.
    %% Main's other names are reachable from inside a module too, as the
    %% runtime looks a name up: the module first, then the global level. They
    %% are read from the program's own two copies, csp_in and csp_out -- which
    %% is why every struct comes before any function.
    MainSym = symbols(Decls, Mods),
    Consts = maps:filter(fun(_, {const, _}) -> true; (_, _) -> false end, MainSym),
    Globals = maps:merge(
                maps:map(fun(_, V) -> {global, V} end,
                         maps:filter(fun(_, {K, _}) -> lists:member(K, [var, digital, analog, local, timer]);
                                        (_, {object, _, _}) -> true;
                                        (_, _) -> false end, MainSym)),
                Consts),
    Mains = [{M, maps:get(M, Mods)} || M <- Order] ++ [{"Main", Decls}],
    put(csp_watched, watched(Mains)),
    put(csp_mods, Mods),
    put(csp_main_ds, Decls),
    put(csp_fields, sets:from_list([N || {_, Ds} <- Mains, D <- flat(Ds),
                                         N <- [name_of(D)], N =/= undefined])),
    put(csp_haspar, has_tree(Mains, fun is_param/1)),
    put(csp_kept, kept(Mains)),
    put(csp_hasloc, has_tree(Mains, fun is_lfield/1)),
    {_, RuleNo} = number_rules(Decls, {1, #{}}),
    put(csp_ruleno, RuleNo),
    put(csp_hascan, has_tree(Mains, fun({buffer, _, _, _, _, [{can, _}]}) -> true;
                                       (_) -> false end)),
    put(csp_hasbuf, has_tree(Mains, fun({buffer, _, _, _, _, _}) -> true;
                                       (_) -> false end)),
    %% In this order: with --graph a module leaves its steps for the driver.
    ModTexts = [module(M, maps:get(M, Mods), Mods, States, Globals) || M <- Order],
    MainText = module("Main", Decls, Mods, States, Consts),
    Driver = driver(Mods, Decls),
    [header(In, States),
     [defines(Ds) || {_, Ds} <- [Main | [maps:get(M, Mods) || M <- Order]]],
     [[struct(M, Ds), side(M, local, Ds), side(M, param, Ds)] || {M, Ds} <- Mains],
     "static Main_t csp_in, csp_out;\n",
     case has_loc("Main") of
         true -> "static Main_local_t csp_loc;\n";
         false -> []
     end,
     case has_par("Main") of
         true -> "static Main_param_t csp_par;\n";
         false -> []
     end,
     "\n",
     ModTexts, MainText, Driver].

%% ------------------------------------------------------------------
%% Where a #local lives
%% ------------------------------------------------------------------
%%
%% Three places, from cheapest:
%%
%% - A C variable in the run function (is_stack), for a formula nothing needs
%%   after the function returns. What most locals are, and they cost no RAM.
%%   Not with --graph, where a step runs again only when woken and must find
%%   the value it left.
%% - Name_local_t (is_lfield), ONE copy, for a local that must outlive the
%%   run: a `#local X in' the instance line binds from outside, an `out' the
%%   instance's parent reads, a formula that reads itself or a local not yet
%%   computed this cycle (last cycle's value), a local of the program a module
%%   reads (kept/1).
%% - The committed and working pair in Name_t, for a local changed() is asked
%%   of: that compares against last cycle's value, as the runtime gives a
%%   watched local a slot of its own.
%%
%% By name, across the program -- conservatively.
watched(Mains) ->
    All = [D || {_, Ds} <- Mains, D <- flat(Ds)],
    sets:from_list([N || {call, _, {'WORD', _, "changed"}, [Arg]} <- terms(All),
                         N <- case Arg of
                                  {field, _, {'WORD', _, X}} -> [X];
                                  {fld, _, _, {'WORD', _, X}} -> [X];
                                  _ -> []
                              end]).

is_lside({local, _, {'WORD', _, N}, _, _, _}) ->
    not sets:is_element(N, get(csp_watched));
is_lside(_) -> false.

is_stack({local, _, {'WORD', _, N}, _, Opts, E} = D) ->
    not graph() andalso E =/= undefined andalso is_formula(Opts) andalso
        proplists:get_value(dir, Opts) =/= out andalso is_lside(D) andalso
        not sets:is_element(N, get(csp_kept));
is_stack(_) -> false.

is_lfield(D) -> is_lside(D) andalso not is_stack(D).

%% The locals that must outlive their run function, by name: one read before
%% its formula has run this cycle -- by itself, or by something above it --
%% and one of the program's that a module reads.
kept(Mains) ->
    Early = lists:append([early(flat(Ds)) || {_, Ds} <- Mains]),
    MainLocals = [N || {"Main", Ds} <- Mains, {local, _, {'WORD', _, N}, _, _, _} <- flat(Ds)],
    InModules = lists:append([reads(D) || {M, Ds} <- Mains, M =/= "Main", D <- flat(Ds)]),
    sets:from_list(Early ++ [N || N <- MainLocals, lists:member(N, InModules)]).

early(Ds) ->
    Locals = [N || {local, _, {'WORD', _, N}, _, _, _} <- Ds],
    {Early, _} =
        lists:foldl(
          fun(D, {E, Seen}) ->
                  E1 = E ++ [X || X <- reads(D), lists:member(X, Locals),
                                  not lists:member(X, Seen)],
                  case D of
                      {local, _, {'WORD', _, N}, _, _, _} -> {E1, [N | Seen]};
                      _ -> {E1, Seen}
                  end
          end, {[], []}, Ds),
    Early.

reads(D) -> [X || {field, _, {'WORD', _, X}} <- terms(D)].

%% A C variable for a #local: its own name could be a macro or a keyword.
svar(N) -> ["l_", N].

%% The C variables, at the top of the run function. A local no rule reads is
%% still written, hence the (void).
stack_decls(Ds) ->
    [[[?IND, ctype(Res, Opts, Ln), " ", svar(N), " = 0;\n"],
      [?IND, "(void)", svar(N), ";\n"]]
     || {local, Ln, {'WORD', _, N}, Res, Opts, _} = D <- flat(Ds), is_stack(D)].

%% A store into one: the C variable holds 32 bits, so a narrow local is cut to
%% its width as a bit-field would cut it.
stack_store(N, Res, Opts, Ln, Rhs) ->
    case Res of
        default -> [svar(N), " = ", Rhs, ";\n"];
        {'INT', _, "32"} -> [svar(N), " = ", Rhs, ";\n"];
        {'INT', _, W} ->
            F = case ctype(Res, Opts, Ln) of
                    "uint32_t" -> "csp_lib_wrapu";
                    _ -> "csp_lib_wraps"
                end,
            [svar(N), " = ", F, "(", Rhs, ", ", W, ");\n"]
    end.

%% Declarations, with those inside #in and #when blocks.
flat(Ds) ->
    lists:flatmap(fun({{_, _, _}, Inner}) when is_list(Inner) -> flat(Inner);
                     (D) -> [D] end, Ds).

%% Every subterm, depth first.
terms(T) when is_tuple(T) -> [T | terms(tuple_to_list(T))];
terms([H | R]) -> terms(H) ++ terms(R);
terms(_) -> [].

%% A #constant or #define is a C macro, named as in the program -- unless a
%% field somewhere has the same name, which a module may (a member shadows a
%% global). The macro would then rewrite the field, so it is renamed instead.
macro(N) ->
    case sets:is_element(N, get(csp_fields)) of
        true -> "c_" ++ N;
        false -> N
    end.

%% Every module definition, by name -- the imported library ones included.
modules([{{module, _, {'WORD', _, Name}}, _B, Ds} | T], Acc) ->
    modules(T, modules(Ds, Acc#{Name => Ds}));
modules([_ | T], Acc) -> modules(T, Acc);
modules([], Acc) -> Acc.

%% Modules a module instantiates come before it: C wants the struct first.
module_order({_, Ds}, Mods) ->
    lists:reverse(order(used(Ds), Mods, [])).

order([M | T], Mods, Acc) ->
    case lists:member(M, Acc) of
        true -> order(T, Mods, Acc);
        false ->
            Ds = module_ds(M, Mods, 0),
            Acc1 = order(used(Ds), Mods, Acc),
            order(T, Mods, [M | Acc1])
    end;
order([], _Mods, Acc) -> Acc.

used(Ds) -> [M || {object, _, {'WORD', _, M}, _, _} <- Ds].

module_ds(M, Mods, Ln) ->
    case maps:find(M, Mods) of
        {ok, Ds} -> Ds;
        error -> throw({unsupported, "unknown module " ++ M, Ln})
    end.

header(In, States) ->
    ["// Generated by utils/candyspeak_c.erl. Do not edit: edit the .csp.\n",
     "//\n",
     "// Sources, the program first and then each import in the order it came in:\n",
     [["//   ", F, "\n"] || F <- sources(In)],
     "\n",
     "#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n#include \"csp_lib.h\"\n",
     "#if defined(CSP_LIB_HOST)\n#include \"csp_lib_host.h\"\n#endif\n\n",
     "// tick() and cycle(): the time of this cycle and its number, from 1.\n",
     "static uint32_t csp_now;\nstatic uint32_t csp_cycle;\n\n",
     case graph() of
         true ->
             ["// A step function, as its module's table holds it: each one is written\n",
              "// with its module's own types and cast to this.\n",
              "typedef uint32_t (*csp_rule_fn_t)(void* in, void* out, void* loc, void* par);\n",
              "typedef void (*csp_diff_fn_t)(void* a, void* b, uint32_t* m);\n\n"];
         false -> []
     end,
     "enum {\n",
     [[?IND, S, " = ", integer_to_list(N), ",\n"]
      || {S, N} <- lists:zip(States, lists:seq(0, length(States) - 1))],
     "};\n\n"].

%% Absolute paths, so a .c found later can be traced to exactly the files it
%% was made from. candyspeak:parse/2 with `sources` marks where each import
%% begins; an import seen twice is read once and listed once.
sources(In) ->
    Marks = case candyspeak:parse(In, [sources]) of
                {ok, Ast} -> [P || {source, 0, P} <- Ast];
                _ -> []
            end,
    uniq([abs_path(F) || F <- [In | Marks]], []).

uniq([F | T], Acc) ->
    case lists:member(F, Acc) of
        true -> uniq(T, Acc);
        false -> uniq(T, Acc ++ [F])
    end;
uniq([], Acc) -> Acc.

abs_path(F) ->
    norm(filename:split(filename:absname(F)), []).

norm([".." | T], [_ | Acc]) -> norm(T, Acc);
norm([".." | T], []) -> norm(T, []);
norm(["." | T], Acc) -> norm(T, Acc);
norm([P | T], Acc) -> norm(T, [P | Acc]);
norm([], Acc) -> filename:join(lists:reverse(Acc)).

defines(Ds) ->
    [[case D of
          {define, _, {'WORD', _, N}, E} ->
              ["#define ", macro(N), " (", cexpr(E, #{}), ")\n"];
          {constant, Ln, {'WORD', _, N}, scalar, _Res, _Opts, E} ->
              check_const(E, Ln),
              ["#define ", macro(N), " (", cexpr(E, #{}), ")\n"];
          {constant, Ln, _, _, _, _, _} ->
              throw({unsupported, "#constant array", Ln});
          _ -> []
      end || D <- Ds], "\n"].

check_const({'FLT', Ln, _}, _) -> throw({unsupported, "float", Ln});
check_const(_, _) -> ok.

%% ------------------------------------------------------------------
%% A module: its struct, init, rules, and the walks for timers and pins
%% ------------------------------------------------------------------

module(Name, Ds, Mods, States, Globals) ->
    Sym = maps:merge(Globals, symbols(Ds, Mods)),
    Env = #{sym => Sym, mods => Mods, states => States, main => Name =:= "Main",
            module => Name},
    T = [Name, "_t"],
    [init(Name, Ds, Env),
     can_fn(Name, Ds),
     rx_fn(Name, Ds),
     walk(Name, "timers_in", "uint32_t now", Ds, Env),
     walk(Name, "timers_out", "uint32_t now", Ds, Env),
     wait_fn(Name, Ds),
     "#if !defined(CSP_LIB_HOST)\n",
     io_fn(Name, "config", Ds, Env),
     io_fn(Name, "input", Ds, Env),
     io_fn(Name, "output", Ds, Env),
     "#endif\n\n",
     case graph() of
         false ->
             ["static void ", Name, "_run(", T, "* in, ", T, "* out", sargs(Name), ")\n{\n",
              stack_decls(Ds),
              timer_starts(Ds, Env),
              body(Ds, ?IND, Env, top),
              tail(),
              "}\n\n"];
         true ->
             steps(Name, Ds, Env)
     end].

%% INIT lasts one cycle unless a rule said otherwise; FAILSAFE is left only by
%% a reset.
tail() ->
    [?IND, "// INIT lasts one cycle unless a rule said otherwise; FAILSAFE\n",
     ?IND, "// is left only by a reset.\n",
     ?IND, "if ((in->State == INIT) && (out->State == INIT))\n",
     ?IND, ?IND, "out->State = NORMAL;\n",
     ?IND, "if (in->State == FAILSAFE)\n",
     ?IND, ?IND, "out->State = FAILSAFE;\n"].

%% ------------------------------------------------------------------
%% --graph: a function per step
%% ------------------------------------------------------------------
%%
%% Instead of one run function a module gets a function per step -- a rule,
%% a #local formula, a binding on an instance line, an instance's defaults,
%% its timer starts and its tail -- each with the gates of the #in and #when
%% blocks it stands in. What is left between them is the order: the module's
%% step table, where an entry is a function or a child instance. Walked from
%% the top it is the run functions (csp_run, with -DCSP_LIB_SEQ); walked
%% with a bit per (instance, step) it runs only what was woken (csp_react).
%%
%% A step function returns which of what it wrote changed, a bit each in the
%% order of step_writes/2; the driver turns a bit into the steps to wake.
%%
%% An instance inside a block would have its steps gated by its parent's
%% state, which its own functions cannot see; refused for now.
steps(Name, Ds, Env) ->
    Ts = case timer_starts(Ds, Env) of
             [] -> [];
             Code -> [{fn, Code}]
         end,
    All = Ts ++ gsteps(Ds, Env, top, []) ++ [{fn, tail()}],
    {Fns, Seq, Info, _} =
        lists:foldl(
          fun({fn, Code}, {F, S, I, J}) ->
                  Text = lists:flatten(Code),
                  W = step_writes(Name, Text),
                  {[step_fn(Name, J, Text, W) | F], [{fn, J} | S], [{Text, W} | I], J + 1};
             (Call, {F, S, I, J}) ->
                  {F, [Call | S], I, J}
          end, {[], [], [], 0}, All),
    Steps = lists:reverse(Seq),
    put({csp_steps, Name}, Steps),
    put({csp_stepinfo, Name}, list_to_tuple(lists:reverse(Info))),
    Leaves = diff_leaves(Name),
    J1 = length(Fns),
    [lists:reverse(Fns),
     "static const csp_rule_fn_t ", Name, "_fn[] = {\n",
     [[?IND, "(csp_rule_fn_t)", Name, "_r", integer_to_list(J), ",\n"]
      || J <- lists:seq(0, J1 - 1)],
     "};\n\n",
     "// Where each step function's bits start in ", Name, "_wmap.\n",
     "static const uint16_t ", Name, "_wofs[] = {\n",
     table(lists:reverse(lists:foldl(fun({_, W}, [H | _] = A) -> [H + length(W) | A] end,
                                     [0], lists:reverse(Info)))),
     "};\n\n",
     "// A bit: the field it is, {instance, slot}. The instance is this one plus\n",
     "// that many, or with the top bit set the one of that number; slot 0xFFFF\n",
     "// is the step itself, next cycle.\n",
     "static const uint16_t ", Name, "_wmap[][2] = {\n",
     case [wmap(Name, X) || {_, W} <- lists:reverse(Info), X <- W] of
         [] -> [?IND, "{ 0, 0xFFFF }\n"];
         Ws -> [[?IND, "{ ", A, ", ", B, " },\n"] || {A, B} <- Ws]
     end,
     "};\n\n",
     "static const uint16_t ", Name, "_steps[] = {\n",
     [[?IND, case St of
                 {fn, J} -> integer_to_list(J);
                 {call, M, N} ->
                     ["0x8000 | ", integer_to_list(child_offset(Name, N)),
                      ",    // ", N, " ", M]
             end, case St of {fn, _} -> ",\n"; _ -> "\n" end]
      || St <- Steps],
     "};\n\n",
     "// Which of the instance's own fields differ between two copies, a bit\n",
     "// each in m[], in declaration order.\n",
     "static void ", Name, "_diff(", Name, "_t* a, ", Name, "_t* b, uint32_t* m)\n{\n",
     [[?IND, "m[", integer_to_list(B div 32), "] ", case B rem 32 of 0 -> "="; _ -> "|=" end,
       " (uint32_t)", differs(["a->", F], ["b->", F], K), " << ", integer_to_list(B rem 32), ";\n"]
      || {B, {F, K}} <- lists:zip(lists:seq(0, length(Leaves) - 1), Leaves)],
     "}\n\n"].

%% Each step function takes the instance's two copies and its two sides, in
%% the module's own types; the table holds it as void pointers, which every
%% ABI here passes alike. A side the module does not have is a void pointer.
%% A #local it writes is kept as it was on the way in, to tell whether it
%% changed; a field in the copies is compared against the committed one.
step_fn(Name, J, Code, W) ->
    T = [Name, "_t"],
    WI = lists:zip(lists:seq(0, length(W) - 1), W),
    ["static uint32_t ", Name, "_r", integer_to_list(J), "(", T, "* in, ", T, "* out, ",
     case has_loc(Name) of true -> [Name, "_local_t* loc, "]; false -> "void* loc, " end,
     case has_par(Name) of true -> [Name, "_param_t* par"]; false -> "void* par" end,
     ")\n{\n",
     [[?IND, "int32_t w", integer_to_list(B), " = (int32_t)", wpath(X), ";\n"]
      || {B, {loc, _, _, _} = X} <- WI],
     ?IND, "(void)in; (void)out; (void)loc; (void)par;\n",
     Code,
     ?IND, "return 0",
     [[" |\n", ?IND, ?IND, "((uint32_t)", wtest(B, X), " << ", integer_to_list(B), ")"]
      || {B, X} <- WI],
     ";\n",
     "}\n\n"].

%% A written field, as {state | loc, Abs, Leaf, Kind}: Abs when it is a
%% global (csp_out./csp_loc.), Leaf its path down to the field itself, Kind
%% scalar or blob (a timer, a buffer). `always' is a step that reads the clock
%% and so has to run every cycle.
wpath({loc, false, L, _}) -> ["loc->", dots(L)];
wpath({loc, true, L, _}) -> ["csp_loc.", dots(L)].

wtest(_, always) -> "1";
wtest(B, {loc, _, _, _} = X) -> ["((int32_t)", wpath(X), " != w", integer_to_list(B), ")"];
wtest(_, {state, false, L, K}) -> differs(["out->", dots(L)], ["in->", dots(L)], K);
wtest(_, {state, true, L, K}) -> differs(["csp_out.", dots(L)], ["csp_in.", dots(L)], K).

differs(A, B, blob) -> ["(memcmp(&", A, ", &", B, ", sizeof(", A, ")) != 0)"];
differs(A, B, scalar) -> ["(", A, " != ", B, ")"].

dots(L) -> lists:join(".", L).

%% What a step's C touches, read off the text: {in | out | loc | par, Abs,
%% Path, Assigned}. Abs for csp_in. and the like, a global from a module.
refs(Text) ->
    P = "(?<![A-Za-z0-9_])(in|out|loc|par)->([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*)(\\s*=(?!=))?",
    G = "(?<![A-Za-z0-9_])csp_(in|out|loc|par)\\.([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*)(\\s*=(?!=))?",
    [{list_to_atom(S), Abs, string:split(Path, ".", all), Eq =/= []}
     || {Re, Abs} <- [{P, false}, {G, true}],
        [S, Path | Eq0] <- case re:run(Text, Re, [global, {capture, all_but_first, list}]) of
                               {match, Ms} -> Ms;
                               nomatch -> []
                           end,
        Eq <- [case Eq0 of [E] -> E; [] -> [] end]].

%% Where a path in module Mod ends up: {ok, Leaf, Kind} for a field of that
%% side (state: the two copies; loc: the local side), down through instances.
%% A #param or #constant changes nothing a rule can see happen.
leaf_of(_Mod, Side, ["State" | _]) when Side =:= state -> {ok, ["State"], scalar};
leaf_of(Mod, Side, [C | Rest]) ->
    case [D || D <- mod_ds(Mod), dname(D) =:= C] of
        [{object, _, {'WORD', _, M}, _, _}] when Rest =/= [] ->
            case leaf_of(M, Side, Rest) of
                {ok, L, K} -> {ok, [C | L], K};
                E -> E
            end;
        [{timer, _, _, _, _}] when Side =:= state -> {ok, [C], blob};
        [{buffer, _, _, _, _, _}] when Side =:= state -> {ok, [C], blob};
        [{local, _, _, _, _, _} = D] ->
            case {is_lside(D), Side} of
                {true, loc} -> {ok, [C], scalar};
                {false, state} -> {ok, [C], scalar};
                _ -> error
            end;
        [D] when Side =:= state ->
            case element(1, D) of
                K when K =:= variable; K =:= digital; K =:= analog -> {ok, [C], scalar};
                _ -> error
            end;
        _ -> error
    end;
leaf_of(_, _, _) -> error.

dname({object, _, _, {'WORD', _, N}, _}) -> N;
dname({timer, _, {'WORD', _, N}, _, _}) -> N;
dname({buffer, _, {'WORD', _, N}, _, _, _}) -> N;
dname(D) -> name_of(D).

%% The step's bits: each field it may have changed, in the order it first
%% names them, and `always' last when it reads the clock.
step_writes(Name, Text) ->
    W = [X || {Side, Abs, Path, Eq} <- refs(Text),
              Side =:= out orelse (Side =:= loc andalso Eq),
              X <- case leaf_of(case Abs of true -> "Main"; false -> Name end,
                                case Side of out -> state; loc -> loc end, Path) of
                       {ok, L, K} -> [{case Side of out -> state; loc -> loc end, Abs, L, K}];
                       error -> []
                   end],
    Always = case re:run(Text, "\\bcsp_(now|cycle)\\b") of
                 nomatch -> [];
                 _ -> [always]
             end,
    case uniq(W, []) ++ Always of
        L when length(L) > 32 -> throw({unsupported, "a step that writes more than 32 fields", 0});
        L -> L
    end.

%% Where a step's bit lands: {instance, slot} as Name_wmap has it.
wmap(_, always) -> {"0", "0xFFFF"};
wmap(Name, {Side, Abs, L, _}) ->
    {I, M} = case Abs of
                 true -> {16#8000, "Main"};
                 false -> {0, Name}
             end,
    {D, TM, F} = descend(M, L, 0),
    Slot = case Side of
               state -> length(diff_leaves(TM)) + index_of(F, [X || {X, _} <- diff_leaves(TM)]);
               loc -> 2 * length(diff_leaves(TM)) + index_of(F, loc_leaves(TM))
           end,
    {case I of 0 -> integer_to_list(D); _ -> ["0x8000 | ", integer_to_list(D)] end,
     integer_to_list(Slot)}.

%% Down a path's instances: {how many instances on, the module, the field}.
descend(M, [F], D) -> {D, M, F};
descend(M, [C | Rest], D) ->
    [Sub] = [X || {object, _, {'WORD', _, X}, {'WORD', _, N}, _} <- mod_ds(M), N =:= C],
    descend(Sub, Rest, D + child_offset(M, C)).

index_of(X, L) -> index_of(X, L, 0).
index_of(X, [X | _], I) -> I;
index_of(X, [_ | T], I) -> index_of(X, T, I + 1).

%% A module's #locals on the local side, in order.
loc_leaves(Name) -> [dname(D) || D <- flat(mod_ds(Name)), is_lside(D)].

%% A module's own fields in its two copies: what M_diff compares.
diff_leaves(Name) ->
    [{"State", scalar} |
     [{dname(D), case element(1, D) of
                       timer -> blob;
                       buffer -> blob;
                       _ -> scalar
                   end}
      || D <- flat(mod_ds(Name)), not is_lside(D), not is_param(D),
         element(1, D) =/= object, field(D) =/= []]].

%% Instances are numbered depth first, so the instances of a module's subtree
%% follow it in the same order everywhere: child N is a fixed distance on.
child_offset(Mod, N) ->
    Kids = [{X, M} || {object, _, {'WORD', _, M}, {'WORD', _, X}, _} <- mod_ds(Mod)],
    {Before, _} = lists:splitwith(fun({X, _}) -> X =/= N end, Kids),
    1 + lists:sum([subtree(M) || {_, M} <- Before]).

subtree(Mod) ->
    1 + lists:sum([subtree(M) || {object, _, {'WORD', _, M}, _, _} <- mod_ds(Mod)]).

mod_ds("Main") -> get(csp_main_ds);
mod_ds(M) -> maps:get(M, get(csp_mods)).

gsteps(Ds, Env, Where, Gates) ->
    lists:flatmap(fun(D) -> gstep(D, Env, Where, Gates) end, Ds).

gstep({{in, _, States}, Ds}, Env, _Where, Gates) ->
    C = lists:join(" || ", [["in->State == ", S] || {'WORD', _, S} <- States]),
    gsteps(Ds, Env, block, Gates ++ [C]);
gstep({{'when', _, Cond}, Ds}, Env, Where, Gates) ->
    gsteps(Ds, Env, Where, Gates ++ [unwrap(ccond(Cond, Env))]);
gstep({object, Ln, {'WORD', _, M}, {'WORD', _, N}, Args}, Env, _Where, Gates) ->
    [throw({unsupported, "an instance inside #in/#when with --graph", Ln})
     || Gates =/= []],
    {Binds, Defaults, _Call} = object_parts(M, N, Args, Ln, ?IND, Env),
    [{fn, B} || B <- Binds] ++
        [{fn, Defaults} || Defaults =/= []] ++
        [{call, M, N}];
gstep(D, Env, Where, Gates) ->
    Ind = lists:append(lists:duplicate(length(Gates) + 1, ?IND)),
    case lists:flatten(stmt(D, Ind, Env, Where)) of
        [] -> [];
        Code -> [{fn, gate(Gates, ?IND, Code)}]
    end.

%% Code inside one `if' per gate, outermost first.
gate([], _Ind, Code) -> Code;
gate([G | Gs], Ind, Code) ->
    [Ind, "if (", G, ") {\n", gate(Gs, Ind ++ ?IND, Code), Ind, "}\n"].


struct(Name, Ds) ->
    ["typedef struct {\n",
     ?IND, "int32_t State;\n",
     [field(D) || D <- flat(Ds), not is_lside(D), not is_param(D)],
     "} ", Name, "_t;\n\n"].

%% ------------------------------------------------------------------
%% Block scope
%% ------------------------------------------------------------------
%%
%% A #local formula declared in an #in or #when block is the block's own: #end
%% ends the name, as it does in the runtime (blk_pop rolls the names back). So
%% each one is renamed apart, N to N_<k>, before anything else looks: two
%% blocks may both have an N, and a read after the block finds no N at all and
%% is refused as an unknown name.
scope(Ds, Ren) -> [scope_d(D, Ren) || D <- Ds].

scope_d({{module, _, _} = H, B, Ds}, _Ren) -> {H, B, scope(Ds, #{})};
scope_d({{K, Ln, X}, Ds}, Ren) when K =:= in; K =:= 'when' ->
    Ren1 = lists:foldl(
             fun(N, R) ->
                     K1 = get(csp_scope) + 1,
                     put(csp_scope, K1),
                     R#{N => N ++ "_" ++ integer_to_list(K1)}
             end, Ren,
             [N || {local, _, {'WORD', _, N}, _, Opts, E} <- Ds,
                   E =/= undefined, is_formula(Opts),
                   proplists:get_value(dir, Opts) =/= out]),
    {{K, Ln, subst(X, Ren)}, scope(Ds, Ren1)};
scope_d(D, Ren) -> subst(D, Ren).

%% A name in an expression, and a #local's own: an instance line's member
%% names, and `obj.N', are names in another scope and stay as they are.
subst(T, Ren) when map_size(Ren) =:= 0 -> T;
subst({field, Ln, {'WORD', L, N}}, Ren) ->
    {field, Ln, {'WORD', L, maps:get(N, Ren, N)}};
subst({local, Ln, {'WORD', L, N}, Res, Opts, E}, Ren) ->
    {local, Ln, {'WORD', L, maps:get(N, Ren, N)}, Res, Opts, subst(E, Ren)};
subst({object, Ln, M, N, Args}, Ren) ->
    {object, Ln, M, N, [{Op, L, F, subst(E, Ren)} || {Op, L, F, E} <- Args]};
subst(T, Ren) when is_tuple(T) -> list_to_tuple(subst(tuple_to_list(T), Ren));
subst(L, Ren) when is_list(L) -> [subst(X, Ren) || X <- L];
subst(X, _) -> X.

%% ------------------------------------------------------------------
%% Rule numbers, for #disable
%% ------------------------------------------------------------------
%%
%% Rule N is the Nth rule the compiler lays down, counted through the whole
%% program in source order: a module's body where its #module stands, an #in or
%% #when block's rules where the block stands. A rule counts, and so does an
%% instance line's assignment to a member #variable (`V <- A`, `V = 3`). A
%% #local's formula and an instance's binding of a #local do not: they say
%% what the local is and cannot be disabled (csp_is_rule). Nor does a `>'
%% patch. That is what /list numbers -- check it there.
%%
%% Each guard in the C carries its number as CSP_ON(n): a bit test against the
%% disable mask when the build says -DCSP_LIB_RULES, the constant 1 (and no
%% code at all) when it does not.
number_rules([D | T], Acc) -> number_rules(T, number_rule(D, Acc));
number_rules([], Acc) -> Acc.

number_rule({rule, _, _, _} = R, {N, M}) -> {N + 1, M#{R => N}};
number_rule({object, _, {'WORD', _, Mod}, _, Args}, Acc) ->
    Locals = [F || {local, _, {'WORD', _, F}, _, _, _} <- flat(mod_ds(Mod))],
    lists:foldl(fun({_, _, {field, _, {'WORD', _, F}}, _} = A, {N, M}) ->
                        case lists:member(F, Locals) of
                            true -> {N, M};
                            false -> {N + 1, M#{A => N}}
                        end;
                   (A, {N, M}) -> {N + 1, M#{A => N}}
                end, Acc, Args);
number_rule({{module, _, _}, _, Ds}, Acc) -> number_rules(Ds, Acc);
number_rule({{in, _, _}, Ds}, Acc) -> number_rules(Ds, Acc);
number_rule({{'when', _, _}, Ds}, Acc) -> number_rules(Ds, Acc);
number_rule(_, Acc) -> Acc.

rule_on(Term) -> ["CSP_ON(", integer_to_list(maps:get(Term, get(csp_ruleno))), ")"].

nrules() -> integer_to_list(maps:size(get(csp_ruleno))).

%% `#disable 3 5-7' in the program: the rules start off. A range that runs
%% past the last rule stops there.
disables(Decls) ->
    N = maps:size(get(csp_ruleno)),
    [[?IND, case Kind of disable -> "csp_lib_disable("; enable -> "csp_lib_enable(" end,
      integer_to_list(I), ");\n"]
     || {Kind, _, Items} <- Decls, Kind =:= disable orelse Kind =:= enable,
        Item <- Items,
        I <- case Item of
                 {{'INT', _, A}, {'INT', _, B}} ->
                     lists:seq(list_to_integer(A), min(list_to_integer(B), N));
                 {'INT', _, A} -> [list_to_integer(A)]
             end].

%% ------------------------------------------------------------------
%% Buffers and fields
%% ------------------------------------------------------------------
%%
%% A #buffer is its bytes, its length, and the arrival flags: a frame taken in
%% lands in the WORKING copy with `rxpend', and the commit makes it `rx' --
%% readable, data and flag together, for exactly one cycle. The runtime's
%% BUF_F_RXPEND and BUF_F_RX, in csp_commit. A #field is a view of bits in its
%% buffer, read and written by csp_bits.h, the same code the runtime uses, so
%% the bit order -- `big' or not -- cannot differ.
%%
%% Only `in can <id>' and a plain buffer with no transport so far.
buffer_check(Ln, Opts, Tr) ->
    case {proplists:get_value(dir, Opts), Tr} of
        {_, []} -> ok;
        {in, [{can, _}]} -> ok;
        _ -> throw({unsupported, "#buffer with this transport", Ln})
    end.

%% A frame from the bus, offered to every `in can' buffer that wants its id.
can_fn(Name, Ds) ->
    case has_can(Name) of
        false -> [];
        true ->
            ["static void ", Name, "_can(", Name, "_t* s, uint32_t id, "
             "const uint8_t* d, uint8_t n)\n{\n",
             [[?IND, "if (id == ", Id, ") {\n",
               ?IND, ?IND, "memcpy(s->", N, ".b, d, (n < sizeof(s->", N, ".b)) ? n : sizeof(s->", N, ".b));\n",
               ?IND, ?IND, "s->", N, ".dlc = n;\n",
               ?IND, ?IND, "s->", N, ".rxpend = 1;\n",
               ?IND, "}\n"]
              || {buffer, _, {'WORD', _, N}, _, _, [{can, {'INT', _, Id}}]} <- Ds],
             [[?IND, M, "_can(&s->", N, ", id, d, n);\n"]
              || {object, _, {'WORD', _, M}, {'WORD', _, N}, _} <- Ds, has_can(M)],
             "}\n\n"]
    end.

%% After the commit: what arrived this cycle is readable for the next, and
%% what was readable is no longer.
rx_fn(Name, Ds) ->
    case has_buf(Name) of
        false -> [];
        true ->
            ["static void ", Name, "_rx(", Name, "_t* in, ", Name, "_t* out)\n{\n",
             [[?IND, "in->", N, ".rx = out->", N, ".rx = in->", N, ".rxpend;\n",
               ?IND, "in->", N, ".rxpend = out->", N, ".rxpend = 0;\n"]
              || {buffer, _, {'WORD', _, N}, _, _, _} <- Ds],
             [[?IND, M, "_rx(&in->", N, ", &out->", N, ");\n"]
              || {object, _, {'WORD', _, M}, {'WORD', _, N}, _} <- Ds, has_buf(M)],
             "}\n\n"]
    end.

%% Where a field's bits are: {Buffer, Pos, N, BigEndian, Signed}.
bits_of({field, _, _, Res, Opts, {'WORD', _, Buf}, Range}) ->
    {Pos, N} = case Range of
                   {'INT', _, P} -> {list_to_integer(P),
                                     case Res of
                                         {'INT', _, W} -> list_to_integer(W);
                                         default -> 1
                                     end};
                   {range, _, {'INT', _, Lo}, {'INT', _, Hi}} ->
                       {list_to_integer(Lo), list_to_integer(Hi) - list_to_integer(Lo) + 1}
               end,
    Big = lists:member({endian, big}, Opts),
    Sgn = ctype(Res, Opts, 0) =:= "int32_t",
    {Buf, Pos, N, Big, Sgn}.

%% A buffer of up to four bytes is also a number: its bytes, low first,
%% unsigned -- as the runtime reads a #buffer used as a value.
scalar_bits({buffer, _, _, {'INT', _, Bytes}, _, _}, Ln) ->
    case list_to_integer(Bytes) of
        B when B =< 4 -> integer_to_list(8 * B);
        _ -> throw({unsupported, "a buffer wider than 32 bits as a value", Ln})
    end.

b01(true) -> "1";
b01(false) -> "0".

%% ------------------------------------------------------------------
%% The #params, ONE copy
%% ------------------------------------------------------------------
%%
%% A #param is not written by the rules -- it is set from outside, `>' at a
%% prompt or a stored setting -- so it has no transaction to take part in, and
%% the two copies a variable needs would only hold the same value twice. Each
%% module with params (its own, or in an instance it holds) gets a second
%% struct, M_param_t, with the params of every instance in the same tree
%% shape, and the program has one of them: csp_par. A run function gets `par',
%% its own node of that tree. The #locals are a side of their own the same way:
%% M_local_t, csp_loc, `loc'. They differ in what writes them -- a param is set
%% from outside and kept, a local is a rule's result -- so they are two sides,
%% and a param side could go to flash where nothing sets it at run time.

has_par(M) -> maps:get(M, get(csp_haspar), false).
has_loc(M) -> maps:get(M, get(csp_hasloc), false).

%% Module -> whether it, or an instance it holds, has a declaration Pred is
%% true of. Mains is in dependency order.
has_tree(Mains, Pred) ->
    lists:foldl(
      fun({M, Ds}, Acc) ->
              Own = lists:any(Pred, flat(Ds)),
              Sub = lists:any(fun({object, _, {'WORD', _, X}, _, _}) -> maps:get(X, Acc, false);
                                 (_) -> false end, flat(Ds)),
              Acc#{M => Own orelse Sub}
      end, #{}, Mains).

has_can(M) -> maps:get(M, get(csp_hascan), false).
has_buf(M) -> maps:get(M, get(csp_hasbuf), false).

side(Name, Side, Ds) ->
    {Has, In, T} = case Side of
                       local -> {fun has_loc/1, fun is_lfield/1, "_local_t"};
                       param -> {fun has_par/1, fun is_param/1, "_param_t"}
                   end,
    case Has(Name) of
        false -> [];
        true ->
            ["typedef struct {\n",
             [field(D) || D <- flat(Ds), In(D)],
             [[?IND, M, T, " ", N, ";\n"]
              || {object, _, {'WORD', _, M}, {'WORD', _, N}, _} <- Ds, Has(M)],
             "} ", Name, T, ";\n\n"]
    end.

%% `, M_local_t* loc, M_param_t* par' after a module's own arguments, each
%% when it has that side.
sargs(M) ->
    [case has_loc(M) of true -> [", ", M, "_local_t* loc"]; false -> [] end,
     case has_par(M) of true -> [", ", M, "_param_t* par"]; false -> [] end].

%% The same for an instance M named N, from where `Env' stands.
sptrs(M, N, Env) ->
    [case has_loc(M) of true -> [", &", loc(Env), N]; false -> [] end,
     case has_par(M) of true -> [", &", par(Env), N]; false -> [] end].

par(Env) -> maps:get(par, Env, "par->").
loc(Env) -> maps:get(loc, Env, "loc->").

is_param({param, _, _, _, _, _}) -> true;
is_param(_) -> false.

%% name -> {Kind, Decl}
symbols(Ds, Mods) ->
    lists:foldl(
      fun(D, Acc) ->
              case D of
                  {variable, _, {'WORD', _, N}, _, _, _, _} -> Acc#{N => {var, D}};
                  {param, _, {'WORD', _, N}, _, _, _} -> Acc#{N => {var, D}};
                  {digital, _, {'WORD', _, N}, _, _, _, _} -> Acc#{N => {digital, D}};
                  {analog, _, {'WORD', _, N}, _, _, _, _} -> Acc#{N => {analog, D}};
                  {timer, _, {'WORD', _, N}, _, _} -> Acc#{N => {timer, D}};
                  {local, _, {'WORD', _, N}, _, _, _} -> Acc#{N => {local, D}};
                  {constant, _, {'WORD', _, N}, _, _, _, _} -> Acc#{N => {const, D}};
                  {define, _, {'WORD', _, N}, _} -> Acc#{N => {const, D}};
                  {buffer, _, {'WORD', _, N}, _, _, _} -> Acc#{N => {buffer, D}};
                  {field, _, {'WORD', _, N}, _, _, _, _} -> Acc#{N => {bfield, D}};
                  {object, Ln, {'WORD', _, M}, {'WORD', _, N}, _} ->
                      Acc#{N => {object, M, symbols(module_ds(M, Mods, Ln), Mods)}};
                  _ -> Acc
              end
      end, #{}, flat(Ds)).

field({variable, Ln, {'WORD', _, N}, Arr, Res, Opts, _}) ->
    scalar(Arr, Ln), [?IND, ctype(Res, Opts, Ln), " ", N, width(Res), ";\n"];
field({param, Ln, {'WORD', _, N}, Res, Opts, _}) ->
    [?IND, ctype(Res, Opts, Ln), " ", N, width(Res), ";\n"];
field({local, Ln, {'WORD', _, N}, Res, Opts, _}) ->
    [?IND, ctype(Res, Opts, Ln), " ", N, width(Res), ";\n"];
field({digital, Ln, {'WORD', _, N}, Arr, _, _, _}) ->
    scalar(Arr, Ln), [?IND, "uint32_t ", N, ":1;\n"];
field({analog, Ln, {'WORD', _, N}, Arr, Res0, Opts, _}) ->
    Res = analog_res(Res0),
    scalar(Arr, Ln), [?IND, ctype(Res, analog_opts(Opts), Ln), " ", N, width(Res), ";\n"];
field({timer, _, {'WORD', _, N}, _, _}) ->
    [?IND, "csp_timer_t ", N, ";\n"];
field({object, _, {'WORD', _, M}, {'WORD', _, N}, _}) ->
    [?IND, M, "_t ", N, ";\n"];
field({buffer, Ln, {'WORD', _, N}, {'INT', _, Bytes}, Opts, Tr}) ->
    buffer_check(Ln, Opts, Tr),
    [?IND, "struct { uint8_t b[", Bytes, "]; uint8_t dlc, rx, rxpend; } ", N, ";\n"];
field({field, _, _, _, _, _, _}) -> [];      % a view into its buffer
field(_) -> [].

scalar(scalar, _) -> ok;
scalar(_, Ln) -> throw({unsupported, "array", Ln}).

%% An #analog is unsigned unless it says `integer`: a converter delivers
%% counts. The runtime's csp_parse_analog has the same default.
analog_res(default) -> {'INT', 0, "10"};      % csp_parse_analog's default
analog_res(Res) -> Res.

analog_opts(Opts) ->
    case proplists:is_defined(type, Opts) of
        true -> Opts;
        false -> [{type, unsigned} | Opts]
    end.

%% A typeless declaration is signed -- except `:1`, which holds 0 and 1 rather
%% than 0 and -1, or every `Flag == 1` would be false.
ctype(Res, Opts, Ln) ->
    Dflt = case Res of {'INT', _, "1"} -> unsigned; _ -> integer end,
    case proplists:get_value(type, Opts, Dflt) of
        integer -> "int32_t";
        unsigned -> "uint32_t";
        float -> throw({unsupported, "float", Ln});
        string -> throw({unsupported, "string", Ln})
    end.

width(default) -> "";
width({'INT', _, "32"}) -> "";
width({'INT', _, W}) -> [":", W].

%% The declared values. A plain name in an initialiser is read from the struct
%% being built, which is what makes `#variable Pt = SD` follow a parameter.
init(Name, Ds, Env) ->
    ["static void ", Name, "_init(", Name, "_t* s", sargs(Name), ")\n{\n",
     ?IND, "memset(s, 0, sizeof(*s));\n",
     case has_loc(Name) of true -> [?IND, "memset(loc, 0, sizeof(*loc));\n"]; false -> [] end,
     [init1(D, Env) || D <- Ds],
     "}\n\n"].

init1({variable, _, {'WORD', _, N}, _, _, _, E}, Env) when E =/= undefined ->
    [?IND, "s->", N, " = ", iexpr(E, Env), ";\n"];
init1({param, _, {'WORD', _, N}, _, _, E}, Env) when E =/= undefined ->
    [?IND, "par->", N, " = ", iexpr(E, Env), ";\n"];
init1({local, _, {'WORD', _, N}, _, Opts, E} = D, Env) when E =/= undefined ->
    case is_formula(Opts) of
        true -> [];
        false -> [?IND, case is_lside(D) of true -> "loc->"; false -> "s->" end,
                  N, " = ", iexpr(E, Env), ";\n"]
    end;
init1({timer, _, {'WORD', _, N}, P, E}, Env) ->
    [?IND, "s->", N, ".period = ", iexpr(P, Env), ";\n",
     case E of
         undefined -> [];
         _ -> [?IND, "s->", N, ".val = ", timer_val(iexpr(E, Env)), ";\n"]
     end];
init1({object, _, {'WORD', _, M}, {'WORD', _, N}, _}, _Env) ->
    [?IND, M, "_init(&s->", N, sptrs(M, N, #{}), ");\n"];
init1(_, _) -> [].

iexpr(E, Env) -> cexpr(E, Env#{rd => "s->", lrd => "s->"}).

%% `#timer T p = 1` is ALSO stored in the module's INIT, in its own body --
%% after anything the instantiating line wrote into T, so the declaration has
%% the last word, as it has in the runtime.
timer_starts(Ds, Env) ->
    [[?IND, "if (in->State == INIT) ",
      store({timer_val, ["out->", N]}, cexpr(E, Env))]
     || {timer, _, {'WORD', _, N}, _, E} <- Ds, E =/= undefined].

%% Timers in every instance, depth first.
walk(Name, What, Arg, Ds, _Env) ->
    T = [Name, "_t"],
    ["static void ", Name, "_", What, "(", T, "* in, ", T, "* out, ", Arg, ")\n{\n",
     [case D of
          {timer, _, {'WORD', _, N}, _, _} ->
              [?IND, lib_timer(What), "(&in->", N, ", &out->", N, ", now);\n"];
          {object, _, {'WORD', _, M}, {'WORD', _, N}, _} ->
              [?IND, M, "_", What, "(&in->", N, ", &out->", N, ", now);\n"];
          _ -> []
      end || D <- Ds],
     ?IND, "(void)in; (void)out; (void)now;\n",
     "}\n\n"].

lib_timer("timers_in") -> "csp_lib_timer_in";
lib_timer("timers_out") -> "csp_lib_timer_out".

%% How long until the next timer runs out: the virtual clock jumps by this.
wait_fn(Name, Ds) ->
    ["static void ", Name, "_wait(", Name, "_t* s, uint32_t now, uint32_t* w)\n{\n",
     [case D of
          {timer, _, {'WORD', _, N}, _, _} ->
              [?IND, "if (s->", N, ".running) {\n",
               ?IND, ?IND, "uint32_t dt = now - s->", N, ".t0;\n",
               ?IND, ?IND, "uint32_t r = (dt >= s->", N, ".period) ? 0 : s->",
               N, ".period - dt;\n",
               ?IND, ?IND, "if (r < *w) *w = r;\n",
               ?IND, "}\n"];
          {object, _, {'WORD', _, M}, {'WORD', _, N}, _} ->
              [?IND, M, "_wait(&s->", N, ", now, w);\n"];
          _ -> []
      end || D <- Ds],
     ?IND, "(void)s; (void)now; (void)w;\n",
     "}\n\n"].

%% Pins. config once, input into `out` before the rules, output from the
%% committed copy after them. The host harness calls none of these: on the
%% host, as in port/csp_linux.c, a pin is what the stimulus wrote.
io_fn(Name, What, Ds, _Env) ->
    T = [Name, "_t"],
    ["static void ", Name, "_", What, "(", T, "* s)\n{\n",
     [io1(What, D) || D <- Ds],
     ?IND, "(void)s;\n",
     "}\n\n"].

io1(What, {digital, Ln, {'WORD', _, N}, _, _, Opts, Pins}) ->
    {Port, Pin} = port_pin(Pins, Ln),
    Dir = proplists:get_value(dir, Opts, in),
    Pull = case proplists:get_value(pull, Opts) of
               pullup -> "CSP_CHIP_PULLUP";
               pulldown -> "CSP_CHIP_PULLDOWN";
               _ -> "0"
           end,
    case {What, Dir} of
        {_, inout} -> throw({unsupported, "#digital inout", Ln});
        {"config", _} ->
            [?IND, "csp_chip_dcfg(", Port, ", ", Pin, ", ", cdir(Dir), ", ",
             Pull, ");\n"];
        {"input", in} ->
            [?IND, "s->", N, " = csp_chip_din(", Port, ", ", Pin, ");\n"];
        {"output", out} ->
            [?IND, "csp_chip_dout(", Port, ", ", Pin, ", s->", N, ");\n"];
        _ -> []
    end;
io1(What, {analog, Ln, {'WORD', _, N}, _, Res, Opts, Pins}) ->
    {Port, Pin} = port_pin(Pins, Ln),
    Dir = proplists:get_value(dir, Opts, in),
    Pwm = case proplists:get_bool(pwm, Opts) of true -> "1"; false -> "0" end,
    {'INT', _, R} = analog_res(Res),
    Sgn = case proplists:get_value(type, analog_opts(Opts)) of
              unsigned -> "0"; _ -> "1" end,
    case {What, Dir} of
        {_, inout} -> throw({unsupported, "#analog inout", Ln});
        {"config", _} ->
            [?IND, "csp_chip_acfg(", Port, ", ", Pin, ", ", cdir(Dir), ", ",
             Pwm, ");\n"];
        {"input", in} ->
            [?IND, "s->", N, " = csp_lib_ain(csp_chip_ain(", Port, ", ", Pin,
             "), ", R, ", ", Sgn, ");\n"];
        {"output", out} ->
            [?IND, "csp_chip_aout(", Port, ", ", Pin, ", csp_lib_aout(s->", N,
             ", ", R, ", ", Sgn, "));\n"];
        _ -> []
    end;
io1(What, {object, _, {'WORD', _, M}, {'WORD', _, N}, _}) ->
    [?IND, M, "_", What, "(&s->", N, ");\n"];
io1(_, _) -> [].

cdir(in) -> "CSP_CHIP_IN";
cdir(out) -> "CSP_CHIP_OUT".

port_pin([{pin, _, P}], _) -> {"0", cexpr(P, #{})};
port_pin([{port_pin, _, Po, {range, Ln, _, _}}], _) ->
    throw({unsupported, "pin range", Ln}), {Po, Po};
port_pin([{port_pin, _, Po, Pi}], _) -> {cexpr(Po, #{}), cexpr(Pi, #{})};
port_pin(_, Ln) -> throw({unsupported, "pin list", Ln}).

%% ------------------------------------------------------------------
%% Rules
%% ------------------------------------------------------------------

%% Where = top for the module's own level, where a loose rule in Main is gated
%% to INIT and NORMAL; inside an #in block the block is the gate.
body(Ds, Ind, Env, Where) ->
    [stmt(D, Ind, Env, Where) || D <- Ds].

stmt({rule, Ln, Assigns, Guard} = R, Ind, Env, Where) ->
    Gate0 = case {Where, maps:get(main, Env)} of
               {top, true} -> ["(in->State == INIT || in->State == NORMAL)"];
               _ -> []
           end,
    Gate = [rule_on(R) | Gate0],
    Cond = case Guard of
               undefined -> Gate;
               _ -> Gate ++ [ccond(Guard, Env)]
           end,
    Body = [assign(A, Ln, Env) || A <- Assigns],
    guarded(Cond, Body, Ind);
stmt({{in, _, States}, Ds}, Ind, Env, _Where) ->
    C = lists:join(" || ", [["in->State == ", S] || {'WORD', _, S} <- States]),
    [Ind, "if (", C, ") {\n", body(Ds, Ind ++ ?IND, Env, block), Ind, "}\n"];
stmt({{'when', _, Cond}, Ds}, Ind, Env, Where) ->
    [Ind, "if (", unwrap(ccond(Cond, Env)), ") {\n",
     body(Ds, Ind ++ ?IND, Env, Where), Ind, "}\n"];
stmt({local, _, {'WORD', _, N}, _Res, Opts, E} = L, Ind, Env, _Where)
  when E =/= undefined ->
    %% `#local Y = f` and `#local Y out = f` are formulas, evaluated where
    %% they stand; only `#local X in = d` gives a default.
    case is_formula(Opts) of
        true ->
            case {is_stack(L), is_lside(L)} of
                {true, _} ->
                    {local, Ln, _, Res, _, _} = L,
                    [Ind, stack_store(N, Res, Opts, Ln, cexpr(E, Env))];
                {_, true} -> [Ind, loc(Env), N, " = ", cexpr(E, Env), ";\n"];
                {_, false} -> [Ind, "out->", N, " = ", cexpr(E, Env), ";\n"]
            end;
        false -> []
    end;
stmt({object, Ln, {'WORD', _, M}, {'WORD', _, N}, Args}, Ind, Env, _Where) ->
    object(M, N, Args, Ln, Ind, Env);
stmt({patch, Ln, _}, _, _, _) -> throw({unsupported, "> patch", Ln});
%% Rules are numbered by the runtime in instruction order; until the
%% translation numbers them the same way, a program that names one is refused.
stmt({disable, _, _}, _, _, _) -> [];       % csp_lib_setup, see disables/1
stmt({enable, _, _}, _, _, _) -> [];
stmt(_, _, _, _) -> [].

guarded([], Body, Ind) -> [[Ind, B] || B <- Body];
guarded([C], Body, Ind) -> guarded1(unwrap(C), Body, Ind);
guarded(Cs, Body, Ind) -> guarded1(lists:join(" && ", Cs), Body, Ind).

%% The outer parentheses of a lone condition, which `if (...)` supplies.
unwrap(C) ->
    case lists:flatten(C) of
        [$( | T] = F ->
            case balanced_outer(T, 1) of
                true -> lists:droplast(T);
                false -> F
            end;
        F -> F
    end.

%% True if the parenthesis opened at the start closes at the very end.
balanced_outer([$)], 1) -> true;
balanced_outer([$( | T], N) -> balanced_outer(T, N + 1);
balanced_outer([$) | _], 1) -> false;
balanced_outer([$) | T], N) -> balanced_outer(T, N - 1);
balanced_outer([_ | T], N) -> balanced_outer(T, N);
balanced_outer([], _) -> false.

guarded1(C, Body, Ind) ->
    [Ind, "if (", C, ") {\n",
     [[Ind, ?IND, B] || B <- Body], Ind, "}\n"].

assign({Op, _, {field, Ln, Lhs}, Rhs}, _, Env) when Op =:= '='; Op =:= '<-' ->
    store(lhs(Lhs, Ln, Env), cexpr(Rhs, Env));
assign({call, _, {'WORD', _, P}, Args}, _, Env)
  when P =:= "println"; P =:= "print" ->
    ["{ ", [print_arg(A, Env) || A <- Args],
     case P of "println" -> "csp_lib_nl(); "; _ -> "" end, "}\n"];
assign(Other, Ln, _) ->
    throw({unsupported, io_lib:format("statement ~p", [element(1, Other)]), Ln}).

print_arg({'STR', _, S}, _) -> ["csp_lib_puts(", cstring(S), "); "];
print_arg(E, Env) ->
    case is_unsigned(E, Env) of
        true -> ["csp_lib_putu(", cexpr(E, Env), "); "];
        false -> ["csp_lib_puti(", cexpr(E, Env), "); "]
    end.

cstring(S) -> io_lib:format("~p", [S]).

%% Where a store goes: a path, or {timer_val, Timer} for the one-bit `val` of a
%% timer, which keeps bit 0 the way the runtime's part does.
lhs({'WORD', _, N}, Ln, Env) ->
    W = wr(Env),
    case sym(N, Env) of
        {bfield, D} ->
            {Buf, Pos, Nb, Big, _} = bits_of(D),
            {bits, [W, Buf, ".b"], integer_to_list(Pos), integer_to_list(Nb), b01(Big)};
        {buffer, D} -> {bits, [W, N, ".b"], "0", scalar_bits(D, Ln), "0"};
        {timer, _} -> {timer_val, [W, N]};
        {const, _} -> throw({unsupported, "store to a constant", Ln});
        undefined when N =:= "State" -> [W, "State"];
        undefined -> throw({unsupported, "unknown name " ++ N, Ln});
        {var, D} ->
            case is_param(D) of
                true -> [par(Env), N];
                false -> [W, N]
            end;
        {local, D} ->
            case is_lside(D) of
                true -> [loc(Env), N];
                false -> [W, N]
            end;
        _ -> [W, N]
    end;
lhs({part, _, {'WORD', _, T}, {'WORD', _, P}}, Ln, Env) ->
    case sym(T, Env) of
        {timer, _} -> [wr(Env), T, ".", timer_part(P, Ln)];
        _ -> throw({unsupported, "part ." ++ P, Ln})
    end;
lhs({fld, _, {'WORD', _, O}, {'WORD', _, N}}, Ln, Env) ->
    case sym(O, Env) of
        {object, _, MSym} ->
            case maps:get(N, MSym, undefined) of
                {timer, _} -> {timer_val, [wr(Env), O, ".", N]};
                {var, D} ->
                    case is_param(D) of
                        true -> [par(Env), O, ".", N];
                        false -> [wr(Env), O, ".", N]
                    end;
                {local, D} ->
                    case is_lside(D) of
                        true -> [loc(Env), O, ".", N];
                        false -> [wr(Env), O, ".", N]
                    end;
                _ -> [wr(Env), O, ".", N]
            end;
        _ -> throw({unsupported, O ++ "." ++ N, Ln})
    end;
lhs(_, Ln, _) -> throw({unsupported, "left-hand side", Ln}).

%% A write to a timer is a request, even of the value it has: `T = 1' starts
%% it over, `T = 0' stops it (csp_lib_timer_out). One statement, as it may
%% stand alone after an `if'.
store({timer_val, T}, Rhs) -> [T, ".val = ", timer_val(Rhs), ", ", T, ".req = 1;\n"];
store({bits, B, Pos, N, Big}, Rhs) ->
    ["csp_lib_bits_set(", B, ", ", Pos, ", ", N, ", ", Big, ", ", Rhs, ");\n"];
store(Path, Rhs) -> [Path, " = ", Rhs, ";\n"].

timer_val(E) ->
    case string:to_integer(lists:flatten(E)) of
        {I, []} -> integer_to_list(I band 1);
        _ -> ["(uint8_t)((", E, ") & 1)"]
    end.

is_formula(Opts) ->
    not lists:member(proplists:get_value(dir, Opts), [in, inout]).

wr(Env) -> maps:get(wr, Env, "out->").

buf_part("rx", _) -> "rx";
buf_part("dlc", _) -> "dlc";
buf_part(P, Ln) -> throw({unsupported, "buffer part ." ++ P, Ln}).

timer_part("period", _) -> "period";
timer_part("fired", _) -> "fired";
timer_part("running", _) -> "running";
timer_part(P, Ln) -> throw({unsupported, "timer part ." ++ P, Ln}).

%% An instance: its bindings, its one-shot INIT values, then its body.
%%
%% A #local in is BOUND -- written every cycle before the call, `=` or `<-'
%% alike -- and one left unbound takes its default the same way. Any other
%% field given with `=` is set once, in the instance's own INIT; with `<-` it
%% follows the expression each cycle.
object(M, N, Args, Ln, Ind, Env) ->
    tuple_to_list(object_parts(M, N, Args, Ln, Ind, Env)).

%% {the bindings one by one, the defaults, the call}.
object_parts(M, N, Args, Ln, Ind, Env) ->
    {object, M, MSym} = sym(N, Env),
    Bound = [F || {_, _, {field, _, {'WORD', _, F}}, _} <- Args],
    Binds =
        [case maps:get(F, MSym, undefined) of
             {local, {local, _, _, _, Opts, _}} ->
                 case lists:member(proplists:get_value(dir, Opts), [in, inout]) of
                     true ->
                         [Ind, member(N, F, MSym, Env, E)];
                     false -> throw({unsupported, "binding a #local out", Ln})
                 end;
             _ when Op =:= '<-' ->
                 [Ind, "if (", rule_on(A), ") ", member(N, F, MSym, Env, E)];
             _ ->
                 [Ind, "if (", rule_on(A), " && in->", N, ".State == INIT) ",
                  member(N, F, MSym, Env, E)]
         end || {Op, _, {field, _, {'WORD', _, F}}, E} = A <- Args],
    Defaults =
        [case E of
             undefined ->
                 throw({unsupported, F ++ " is a #local in with no default", Ln});
             _ ->
                 [Ind, member_path(N, F, MSym, Env), " = ",
                  cexpr(E, Env#{sym => MSym, rd => ["in->", N, "."],
                                lrd => ["out->", N, "."],
                                par => [par(Env), N, "."],
                                loc => [loc(Env), N, "."]}), ";\n"]
         end
         || {F, {local, {local, _, _, _, Opts, E}}} <- maps:to_list(MSym),
            proplists:get_value(dir, Opts) =:= in,
            not lists:member(F, Bound)],
    Call = [Ind, M, "_run(&in->", N, ", &out->", N, sptrs(M, N, Env), ");\n"],
    {Binds, Defaults, Call}.

%% Where member F of instance N is written: its own side.
member_path(N, F, MSym, Env) ->
    case maps:get(F, MSym, undefined) of
        {var, D} -> case is_param(D) of
                        true -> [par(Env), N, ".", F];
                        false -> ["out->", N, ".", F]
                    end;
        {local, D} -> case is_lside(D) of
                          true -> [loc(Env), N, ".", F];
                          false -> ["out->", N, ".", F]
                      end;
        _ -> ["out->", N, ".", F]
    end.

member(N, F, MSym, Env, E) ->
    Path = member_path(N, F, MSym, Env),
    case maps:get(F, MSym, undefined) of
        {timer, _} -> store({timer_val, Path}, cexpr(E, Env));
        _ -> store(Path, cexpr(E, Env))
    end.

%% ------------------------------------------------------------------
%% Expressions. Every operand is widened to 32 bits first, because int is 16
%% on an AVR and the runtime computes in 32.
%% ------------------------------------------------------------------

ccond(E, Env) -> cbool(E, Env).

%% An expression where only zero or nonzero matters: a guard, an #when, an
%% operand of && and ||. No CSP_LIB_BOOL, since TRUE is -1 and any nonzero is
%% as good to an `if`; and no widening cast on a lone name, since nothing is
%% computed with it.
cbool({Op, _, _, _} = E, Env) when Op =:= '&&'; Op =:= '||' ->
    ["(", lists:join([" ", atom_to_list(Op), " "],
                     [cbool(X, Env) || X <- chain(Op, E)]), ")"];
cbool({'!', _, A}, Env) -> ["!", bparen(A, Env)];
cbool({Op, _, L, R}, Env)
  when Op =:= '<'; Op =:= '<='; Op =:= '>'; Op =:= '>=';
       Op =:= '=='; Op =:= '!=' ->
    ["(", cexpr(L, Env), " ", atom_to_list(Op), " ", cexpr(R, Env), ")"];
cbool({field, Ln, F}, Env) -> ref(F, Ln, Env#{nocast => true});
cbool({call, Ln, {'WORD', _, F}, Args}, Env)
  when F =:= "timeout"; F =:= "changed" ->
    call(F, Args, Ln, Env#{nocast => true});
cbool(E, Env) -> cexpr(E, Env).

%% a || b || c as one list, not as ((a || b) || c): the operator is
%% associative, and C reads the flat form the same way.
chain(Op, {Op, _, L, R}) -> chain(Op, L) ++ chain(Op, R);
chain(_, E) -> [E].

%% cbool, parenthesised unless it already is or needs none.
bparen({field, _, _} = E, Env) -> cbool(E, Env);
bparen({call, _, _, _} = E, Env) -> cbool(E, Env);
bparen({_, _, _, _} = E, Env) -> cbool(E, Env);
bparen(E, Env) -> ["(", cbool(E, Env), ")"].

sym(N, Env) -> maps:get(N, maps:get(sym, Env, #{}), undefined).

%% A global read from inside a module: the same name, looked up in Main and
%% read from Main's copies. A #local there is seen at once, as anywhere.
global_env(N, Inner, Env) ->
    Env#{sym => #{N => Inner}, rd => "csp_in.", lrd => "csp_out.",
         wr => "csp_out.", par => "csp_par.", loc => "csp_loc.", global => true}.

rd(Env) -> maps:get(rd, Env, "in->").
lrd(Env) -> maps:get(lrd, Env, "out->").

cexpr({'INT', _, I}, _) -> I;
cexpr({'FLT', Ln, _}, _) -> throw({unsupported, "float", Ln});
cexpr({'STR', Ln, _}, _) -> throw({unsupported, "string in an expression", Ln});
cexpr({field, Ln, F}, Env) -> ref(F, Ln, Env);
cexpr({call, Ln, {'WORD', _, F}, Args}, Env) -> call(F, Args, Ln, Env);
cexpr({'!', _, A}, Env) -> ["CSP_LIB_BOOL(!", bparen(A, Env), ")"];
cexpr({'~', _, A}, Env) -> ["(~", uparen(A, Env), ")"];
cexpr({'-', _, A}, Env) -> ["(-", uparen(A, Env), ")"];
cexpr({'+', _, A}, Env) -> uparen(A, Env);
cexpr({Op, _, L, R}, Env) when Op =:= '/'; Op =:= '%' ->
    F = case {Op, is_unsigned(L, Env) orelse is_unsigned(R, Env)} of
            {'/', false} -> "csp_lib_div";
            {'/', true} -> "csp_lib_divu";
            {'%', false} -> "csp_lib_rem";
            {'%', true} -> "csp_lib_remu"
        end,
    [F, "(", cexpr(L, Env), ", ", cexpr(R, Env), ")"];
%% cbool of these comes back in parentheses, which is what CSP_LIB_BOOL needs.
cexpr({Op, _, _, _} = E, Env)
  when Op =:= '&&'; Op =:= '||'; Op =:= '<'; Op =:= '<='; Op =:= '>';
       Op =:= '>='; Op =:= '=='; Op =:= '!=' ->
    ["CSP_LIB_BOOL", cbool(E, Env)];
cexpr({Op, _, _, _} = E, Env) when Op =:= '&'; Op =:= '|'; Op =:= '^' ->
    ["(", lists:join([" ", atom_to_list(Op), " "],
                     [cexpr(X, Env) || X <- chain(Op, E)]), ")"];
cexpr({Op, _, L, R}, Env)
  when Op =:= '+'; Op =:= '-'; Op =:= '*'; Op =:= '<<'; Op =:= '>>' ->
    ["(", cexpr(L, Env), " ", atom_to_list(Op), " ", cexpr(R, Env), ")"];
cexpr(E, _) ->
    throw({unsupported, io_lib:format("expression ~p", [element(1, E)]),
           element(2, E)}).

paren(E, Env) -> ["(", cexpr(E, Env), ")"].

%% The operand of a unary operator. A name, a number, a call and a binary
%% operation all come back from cexpr needing no more parentheses.
uparen({'INT', _, _} = E, Env) -> cexpr(E, Env);
uparen({field, _, _} = E, Env) -> cexpr(E, Env);
uparen({call, _, _, _} = E, Env) -> cexpr(E, Env);
uparen({_, _, _, _} = E, Env) -> cexpr(E, Env);
uparen(E, Env) -> paren(E, Env).


ref({'WORD', _, N} = W, Ln, Env) ->
    States = maps:get(states, Env, []),
    case sym(N, Env) of
        {bfield, D} ->
            {Buf, Pos, Nb, Big, Sgn} = bits_of(D),
            ["csp_lib_bits(", rd(Env), Buf, ".b, ", integer_to_list(Pos), ", ",
             integer_to_list(Nb), ", ", b01(Big), ", ", b01(Sgn), ")"];
        {buffer, D} ->
            ["csp_lib_bits(", rd(Env), N, ".b, 0, ", scalar_bits(D, Ln), ", 0, 0)"];
        {global, Inner} -> ref(W, Ln, global_env(N, Inner, Env));
        _ when N =:= "State" -> [rd(Env), "State"];
        undefined ->
            case lists:member(N, States) of
                true -> ["(int32_t)", N];
                false -> throw({unsupported, "unknown name " ++ N, Ln})
            end;
        {const, _} -> macro(N);
        {timer, _} -> [tcast(Env), rd(Env), N, ".val"];
        {local, D} ->                               % a #local is seen at once
            case {is_stack(D), is_lside(D)} of
                {true, _} -> svar(N);                % already 32 bits, its own type
                {_, true} -> [cast(D, Env), loc(Env), N];
                {_, false} -> [cast(D, Env), lrd(Env), N]
            end;
        {object, _, _} -> throw({unsupported, "an object as a value", Ln});
        {_, D} ->
            case is_param(D) of
                true -> [cast(D, Env), par(Env), N];
                false -> [cast(D, Env), rd(Env), N]
            end
    end;
ref({part, _, {'WORD', _, T}, {'WORD', _, P}}, Ln, Env) ->
    case sym(T, Env) of
        {timer, _} -> [tcast(Env), rd(Env), T, ".", timer_part(P, Ln)];
        {buffer, _} -> ["(int32_t)", rd(Env), T, ".", buf_part(P, Ln)];
        {bfield, D} ->
            {Buf, _, _, _, _} = bits_of(D),
            ["(int32_t)", rd(Env), Buf, ".", buf_part(P, Ln)];
        _ -> throw({unsupported, "part ." ++ P, Ln})
    end;
ref({fld, _, {'WORD', _, O}, {'WORD', _, _}} = F, Ln, Env) when
      element(1, map_get(O, map_get(sym, Env))) =:= global ->
    {global, Inner} = sym(O, Env),
    ref(F, Ln, global_env(O, Inner, Env));
ref({fld, _, {'WORD', _, O}, {'WORD', _, N}}, Ln, Env) ->
    case sym(O, Env) of
        {object, _, MSym} ->
            case maps:get(N, MSym, undefined) of
                {local, D} ->
                    case is_lside(D) of
                        true -> [cast(D, Env), loc(Env), O, ".", N];
                        false -> [cast(D, Env), lrd(Env), O, ".", N]
                    end;
                {timer, _} -> [tcast(Env), rd(Env), O, ".", N, ".val"];
                {_, D} ->
                    case is_param(D) of
                        true -> [cast(D, Env), par(Env), O, ".", N];
                        false -> [cast(D, Env), rd(Env), O, ".", N]
                    end;
                undefined when N =:= "State" -> [rd(Env), O, ".State"];
                undefined -> throw({unsupported, O ++ "." ++ N, Ln})
            end;
        _ -> throw({unsupported, O ++ "." ++ N, Ln})
    end;
ref({index, Ln, _, _}, _, _) -> throw({unsupported, "array index", Ln});
ref(_, Ln, _) -> throw({unsupported, "reference", Ln}).

%% Widening a member to 32 bits before it is computed with. Only a BIT-FIELD
%% needs it: one narrower than int promotes to int, which is 16 bits on an
%% AVR. A full int32_t or uint32_t member is already what it would be cast to.
cast(D, Env) ->
    case maps:get(nocast, Env, false) orelse not is_bitfield(D) of
        true -> "";
        false ->
            case is_unsigned_decl(D) of
                true -> "(uint32_t)";
                false -> "(int32_t)"
            end
    end.

is_bitfield({digital, _, _, _, _, _, _}) -> true;
is_bitfield({variable, _, _, _, Res, _, _}) -> narrow(Res);
is_bitfield({analog, _, _, _, Res, _, _}) -> narrow(analog_res(Res));
is_bitfield({param, _, _, Res, _, _}) -> narrow(Res);
is_bitfield({local, _, _, Res, _, _}) -> narrow(Res);
is_bitfield(_) -> true.

narrow(default) -> false;
narrow({'INT', _, "32"}) -> false;
narrow(_) -> true.

%% A timer's val, fired and running are uint8_t.
tcast(Env) ->
    case maps:get(nocast, Env, false) of
        true -> "";
        false -> "(int32_t)"
    end.

is_unsigned_decl({digital, _, _, _, _, _, _}) -> true;
is_unsigned_decl(D) when is_tuple(D) ->
    Opts = case D of
               {variable, _, _, _, _, O, _} -> O;
               {analog, _, _, _, _, O, _} -> analog_opts(O);
               {param, _, _, _, O, _} -> O;
               {local, _, _, _, O, _} -> O;
               _ -> []
           end,
    proplists:get_value(type, Opts) =:= unsigned;
is_unsigned_decl(_) -> false.

%% The usual arithmetic conversions, which are also the runtime's: one unsigned
%% operand makes the operation unsigned. Comparisons and logic yield a signed
%% truth value.
is_unsigned({field, _, {'WORD', _, N}}, Env) ->
    case sym(N, Env) of
        {global, {_, D}} -> is_unsigned_decl(D);
        {local, D} -> is_unsigned_decl(D);
        {Kind, D} when Kind =:= var; Kind =:= digital; Kind =:= analog ->
            is_unsigned_decl(D);
        _ -> false
    end;
is_unsigned({field, _, {fld, _, {'WORD', _, O}, {'WORD', _, N}}}, Env) ->
    case sym(O, Env) of
        {object, _, MSym} ->
            case maps:get(N, MSym, undefined) of
                {_, D} -> is_unsigned_decl(D);
                _ -> false
            end;
        _ -> false
    end;
is_unsigned({Op, _, L, R}, Env)
  when Op =:= '+'; Op =:= '-'; Op =:= '*'; Op =:= '/'; Op =:= '%';
       Op =:= '&'; Op =:= '|'; Op =:= '^' ->
    is_unsigned(L, Env) orelse is_unsigned(R, Env);
is_unsigned({Op, _, L, _}, Env) when Op =:= '<<'; Op =:= '>>' ->
    is_unsigned(L, Env);
is_unsigned({'~', _, A}, Env) -> is_unsigned(A, Env);
is_unsigned(_, _) -> false.

call("timeout", [{field, Ln, {'WORD', _, T}}], _, Env) ->
    case sym(T, Env) of
        {timer, _} ->
            case maps:get(nocast, Env, false) of
                true -> [rd(Env), T, ".fired"];
                false -> ["CSP_LIB_BOOL(", rd(Env), T, ".fired)"]
            end;
        _ -> throw({unsupported, "timeout of a non-timer", Ln})
    end;
%% changed(X): a store this cycle -- the input sweep or a rule before this one
%% -- left the working copy different from the committed one. The runtime's
%% dirty bit, read off the two copies instead of kept beside them. A #local is
%% compared the same way, committed against working, though it reads working.
call("changed", [{field, Ln, F}], _, Env) ->
    W = wr(Env),
    Cmp = ["(", ref(F, Ln, Env#{lrd => rd(Env), nocast => true}), " != ",
           ref(F, Ln, Env#{rd => W, lrd => W, nocast => true}), ")"],
    case maps:get(nocast, Env, false) of
        true -> Cmp;
        false -> ["CSP_LIB_BOOL", Cmp]
    end;
%% elapsed(T): how long it has run, or its period once it has stopped -- as
%% fn_elapsed has it.
call("elapsed", [{field, Ln, {'WORD', _, T}}], _, Env) ->
    case sym(T, Env) of
        {timer, _} ->
            R = rd(Env),
            ["(int32_t)(", R, T, ".running ? csp_now - ", R, T, ".t0 : ", R, T, ".period)"];
        _ -> throw({unsupported, "elapsed of a non-timer", Ln})
    end;
call("tick", [], _, _) -> "(int32_t)csp_now";
call("cycle", [], _, _) -> "(int32_t)csp_cycle";
call("min", [A, B], _, Env) -> ["csp_lib_min(", cexpr(A, Env), ", ", cexpr(B, Env), ")"];
call("max", [A, B], _, Env) -> ["csp_lib_max(", cexpr(A, Env), ", ", cexpr(B, Env), ")"];
call("abs", [A], _, Env) -> ["csp_lib_abs(", cexpr(A, Env), ")"];
call("clip", [X, L, H], _, Env) ->
    ["csp_lib_clip(", cexpr(X, Env), ", ", cexpr(L, Env), ", ", cexpr(H, Env), ")"];
call(F, _, Ln, _) -> throw({unsupported, F ++ "()", Ln}).

%% ------------------------------------------------------------------
%% The driver: setup and one step, plus a name table for the host harness.
%% ------------------------------------------------------------------

driver(Mods, Decls) ->
    ["// How many rules there are, for a listing and a range that runs past the\n",
     "// end -- #disable 2-99 clamps to the last.\n",
     "const uint16_t csp_lib_nrules = ", nrules(), ";\n\n",
     case graph() of
         false -> [];
         true -> rules()
     end,
     "void csp_lib_setup(void)\n{\n",
     disables(Decls),
     ?IND, "Main_init(&csp_in", main_sides(), ");\n",
     ?IND, "csp_out = csp_in;\n",
     case graph() of
         false -> [];
         true -> ["#if !defined(CSP_LIB_SEQ)\n",
                  ?IND, "csp_lib_wake_all();      // cycle 1 runs every step\n",
                  ?IND, "csp_prev = csp_in;\n",
                  "#endif\n"]
     end,
     "#if !defined(CSP_LIB_HOST)\n",
     ?IND, "Main_config(&csp_in);\n",
     "#endif\n",
     "}\n\n",
     "// One cycle at time `now`. Returns 1 if a rule changed anything, and sets\n",
     "// *wait to the ms until the next timer runs out (0xFFFFFFFF: none).\n",
     "int csp_lib_step(uint32_t now, uint32_t* wait)\n{\n",
     ?IND, "int changed;\n",
     ?IND, "csp_now = now;\n",
     ?IND, "csp_cycle++;\n",
     ?IND, "// `> X = v` in the program: INIT sees the declared value, the patch\n",
     ?IND, "// is there from cycle 2 on.\n",
     ?IND, "if (csp_cycle == 2) {\n",
     patches(Decls, Mods, "csp_in.", ?IND ++ ?IND),
     ?IND, ?IND, "csp_out = csp_in;\n",
     case graph() of
         false -> [];
         true -> ["#if !defined(CSP_LIB_SEQ)\n",
                  ?IND, ?IND, "csp_lib_wake_all();      // a patch may be a #param\n",
                  "#endif\n"]
     end,
     ?IND, "}\n",
     ?IND, "Main_timers_in(&csp_in, &csp_out, now);\n",
     case has_can("Main") of
         true ->
             [?IND, "// Frames from the bus: taken into the working copy, readable\n",
              ?IND, "// after the commit with .rx.\n",
              ?IND, "{\n",
              ?IND, ?IND, "uint32_t id;\n",
              ?IND, ?IND, "uint8_t d[8], n;\n",
              ?IND, ?IND, "while (csp_chip_can_recv(&id, d, &n))\n",
              ?IND, ?IND, ?IND, "Main_can(&csp_out, id, d, n);\n",
              ?IND, "}\n"];
         false -> []
     end,
     "#if !defined(CSP_LIB_HOST)\n",
     ?IND, "Main_input(&csp_out);\n",
     "#else\n",
     ?IND, "csp_lib_host_input();\n",
     "#endif\n",
     case graph() of
         false -> [?IND, "Main_run(&csp_in, &csp_out", main_sides(), ");\n"];
         true -> ["#if defined(CSP_LIB_SEQ)\n",
                  ?IND, "csp_run(0);\n",
                  "#else\n",
                  ?IND, "csp_cycle_steps();\n",
                  "#endif\n"]
     end,
     ?IND, "changed = memcmp(&csp_in, &csp_out, sizeof(csp_in)) != 0;\n",
     ?IND, "csp_in = csp_out;\n",
     case has_buf("Main") of
         true -> [?IND, "Main_rx(&csp_in, &csp_out);\n"];
         false -> []
     end,
     "#if !defined(CSP_LIB_HOST)\n",
     ?IND, "Main_output(&csp_in);\n",
     "#endif\n",
     ?IND, "Main_timers_out(&csp_in, &csp_out, now);\n",
     ?IND, "*wait = 0xFFFFFFFFu;\n",
     ?IND, "Main_wait(&csp_in, now, wait);\n",
     ?IND, "return changed;\n",
     "}\n\n",
     names(Mods, Decls),
     "#if defined(CSP_LIB_MAIN)\n",
     "int main(void)\n{\n",
     ?IND, "uint32_t wait;\n",
     ?IND, "csp_chip_init();\n",
     ?IND, "csp_lib_setup();\n",
     ?IND, "for (;;)\n",
     ?IND, ?IND, "(void)csp_lib_step(csp_chip_millis(), &wait);\n",
     "}\n",
     "#endif\n"].

rules() ->
    Insts = insts(get(csp_main_ds), get(csp_mods), "", "Main", ""),
    Used = uniq([M || {_, M, _} <- Insts], []),
    Ix = maps:from_list(lists:zip(Used, lists:seq(0, length(Used) - 1))),
    {Nk, Kbase, Common, Extra} = graph(Insts),
    Xofs = lists:reverse(lists:foldl(fun({_, X}, [H | _] = A) -> [H + length(X) | A] end,
                                     [0], Extra)),
    MaxW = lists:max([(length(diff_leaves(M)) + 31) div 32 || M <- Used]),
    Off = fun(_, "") -> "0";
             (T, C) -> ["offsetof(", T, ", ", tl(C), ")"]
          end,
    %% No side in Main: none anywhere.
    Base = fun(true, V, F) -> ["(uint8_t*)&", V, " + x->", F];
              (false, _, _) -> "0"
           end,
    Call = ["m->fn[e]((uint8_t*)&csp_in + x->in, (uint8_t*)&csp_out + x->in,\n",
            ?IND, ?IND, ?IND, "         ", Base(has_loc("Main"), "csp_loc", "loc"), ", ",
            Base(has_par("Main"), "csp_par", "par"), ")"],
    [[begin
          C = maps:get(M, Common),
          ["// ", M, ": what each slot wakes, from the instance's first step.\n",
           "static const uint16_t ", M, "_eofs[] = {\n",
           table(lists:reverse(lists:foldl(fun(E, [H | _] = A) -> [H + length(E) | A] end,
                                           [0], C))),
           "};\n\n",
           "static const uint16_t ", M, "_edg[] = {\n",
           table(case lists:append(C) of [] -> [0]; L -> L end),
           "};\n\n"]
      end || M <- Used],
     "typedef struct {\n",
     ?IND, "const csp_rule_fn_t* fn;\n",
     ?IND, "const uint16_t* wofs;\n",
     ?IND, "const uint16_t (*wmap)[2];\n",
     ?IND, "const uint16_t* steps;\n",
     ?IND, "const uint16_t* eofs;\n",
     ?IND, "const uint16_t* edg;\n",
     ?IND, "csp_diff_fn_t diff;\n",
     ?IND, "uint16_t nsteps;\n",
     ?IND, "uint8_t nleaves;\n",
     "} csp_mod_t;\n\n",
     "typedef struct {\n",
     ?IND, "uint8_t mod;\n",
     ?IND, "uint16_t in, loc, par;     // offsets in csp_in (and csp_out), csp_loc, csp_par\n",
     ?IND, "uint16_t k;                // its first step\n",
     ?IND, "uint16_t x;                // its own wakes in csp_xs, up to the next one's\n",
     "} csp_inst_t;\n\n",
     "static const csp_mod_t csp_mods[] = {\n",
     [[?IND, "{ ", M, "_fn, ", M, "_wofs, ", M, "_wmap, ", M, "_steps, ", M, "_eofs, ", M, "_edg, (csp_diff_fn_t)", M, "_diff,\n",
       ?IND, "  sizeof(", M, "_steps) / sizeof(uint16_t), ",
       integer_to_list(length(diff_leaves(M))), " },\n"]
      || M <- Used],
     "};\n\n",
     "static const csp_inst_t csp_insts[] = {\n",
     [[?IND, "{ ", integer_to_list(maps:get(M, Ix)), ", ",
       Off("Main_t", C), ", ",
       case has_loc(M) of true -> Off("Main_local_t", C); false -> "0" end, ", ",
       case has_par(M) of true -> Off("Main_param_t", C); false -> "0" end, ", ",
       integer_to_list(maps:get(I, Kbase)), ", ", integer_to_list(X),
       " },", case P of "" -> ""; _ -> ["    // ", P] end, "\n"]
      || {I, {P, M, C}, X} <- lists:zip3(lists:seq(0, length(Insts) - 1), Insts,
                                         lists:droplast(Xofs))],
     "};\n\n",
     "#define CSP_NINST ", integer_to_list(length(Insts)), "\n",
     "#define CSP_NK ", integer_to_list(Nk), "          // steps, all instances\n",
     "#define CSP_NX ", integer_to_list(lists:last(Xofs)), "\n",
     "#define CSP_MAXW ", integer_to_list(MaxW), "\n\n",
     "#if defined(CSP_LIB_SEQ)\n",
     "// Instance i and its subtree, step by step. A step with the top bit set\n",
     "// is a child instance, that many on.\n",
     "static void csp_run(uint16_t i)\n{\n",
     ?IND, "const csp_inst_t* x = &csp_insts[i];\n",
     ?IND, "const csp_mod_t* m = &csp_mods[x->mod];\n",
     ?IND, "uint16_t s;\n\n",
     ?IND, "for (s = 0; s < m->nsteps; s++) {\n",
     ?IND, ?IND, "uint16_t e = m->steps[s];\n",
     ?IND, ?IND, "if (e & 0x8000)\n",
     ?IND, ?IND, ?IND, "csp_run(i + (e & 0x7fff));\n",
     ?IND, ?IND, "else\n",
     ?IND, ?IND, ?IND, "(void)", Call, ";\n",
     ?IND, "}\n",
     "}\n",
     "#else\n",
     "// The wakes that are an instance's own, {slot, k}.\n",
     "static const uint16_t csp_xs[][2] = {\n",
     case lists:append([X || {_, X} <- Extra]) of
         [] -> [?IND, "{ 0xFFFF, 0 }\n"];
         Xs -> [[?IND, "{ ", integer_to_list(Sl), ", ", integer_to_list(E), " },\n"] || {Sl, E} <- Xs]
     end,
     "};\n\n",
     "// A bit per step: this cycle's and the next's. A step woken by one that\n",
     "// runs before it in the order runs this cycle, one woken by a later step\n",
     "// the next. Cycle 1 runs them all.\n",
     "static uint32_t csp_wake[2][(CSP_NK + 31) / 32];\n",
     "static uint8_t csp_gen;\n",
     "static uint16_t csp_k;\n",
     "static Main_t csp_prev;          // csp_in as the steps last saw it\n\n",
     "static void csp_set(uint16_t t, uint16_t k)\n{\n",
     ?IND, "uint32_t* w = csp_wake[((k == 0xFFFF) || (t > k)) ? csp_gen : csp_gen ^ 1];\n",
     ?IND, "w[t >> 5] |= 1u << (t & 31);\n",
     "}\n\n",
     "// Slot s of instance i, with step k running (0xFFFF: none yet).\n",
     "static void csp_edges(uint16_t i, uint16_t s, uint16_t k)\n{\n",
     ?IND, "const csp_inst_t* x = &csp_insts[i];\n",
     ?IND, "const csp_mod_t* m = &csp_mods[x->mod];\n",
     ?IND, "uint16_t e, end;\n\n",
     ?IND, "for (e = m->eofs[s]; e < m->eofs[s + 1]; e++)\n",
     ?IND, ?IND, "csp_set(x->k + m->edg[e], k);\n",
     ?IND, "end = (i + 1 < CSP_NINST) ? x[1].x : CSP_NX;\n",
     ?IND, "for (e = x->x; e < end; e++)\n",
     ?IND, ?IND, "if (csp_xs[e][0] == s)\n",
     ?IND, ?IND, ?IND, "csp_set(csp_xs[e][1], k);\n",
     "}\n\n",
     "// What changed in the committed copy since the steps last ran -- the\n",
     "// commit, a timer, a frame, a patch -- wakes what reads it; what an input\n",
     "// left in the working copy wakes what reads it there.\n",
     "static void csp_diff(void)\n{\n",
     ?IND, "uint16_t i, b;\n",
     ?IND, "uint32_t m[CSP_MAXW];\n\n",
     ?IND, "for (i = 0; i < CSP_NINST; i++) {\n",
     ?IND, ?IND, "const csp_inst_t* x = &csp_insts[i];\n",
     ?IND, ?IND, "const csp_mod_t* md = &csp_mods[x->mod];\n",
     ?IND, ?IND, "md->diff((uint8_t*)&csp_in + x->in, (uint8_t*)&csp_prev + x->in, m);\n",
     ?IND, ?IND, "for (b = 0; b < md->nleaves; b++)\n",
     ?IND, ?IND, ?IND, "if (m[b >> 5] & (1u << (b & 31)))\n",
     ?IND, ?IND, ?IND, ?IND, "csp_edges(i, b, 0xFFFF);\n",
     ?IND, ?IND, "md->diff((uint8_t*)&csp_out + x->in, (uint8_t*)&csp_in + x->in, m);\n",
     ?IND, ?IND, "for (b = 0; b < md->nleaves; b++)\n",
     ?IND, ?IND, ?IND, "if (m[b >> 5] & (1u << (b & 31)))\n",
     ?IND, ?IND, ?IND, ?IND, "csp_edges(i, md->nleaves + b, 0xFFFF);\n",
     ?IND, "}\n",
     ?IND, "csp_prev = csp_in;\n",
     "}\n\n",
     "static void csp_react(uint16_t i)\n{\n",
     ?IND, "const csp_inst_t* x = &csp_insts[i];\n",
     ?IND, "const csp_mod_t* m = &csp_mods[x->mod];\n",
     ?IND, "uint16_t s;\n\n",
     ?IND, "for (s = 0; s < m->nsteps; s++) {\n",
     ?IND, ?IND, "uint16_t e = m->steps[s];\n",
     ?IND, ?IND, "if (e & 0x8000) {\n",
     ?IND, ?IND, ?IND, "csp_react(i + (e & 0x7fff));\n",
     ?IND, ?IND, ?IND, "continue;\n",
     ?IND, ?IND, "}\n",
     ?IND, ?IND, "if (csp_wake[csp_gen][csp_k >> 5] & (1u << (csp_k & 31))) {\n",
     ?IND, ?IND, ?IND, "uint32_t w = ", Call, ";\n",
     ?IND, ?IND, ?IND, "uint16_t b;\n",
     ?IND, ?IND, ?IND, "for (b = m->wofs[e]; w; b++, w >>= 1) {\n",
     ?IND, ?IND, ?IND, ?IND, "uint16_t t = m->wmap[b][0], sl = m->wmap[b][1];\n",
     ?IND, ?IND, ?IND, ?IND, "if (!(w & 1))\n",
     ?IND, ?IND, ?IND, ?IND, ?IND, "continue;\n",
     ?IND, ?IND, ?IND, ?IND, "if (sl == 0xFFFF)\n",
     ?IND, ?IND, ?IND, ?IND, ?IND, "csp_set(csp_k, csp_k);\n",
     ?IND, ?IND, ?IND, ?IND, "else\n",
     ?IND, ?IND, ?IND, ?IND, ?IND, "csp_edges((t & 0x8000) ? (t & 0x7fff) : i + t, sl, csp_k);\n",
     ?IND, ?IND, ?IND, "}\n",
     ?IND, ?IND, "}\n",
     ?IND, ?IND, "csp_k++;\n",
     ?IND, "}\n",
     "}\n\n",
     "void csp_lib_wake_all(void)\n{\n",
     ?IND, "memset(csp_wake[csp_gen], 0xFF, sizeof(csp_wake[0]));\n",
     "}\n\n",
     "static void csp_cycle_steps(void)\n{\n",
     ?IND, "csp_diff();\n",
     ?IND, "csp_k = 0;\n",
     ?IND, "csp_react(0);\n",
     ?IND, "memset(csp_wake[csp_gen], 0, sizeof(csp_wake[0]));\n",
     ?IND, "csp_gen ^= 1;\n",
     "}\n",
     "#endif\n\n"].

table(L) ->
    [[?IND, lists:join(", ", [integer_to_list(X) || X <- Row]), ",\n"] || Row <- rows(L, 12)].

rows([], _) -> [];
rows(L, N) when length(L) =< N -> [L];
rows(L, N) -> {H, T} = lists:split(N, L), [H | rows(T, N)].

%% What wakes what. The steps are numbered k in the order the run functions
%% take them, instance by instance. A step reads a field committed (in->)
%% or as it is being written this cycle (out->, loc->); it writes into out->
%% and loc->. Then:
%%
%%   a field that differs from what the steps last saw wakes every step that
%%     reads it -- this cycle, before any runs; one an input left different in
%%     the working copy wakes those that read it as it is being written;
%%   a field in the copies that a step leaves different from the committed
%%     one wakes the steps that read it as it is being written -- the later
%%     ones this cycle, the earlier ones (also woken by the commit) the next;
%%   a #local a step changes wakes its readers: the later ones this cycle,
%%     the earlier ones (which read it before it changed) the next.
%%
%% And steps that write the same field are woken together, so the last one
%% in the order still has the last word, as it has when all of them run.
%%
%% The steps of an instance's subtree come one after the other, as many for
%% every instance of the module, so a wake inside the subtree is the same
%% distance from its first step wherever the instance is: those are kept per
%% module. What differs -- a parent that reads a child's field, a global --
%% is kept per instance, as {slot, k}.
%%
%% A module's slots: its fields committed, the same being written, then its
%% #locals. A step's bit names the slot of the field it changed.
graph(Insts) ->
    Pcs = list_to_tuple([case P of "" -> []; _ -> string:split(P, ".", all) end
                         || {P, _, _} <- Insts]),
    Sched = sched("Main", 0),
    Info = [{K, I, Mod, element(J + 1, get({csp_stepinfo, Mod}))}
            || {K, {I, Mod, J}} <- lists:zip(lists:seq(0, length(Sched) - 1), Sched)],
    Acc0 = {#{}, #{}, #{}},                     % in-readers, cur-readers, writers
    {InR, CurR, Wr} =
        lists:foldl(
          fun({K, I, Mod, {Text, W}}, {A, B, C}) ->
                  Rs = [{Side, Id, Eq} || {Side, Abs, Path, Eq} <- refs(Text),
                                          Side =/= par,
                                          {ok, Id} <- [abs_leaf(Mod, Pcs, I, Side, Abs, Path)]],
                  A1 = lists:foldl(fun({S, Id, _}, X) when S =:= in; S =:= out -> add(Id, K, X);
                                      (_, X) -> X end, A, Rs),
                  B1 = lists:foldl(fun({out, Id, _}, X) -> add(Id, K, X);
                                      ({loc, Id, false}, X) -> add(Id, K, X);
                                      (_, X) -> X end, B, Rs),
                  C1 = lists:foldl(fun(Id, X) -> add(Id, K, X) end, C,
                                   [wid(Pcs, I, X) || X <- W, X =/= always]),
                  {A1, B1, C1}
          end, Acc0, Info),
    Comp = components(Wr),
    Exp = fun(Ks) -> lists:usort(lists:flatmap(fun(K) -> maps:get(K, Comp, [K]) end, Ks)) end,
    Get = fun(Id, M) -> maps:get(Id, M, []) end,
    {Kbase, _} = kbases("Main", 0, 0, #{}),
    %% Every instance's slots, absolute: a list of wake lists per instance.
    Abs = [begin
               Pc = element(I + 1, Pcs),
               Ids = [{state, Pc ++ [F]} || {F, _} <- diff_leaves(M)],
               Ls = [{loc, Pc ++ [F]} || F <- loc_leaves(M)],
               {I, M, [Exp(Get(Id, InR)) || Id <- Ids] ++ [Exp(Get(Id, CurR)) || Id <- Ids ++ Ls]}
           end || {I, {_, M, _}} <- lists:zip(lists:seq(0, length(Insts) - 1), Insts)],
    %% Relative to the instance's first step, inside its subtree.
    Rel = fun(I, M, Es) ->
                  B = maps:get(I, Kbase),
                  N = length(sched(M, 0)),
                  [E - B || E <- Es, E >= B, E < B + N]
          end,
    Mods = uniq([M || {_, M, _} <- Insts], []),
    Common = maps:from_list(
               [{M, begin
                        Per = [[ordsets:from_list(Rel(I, M, Es)) || Es <- Slots]
                               || {I, M1, Slots} <- Abs, M1 =:= M],
                        lists:foldl(fun(P, Acc) -> lists:zipwith(fun ordsets:intersection/2, P, Acc) end,
                                    hd(Per), tl(Per))
                    end} || M <- Mods]),
    Extra = [{I, [{S, E} || {S, Es, C} <- lists:zip3(lists:seq(0, length(Slots) - 1), Slots,
                                                       maps:get(M, Common)),
                            E <- Es,
                            not ordsets:is_element(E - maps:get(I, Kbase), C)]}
             || {I, M, Slots} <- Abs],
    {length(Sched), Kbase, Common, Extra}.

%% k of the first step of every instance's subtree.
kbases(Mod, I, K, Acc) ->
    lists:foldl(fun({fn, _}, {A, X}) -> {A, X + 1};
                   ({call, M, N}, {A, X}) -> kbases(M, I + child_offset(Mod, N), X, A)
                end, {Acc#{I => K}, K}, get({csp_steps, Mod})).

%% The schedule: {instance, module, step}, the order csp_run takes them.
sched(Mod, I) ->
    lists:flatmap(fun({fn, J}) -> [{I, Mod, J}];
                     ({call, M, N}) -> sched(M, I + child_offset(Mod, N))
                  end, get({csp_steps, Mod})).

%% A field by where it is from the top: {state | loc, Path}.
abs_leaf(Mod, Pcs, I, Side, Abs, Path) ->
    {M0, Base} = case Abs of
                     true -> {"Main", []};
                     false -> {Mod, element(I + 1, Pcs)}
                 end,
    S = case Side of loc -> loc; _ -> state end,
    case leaf_of(M0, S, Path) of
        {ok, L, _} -> {ok, {S, Base ++ L}};
        error -> error
    end.

wid(Pcs, I, {Side, Abs, L, _}) ->
    Base = case Abs of true -> []; false -> element(I + 1, Pcs) end,
    {Side, Base ++ L}.

add(Id, K, M) -> maps:update_with(Id, fun(L) -> [K | L] end, [K], M).

%% Steps that write a common field, as k -> every k of its group.
components(Wr) ->
    Parent = maps:fold(fun(_, Ks, P) -> unite(lists:usort(Ks), P) end, #{}, Wr),
    Groups = maps:fold(fun(K, _, G) -> add(find(K, Parent), K, G) end, #{}, Parent),
    maps:fold(fun(_, Ks, M) ->
                      S = lists:usort(Ks),
                      lists:foldl(fun(K, X) -> X#{K => S} end, M, S)
              end, #{}, Groups).

unite([], P) -> P;
unite([K | Ks], P0) ->
    P1 = case maps:is_key(K, P0) of true -> P0; false -> P0#{K => K} end,
    R = find(K, P1),
    lists:foldl(fun(X, P) ->
                        P2 = case maps:is_key(X, P) of true -> P; false -> P#{X => X} end,
                        Rx = find(X, P2),
                        case Rx =:= R of
                            true -> P2;
                            false -> P2#{Rx => R}
                        end
                end, P1, Ks).

find(K, P) ->
    case maps:get(K, P) of
        K -> K;
        Up -> find(Up, P)
    end.

main_sides() ->
    [case has_loc("Main") of true -> ", &csp_loc"; false -> "" end,
     case has_par("Main") of true -> ", &csp_par"; false -> "" end].

%% `> X = v` lines in the program file, applied once.
patches(Decls, Mods, S, Ind) ->
    Env = #{sym => symbols(Decls, Mods), mods => Mods, states => [],
            wr => S, rd => S, lrd => S, par => "csp_par.", loc => "csp_loc."},
    [[Ind, store(lhs(L, Ln, Env), cexpr(R, Env))]
     || {immediate, {'=', _, {field, Ln, L}, R}} <- Decls].

%% For the host harness: every scalar by its runtime name ("m.V" for a member),
%% read from the committed copy and written into the working one -- where a
%% stimulus lands in port/csp_linux.c. Not a function per name: a module lists
%% its fields once and has one getter and one setter, and an instance is its
%% path and its part of each side. See csp_lib_host.h.
names(Mods, Decls) ->
    Insts = insts(Decls, Mods, "", "Main", ""),
    Used = uniq([M || {_, M, _} <- Insts], []),
    ["#if defined(CSP_LIB_HOST)\n",
     "#include \"csp_lib_host.h\"\n\n",
     [accessor(M, case M of "Main" -> Decls; _ -> maps:get(M, Mods) end) || M <- Used],
     "const csp_lib_inst_t csp_lib_insts[] = {\n",
     [[?IND, "{ \"", P, "\", &", M, "_mod, &csp_in", C, ", &csp_out", C, ", ",
       case has_par(M) of true -> ["&csp_par", C]; false -> "0" end, " },\n"]
      || {P, M, C} <- Insts],
     ?IND, "{ 0, 0, 0, 0, 0 }\n};\n",
     "#endif\n\n"].

%% Every instance, depth first: {runtime path, module, C member path}.
insts(Ds, Mods, Path, Mod, C) ->
    [{Path, Mod, C} |
     lists:flatmap(fun({object, _, {'WORD', _, M}, {'WORD', _, N}, _}) ->
                           P = case Path of "" -> N; _ -> Path ++ "." ++ N end,
                           insts(maps:get(M, Mods), Mods, P, M, C ++ "." ++ N);
                      (_) -> []
                   end, Ds)].

%% A module's fields, numbered from 0 in declaration order, State first. A
%% #local is the module's own and has no access from outside: it may well be
%% a C variable that is gone when the run function returns.
accessor(M, Ds) ->
    Fs = [{N, case is_param(D) of
                  true -> param;
                  false -> state
              end} || D <- [{state} | Ds], element(1, D) =/= local,
                      N <- [name_of(D)], N =/= undefined],
    Cases = lists:zip(lists:seq(0, length(Fs) - 1), Fs),
    ["static const char* const ", M, "_fields[] = {\n",
     [[?IND, "\"", N, "\",\n"] || {N, _} <- Fs],
     "};\n\n",
     "static int32_t ", M, "_get(const csp_lib_inst_t* x, uint16_t f)\n{\n",
     ?IND, "switch (f) {\n",
     [[?IND, "case ", integer_to_list(I), ": return (int32_t)",
       side_ref(M, Side, get), "->", N, ";\n"] || {I, {N, Side}} <- Cases],
     ?IND, "}\n",
     ?IND, "return 0;\n",
     "}\n\n",
     "static void ", M, "_set(const csp_lib_inst_t* x, uint16_t f, int32_t v)\n{\n",
     ?IND, "switch (f) {\n",
     [[?IND, "case ", integer_to_list(I), ": ",
       side_ref(M, Side, set), "->", N, " = v; break;\n"] || {I, {N, Side}} <- Cases],
     ?IND, "}\n",
     "}\n\n",
     "static const csp_lib_module_t ", M, "_mod = {\n",
     ?IND, integer_to_list(length(Fs)), ", ", M, "_fields, ", M, "_get, ", M, "_set\n",
     "};\n\n"].

side_ref(M, state, get) -> ["((const ", M, "_t*)x->in)"];
side_ref(M, state, set) -> ["((", M, "_t*)x->out)"];
side_ref(M, param, _) -> ["((", M, "_param_t*)x->par)"].

name_of({state}) -> "State";
name_of({variable, _, {'WORD', _, N}, _, _, _, _}) -> N;
name_of({param, _, {'WORD', _, N}, _, _, _}) -> N;
name_of({local, _, {'WORD', _, N}, _, _, _}) -> N;
name_of({digital, _, {'WORD', _, N}, _, _, _, _}) -> N;
name_of({analog, _, {'WORD', _, N}, _, _, _, _}) -> N;
name_of(_) -> undefined.
