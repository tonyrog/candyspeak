%% The actions of candyspeak.y: act(Name, Values) builds what a rule
%% reduces to, from the values of its right hand side: the AST.
-module(candyspeak_parse_act).
-export([act/2]).

act(program, [V1|_]) ->
    lists:reverse(V1);
act(nil, _) ->
    [];
act(file_stmt, [V1, V2|_]) ->
    [V2|V1];
act(pass1, [V1|_]) ->
    V1;
act(pass2, [_, V2|_]) ->
    V2;
act(immediate, [_, V2|_]) ->
    {immediate, V2};
act(module, [V1, V2|_]) ->
    {module,line(V1),V2};
act(states, [V1, V2|_]) ->
    {states,line(V1),V2};
act(in_states, [V1, V2|_]) ->
    {'in',line(V1),V2};
act('when', [V1, V2|_]) ->
    {'when',line(V1),V2};
act('else', [V1|_]) ->
    {'else',line(V1)};
act(digital, [V1, V2, V3, V4, V5|_]) ->
    {digital,line(V1),V2,V3,V4,V5,[]};
act(digital_pins, [V1, V2, V3, V4, V5, V6|_]) ->
    {digital,line(V1),V2,V3,V4,V5,V6};
act(analog_pins, [V1, V2, V3, V4, V5, V6|_]) ->
    {analog,line(V1),V2,V3,V4,V5,V6};
act(analog, [V1, V2, V3, V4, V5|_]) ->
    {analog,line(V1),V2,V3,V4,V5,[]};
act(variable, [V1, V2, V3, V4, V5|_]) ->
    {variable,line(V1),V2,V3,V4,V5,undefined};
act(variable_init, [V1, V2, V3, V4, V5, _, V7|_]) ->
    {variable,line(V1),V2,V3,V4,V5,V7};
act(constant, [V1, V2, V3, V4, V5, _, V7|_]) ->
    {constant,line(V1),V2,V3,V4,V5,V7};
act(local_init, [V1, V2, V3, V4, _, V6|_]) ->
    {local,line(V1),V2,V3,V4,V6};
act(local, [V1, V2, V3, V4|_]) ->
    {local,line(V1),V2,V3,V4,undefined};
act(param, [V1, V2, V3, V4|_]) ->
    {param,line(V1),V2,V3,V4,undefined};
act(param_init, [V1, V2, V3, V4, _, V6|_]) ->
    {param,line(V1),V2,V3,V4,V6};
act(timer, [V1, V2, V3|_]) ->
    {timer,line(V1),V2,V3,undefined};
act(timer_init, [V1, V2, V3, _, V5|_]) ->
    {timer,line(V1),V2,V3,V5};
act(field, [V1, V2, V3, V4, V5, _, V7|_]) ->
    {field,line(V1),V2,V3,V4,V5,V7};
act(buffer, [V1, V2, V3, V4, V5|_]) ->
    {buffer,line(V1),V2,V3,V4,V5};
act(define, [V1, V2, V3|_]) ->
    {define,line(V1),V2,V3};
act(import_name, [V1, V2|_]) ->
    {import,line(V1),name,element(3,V2)};
act(import_root, [V1, V2, V3|_]) ->
    {import,line(V1),{root,element(3,V2)},element(3,V3)};
act(import_quoted, [V1, V2|_]) ->
    {import,line(V1),quoted,element(3,V2)};
act(annotate, [V1, V2, V3, V4|_]) ->
    {annotate,line(V1),V2,V3,V4};
act(annotate_all, [V1, V2, V3, V4|_]) ->
    {annotate,line(V1),V2,{'WORD',line(V3),"*"},V4};
act(word, [V1|_]) ->
    make_word(V1);
act(cons12, [V1, V2|_]) ->
    [V1|V2];
act(key_value, [V1, _, V3|_]) ->
    {V1,V3};
act(key, [V1|_]) ->
    {V1,true};
act(neg_int, [_, V2|_]) ->
    {'INT',line(V2),"-" ++ element(3,V2)};
act(neg_flt, [_, V2|_]) ->
    {'FLT',line(V2),"-" ++ element(3,V2)};
act('end', [V1|_]) ->
    {'end',line(V1)};
act(disable, [V1, V2|_]) ->
    {disable,line(V1),V2};
act(enable, [V1, V2|_]) ->
    {enable,line(V1),V2};
act(object, [V1, V2, V3|_]) ->
    object(V1,V2,V3);
act(default, _) ->
    default;
act(array_size, [V1, V2|_]) ->
    {array_size,line(V1),V2};
act(scalar, _) ->
    scalar;
act(bit_range, [V1, V2, V3|_]) ->
    {range,line(V2),V1,V3};
act(list1, [V1|_]) ->
    [V1];
act(rule_range, [V1, _, V3|_]) ->
    {V1, V3};
act(pin, [V1|_]) ->
    {pin,line(V1),V1};
act(port_pin, [V1, _, V3|_]) ->
    {port_pin,line(V1),V1,V3};
act(port_pin_range, [V1, _, V3, V4, V5|_]) ->
    {port_pin,line(V1),V1, {range,line(V4),V3,V5}};
act(pin_range, [V1, V2, V3|_]) ->
    {pin,line(V1),{range,line(V2),V1,V3}};
act(pin_to, [V1, V2|_]) ->
    {pin,line(V1),{range_to,line(V1),V2}};
act(port, [V1|_]) ->
    {port,line(V1),V1};
act(can, [_, V2|_]) ->
    [{can,V2}];
act(can_name, [_, V2|_]) ->
    [{can,{field,line(V2),V2}}];
act(i2c, [_, V2, V3, V4|_]) ->
    [{i2c,V2,V3,V4}];
act(spi, [_, V2, V3, _, V5, V6|_]) ->
    [{spi,V2,{V3,V5},V6}];
act(udp, [_, V2|_]) ->
    [{udp,V2,undefined}];
act(udp_ip, [_, V2, V3|_]) ->
    [{udp,V2,V3}];
act(udp_name, [_, V2|_]) ->
    [{udp,{field,line(V2),V2},undefined}];
act(udp_name_ip, [_, V2, V3|_]) ->
    [{udp,{field,line(V2),V2},V3}];
act(udp_ip_name, [_, V2, V3|_]) ->
    [{udp,V2,{field,line(V3),V3}}];
act(udp_name_name, [_, V2, V3|_]) ->
    [{udp,{field,line(V2),V2},{field,line(V3),V3}}];
act(tcp, [_, V2|_]) ->
    [{tcp,V2,undefined}];
act(tcp_ip, [_, V2, V3|_]) ->
    [{tcp,V2,V3}];
act(tcp_name, [_, V2|_]) ->
    [{tcp,{field,line(V2),V2},undefined}];
act(tcp_name_ip, [_, V2, V3|_]) ->
    [{tcp,{field,line(V2),V2},V3}];
act(tcp_ip_name, [_, V2, V3|_]) ->
    [{tcp,V2,{field,line(V3),V3}}];
act(tcp_name_name, [_, V2, V3|_]) ->
    [{tcp,{field,line(V2),V2},{field,line(V3),V3}}];
act(uart, [_, V2|_]) ->
    [{uart,V2,9600}];
act(uart_baud, [_, V2, V3|_]) ->
    [{uart,V2,V3}];
act(console, _) ->
    [{console}];
act(repl, _) ->
    [{repl}];
act(sms, [_, V2|_]) ->
    [{sms,V2}];
act(sms_pin, [_, V2, V3|_]) ->
    [{sms,V2,V3}];
act(onewire, [_, V2, _, V4, V5|_]) ->
    [{onewire,{V2,V4},V5}];
act(opt_dir, [V1|_]) ->
    {dir,V1};
act(opt_endian, [V1|_]) ->
    {endian,V1};
act(opt_type, [V1|_]) ->
    {type, V1};
act(opt_pull, [V1|_]) ->
    {pull, V1};
act(opt_trig, [V1|_]) ->
    {trig, V1};
act(pwm, _) ->
    pwm;
act(invert, _) ->
    invert;
act(opt_bind, [_, V2, _, V4|_]) ->
    {bind,{V2,V4}};
act(low, _) ->
    low;
act(high, _) ->
    high;
act(both, _) ->
    both;
act(soft, _) ->
    soft;
act(ready, _) ->
    ready;
act(rising, _) ->
    rising;
act(falling, _) ->
    falling;
act(pullup, _) ->
    pullup;
act(pulldown, _) ->
    pulldown;
act(big, _) ->
    big;
act(little, _) ->
    little;
act(native, _) ->
    native;
act(in, _) ->
    in;
act(out, _) ->
    out;
act(inout, _) ->
    inout;
act(unsigned, _) ->
    unsigned;
act(integer, _) ->
    integer;
act(float, _) ->
    float;
act(string, _) ->
    string;
act(rule_guarded, [V1, V2, V3|_]) ->
    {rule,line(V2),V1,V3};
act(rule, [V1|_]) ->
    {rule,line(V1),V1,undefined};
act(pack, [V1, V2, V3|_]) ->
    {rule, line(V2), [{pack, V1, V3}], true};
act(unpack, [V1, V2, V3|_]) ->
    {rule, line(V2), [{unpack, V1, V3}], true};
act(pack_guarded, [V1, V2, V3, _, V5|_]) ->
    {rule, line(V2),[{pack,V1,V3}],V5};
act(unpack_guarded, [V1, V2, V3, _, V5|_]) ->
    {rule, line(V2),[{unpack,V1,V3}],V5};
act(assign, [V1, V2, V3|_]) ->
    {'=', line(V2), V1, V3};
act(assign_none, [V1|_]) ->
    V1;
act(assign_prev, [V1, V2, V3|_]) ->
    {'<-',line(V2), V1, V3};
act(cons13, [V1, _, V3|_]) ->
    [V1|V3];
act(pack_one, [V1|_]) ->
    [{V1,default}];
act(pack_one_bits, [V1, _, V3|_]) ->
    [{V1,V3}];
act(pack_more, [V1, V2|_]) ->
    [{V1,default}|V2];
act(pack_expr_bits, [_, V2, _, _, V5|_]) ->
    [{V2,V5}];
act(pack_more_bits, [V1, _, V3, V4|_]) ->
    [{V1,V3}|V4];
act(pack_more_expr_bits, [_, V2, _, _, V5, V6|_]) ->
    [{V2,V5}|V6];
act(ref, [V1|_]) ->
    {field,line(V1),V1};
act(ref_part, [V1, V2, V3|_]) ->
    {field,line(V1),{part,line(V2),V1,V3}};
act(ref_member, [V1, V2, V3|_]) ->
    {field,line(V1),{fld,line(V2),V1,V3}};
act(ref_member_part, [V1, V2, V3, V4, V5|_]) ->
    {field,line(V1),{part,line(V2),{fld,line(V4),V1,V3},V5}};
act(ref_index, [V1, V2, V3|_]) ->
    {field,line(V1),{index, line(V2), V1,V3}};
act(ref_member_index, [V1, V2, V3, V4, V5|_]) ->
    {field,line(V1),{index,line(V4),{fld,line(V2),V1,V3},V5}};
act(ref_slice, [V1, V2, V3, V4, V5|_]) ->
    {field,line(V1),{index,line(V2),V1, {range,line(V4),V3,V5}}};
act(hole, [V1|_]) ->
    {hole,line(V1)};
act(neg, [V1, V2|_]) ->
    {'-',line(V1),V2};
act('bnot', [V1, V2|_]) ->
    {'~',line(V1),V2};
act('not', [V1, V2|_]) ->
    case V2 of {'!', _Ln, Cond} -> Cond; Cond -> {'!',line(V1),Cond} end;
act(add, [V1, V2, V3|_]) ->
    {'+',line(V2),V1,V3};
act(sub, [V1, V2, V3|_]) ->
    {'-',line(V2),V1,V3};
act(mul, [V1, V2, V3|_]) ->
    {'*',line(V2),V1,V3};
act('div', [V1, V2, V3|_]) ->
    {'/',line(V2),V1,V3};
act('rem', [V1, V2, V3|_]) ->
    {'%',line(V2),V1,V3};
act('band', [V1, V2, V3|_]) ->
    {'&',line(V2),V1,V3};
act('bor', [V1, V2, V3|_]) ->
    {'|',line(V2),V1,V3};
act('bxor', [V1, V2, V3|_]) ->
    {'^',line(V2),V1,V3};
act(shl, [V1, V2, V3|_]) ->
    {'<<',line(V2),V1,V3};
act(shr, [V1, V2, V3|_]) ->
    {'>>',line(V2),V1,V3};
act(eq, [V1, V2, V3|_]) ->
    {'==',line(V2),V1,V3};
act(ne, [V1, V2, V3|_]) ->
    {'!=',line(V2),V1,V3};
act(le, [V1, V2, V3|_]) ->
    {'<=',line(V2),V1,V3};
act(lt, [V1, V2, V3|_]) ->
    {'<',line(V2),V1,V3};
act(gt, [V1, V2, V3|_]) ->
    {'>',line(V2),V1,V3};
act(ge, [V1, V2, V3|_]) ->
    {'>=',line(V2),V1,V3};
act('and', [V1, V2, V3|_]) ->
    {'&&',line(V2),V1,V3};
act('or', [V1, V2, V3|_]) ->
    {'||',line(V2),V1,V3};
act(call0, [V1, V2|_]) ->
    {call, line(V2), V1, []};
act(call, [V1, V2, V3|_]) ->
    {call, line(V2), V1, V3};
act(call_edge, [V1, V2, V3|_]) ->
    {call, line(V2), make_word(V1), V3};
act(array, [V1, V2|_]) ->
    {array,line(V1),V2};
act(Name, _Vs) ->
    erlang:error({no_action, Name}).


object({'WORD',L,"route"}, Src={'WORD',_,_}, [{field,_,Dst={'WORD',_,_}}]) ->
    {route,L,Src,Dst};
object(M, O, Ps) ->
    {object,line(M),M,O,Ps}.

line([H|_]) -> line(H);
line(T) when is_tuple(T) -> element(2, T).

%% A keyword token is named as it is written.
make_word({Atom,Ln}) when is_atom(Atom), is_integer(Ln) ->
    {'WORD', Ln, atom_to_list(Atom)}.

