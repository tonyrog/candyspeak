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

%% What each option asks of the translation. --graph is the next step and is
%% refused until it is there, rather than ignored.
options(Opts) ->
    [throw({unsupported, "--graph (not yet)", 0}) || graph <- Opts],
    ok.

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

gen({{module, _, {'WORD', _, "Main"}}, Bound, Decls}, In) ->
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
    put(csp_fields, sets:from_list([N || {_, Ds} <- Mains, D <- flat(Ds),
                                         N <- [name_of(D)], N =/= undefined])),
    put(csp_haspar, has_tree(Mains, fun is_param/1)),
    put(csp_hasloc, has_tree(Mains, fun is_lside/1)),
    {_, RuleNo} = number_rules(Decls, {1, #{}}),
    put(csp_ruleno, RuleNo),
    put(csp_hascan, has_tree(Mains, fun({buffer, _, _, _, _, [{can, _}]}) -> true;
                                       (_) -> false end)),
    put(csp_hasbuf, has_tree(Mains, fun({buffer, _, _, _, _, _}) -> true;
                                       (_) -> false end)),
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
     [module(M, maps:get(M, Mods), Mods, States, Globals) || M <- Order],
     module("Main", Decls, Mods, States, Consts),
     driver(Mods, Decls)].

%% ------------------------------------------------------------------
%% Which #locals have one copy
%% ------------------------------------------------------------------
%%
%% A #local is on its own side, Name_local_t, with ONE copy: written, it is
%% readable at once, it takes no part in the commit, and any rule can be run
%% at any time and find it -- no C variable that lives only while the run
%% function does. The exception is a local changed() is asked of: that
%% compares against last cycle's value, so it keeps the committed and working
%% pair in Name_t, as the runtime gives a watched local a slot of its own.
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
     "#include <stdint.h>\n#include <string.h>\n#include \"csp_lib.h\"\n",
     "#if defined(CSP_LIB_HOST)\n#include \"csp_lib_host.h\"\n#endif\n\n",
     "// tick() and cycle(): the time of this cycle and its number, from 1.\n",
     "static uint32_t csp_now;\nstatic uint32_t csp_cycle;\n\n",
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
     "static void ", Name, "_run(", T, "* in, ", T, "* out", sargs(Name), ")\n{\n",
     timer_starts(Ds, Env),
     body(Ds, ?IND, Env, top),
     ?IND, "// INIT lasts one cycle unless a rule said otherwise; FAILSAFE\n",
     ?IND, "// is left only by a reset.\n",
     ?IND, "if ((in->State == INIT) && (out->State == INIT))\n",
     ?IND, ?IND, "out->State = NORMAL;\n",
     ?IND, "if (in->State == FAILSAFE)\n",
     ?IND, ?IND, "out->State = FAILSAFE;\n",
     "}\n\n"].

struct(Name, Ds) ->
    ["typedef struct {\n",
     ?IND, "int32_t State;\n",
     [field(D) || D <- Ds, not is_lside(D), not is_param(D)],
     "} ", Name, "_t;\n\n"].

%% ------------------------------------------------------------------
%% Rule numbers, for #disable
%% ------------------------------------------------------------------
%%
%% Rule N is the Nth OP_RULE the compiler lays down, counted through the whole
%% program in source order: a module's body where its #module stands, an #in or
%% #when block's rules where the block stands. A rule counts, a #local formula
%% counts, and so does each binding on an instance line (`X <- A`, `X = 3`). A
%% `>' patch does not. That is what /list numbers -- check it there.
%%
%% Each guard in the C carries its number as CSP_ON(n): a bit test against the
%% disable mask when the build says -DCSP_LIB_RULES, the constant 1 (and no
%% code at all) when it does not.
number_rules([D | T], Acc) -> number_rules(T, number_rule(D, Acc));
number_rules([], Acc) -> Acc.

number_rule({rule, _, _, _} = R, {N, M}) -> {N + 1, M#{R => N}};
number_rule({local, _, _, _, Opts, E} = L, {N, M}) when E =/= undefined ->
    case is_formula(Opts) of
        true -> {N + 1, M#{L => N}};
        false -> {N, M}
    end;
number_rule({object, _, _, _, Args}, Acc) ->
    lists:foldl(fun(A, {N, M}) -> {N + 1, M#{A => N}} end, Acc, Args);
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
              Own = lists:any(Pred, Ds),
              Sub = lists:any(fun({object, _, {'WORD', _, X}, _, _}) -> maps:get(X, Acc, false);
                                 (_) -> false end, Ds),
              Acc#{M => Own orelse Sub}
      end, #{}, Mains).

has_can(M) -> maps:get(M, get(csp_hascan), false).
has_buf(M) -> maps:get(M, get(csp_hasbuf), false).

side(Name, Side, Ds) ->
    {Has, In, T} = case Side of
                       local -> {fun has_loc/1, fun is_lside/1, "_local_t"};
                       param -> {fun has_par/1, fun is_param/1, "_param_t"}
                   end,
    case Has(Name) of
        false -> [];
        true ->
            ["typedef struct {\n",
             [field(D) || D <- Ds, In(D)],
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
      end, #{}, Ds).

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
      store({timer_val, ["out->", N, ".val"]}, cexpr(E, Env))]
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
    %% they stand; only `#local X in = d` gives a default. A formula is a rule
    %% to #disable too, and a disabled one keeps the value it last had.
    case is_formula(Opts) of
        true ->
            Body = case is_lside(L) of
                       true -> [loc(Env), N, " = ", cexpr(E, Env), ";\n"];
                       false -> ["out->", N, " = ", cexpr(E, Env), ";\n"]
                   end,
            [Ind, "if (", rule_on(L), ") ", Body];
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

%% Where a store goes: a path, or {timer_val, Path} for the one-bit `val` of a
%% timer, which keeps bit 0 the way the runtime's part does.
lhs({'WORD', _, N}, Ln, Env) ->
    W = wr(Env),
    case sym(N, Env) of
        {bfield, D} ->
            {Buf, Pos, Nb, Big, _} = bits_of(D),
            {bits, [W, Buf, ".b"], integer_to_list(Pos), integer_to_list(Nb), b01(Big)};
        {buffer, D} -> {bits, [W, N, ".b"], "0", scalar_bits(D, Ln), "0"};
        {timer, _} -> {timer_val, [W, N, ".val"]};
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
                {timer, _} -> {timer_val, [wr(Env), O, ".", N, ".val"]};
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

store({timer_val, Path}, Rhs) -> [Path, " = ", timer_val(Rhs), ";\n"];
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
    {object, M, MSym} = sym(N, Env),
    Bound = [F || {_, _, {field, _, {'WORD', _, F}}, _} <- Args],
    Binds =
        [case maps:get(F, MSym, undefined) of
             {local, {local, _, _, _, Opts, _}} ->
                 case lists:member(proplists:get_value(dir, Opts), [in, inout]) of
                     true ->
                         [Ind, "if (", rule_on(A), ") ", member(N, F, MSym, Env, E)];
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
    [Binds, Defaults, Ind, M, "_run(&in->", N, ", &out->", N, sptrs(M, N, Env), ");\n"].

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
        {timer, _} -> store({timer_val, [Path, ".val"]}, cexpr(E, Env));
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
            case is_lside(D) of
                true -> [cast(D, Env), loc(Env), N];
                false -> [cast(D, Env), lrd(Env), N]
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
     "void csp_lib_setup(void)\n{\n",
     disables(Decls),
     ?IND, "Main_init(&csp_in", main_sides(), ");\n",
     ?IND, "csp_out = csp_in;\n",
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
     ?IND, "Main_run(&csp_in, &csp_out", main_sides(), ");\n",
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
       case has_loc(M) of true -> ["&csp_loc", C]; false -> "0" end, ", ",
       case has_par(M) of true -> ["&csp_par", C]; false -> "0" end, " },\n"]
      || {P, M, C} <- Insts],
     ?IND, "{ 0, 0, 0, 0, 0, 0 }\n};\n",
     "#endif\n\n"].

%% Every instance, depth first: {runtime path, module, C member path}.
insts(Ds, Mods, Path, Mod, C) ->
    [{Path, Mod, C} |
     lists:flatmap(fun({object, _, {'WORD', _, M}, {'WORD', _, N}, _}) ->
                           P = case Path of "" -> N; _ -> Path ++ "." ++ N end,
                           insts(maps:get(M, Mods), Mods, P, M, C ++ "." ++ N);
                      (_) -> []
                   end, Ds)].

%% A module's fields, numbered from 0 in declaration order, State first.
accessor(M, Ds) ->
    Fs = [{N, case {is_param(D), is_lside(D)} of
                  {true, _} -> param;
                  {_, true} -> local;
                  _ -> state
              end} || D <- [{state} | Ds], N <- [name_of(D)], N =/= undefined],
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
side_ref(M, local, _) -> ["((", M, "_local_t*)x->loc)"];
side_ref(M, param, _) -> ["((", M, "_param_t*)x->par)"].

name_of({state}) -> "State";
name_of({variable, _, {'WORD', _, N}, _, _, _, _}) -> N;
name_of({param, _, {'WORD', _, N}, _, _, _}) -> N;
name_of({local, _, {'WORD', _, N}, _, _, _}) -> N;
name_of({digital, _, {'WORD', _, N}, _, _, _, _}) -> N;
name_of({analog, _, {'WORD', _, N}, _, _, _, _}) -> N;
name_of(_) -> undefined.
