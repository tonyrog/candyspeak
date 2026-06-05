
// # Add NEXT instruction 

When in reactive mode NEXT will execute
the next rule in the queue. In sequential mode
NEXT will be a NOP.

// FIXME: in reactive mode execute until NEXT!
// FIXME: add timeout to dependency graph!

# Test framework

## Structured language tests

Comprehensive test categories for the language:
- Lexer/parser (syntax, tokens, error handling)
- Expressions (operators, precedence, associativity)
- Types (coercion int/float, overflow, fixpoint precision)
- Functions (args, return values, builtin vs user-defined)
- Rules (conditions, side-effects, timing)
- Edge cases (limits, boundary conditions)

## Regression tests

Test-driven bugfixing workflow:
- Write test that fails (reproduces bug)
- Fix the bug
- Verify test passes
- Test becomes permanent regression guard

Directory: tests/regression/

## Test improvements

// Epsilon comparison for float tests (eps=0.0001)
// Print list formatting: x=1,print(y),z=3 ? cond

## Timer tests with event ordering

Test timer behavior by checking relative order of events, not absolute times.
Example: if a has T=100 and b has T=200, output pattern should be "a a b a a b..."

Implementation:
- Capture print() output during test run
- Verify sequence/pattern matches expected
- Useful for testing timer-in-modules with different timeouts per instance

# Register allocation work

## Pinned registers

Update register allocator to use pinned variables
by allocating load of variables from top register (r7)
down to bottom (r0). Temporary expressions use low registers.
When register area is exhausted, pinned variables are
evicted using LRU and reloaded when needed.

Benefits:
- Reduces LD instructions when same variable used multiple times
- Example: x + x * 2 needs only one LD instead of two

// ## free_reg fixes
// Fixed missing free_reg in csp_parse_rule (cnd, result.reg)
// Fixed missing free_reg in make_can_rule (cr, kr, zr, cnd)

## constant folding

Remove LI instruction that are not needed after constant folding
as an alternative maybe postpone LI generation until value is
actually needed?

// ## supply type information while parsing

deduce types for all registers while parsing expression
the types will be calcuated and values checked (using tables)
This allows to pass types to function calls (as separate array argument)

# Function call work

// ## Supply type code for arguments to calls

Match types during parse of funcation calls
type given like V_NUMBER in function argument mean V_INTEGER or V_FLOAT
while V_VOID would mean no argument and V_ANY mean V_NUMBER or V_STRING

the actual argument codes are compiled into a type code
using 4 bit type info per argument

## remove ARG instruction

Maybe optimise function call by loading arguments into
R0, R1, R2, R3 and return the value in R0, this will
avoid instructon ARG to load values and variables into
registers.

// ## format program as C structs for decl and instruction

Update csp to handle precompiled program in text segment,
the program const array of structs are compiled and linked
a "small" change in csp_rt is need to be able to point
to ram or rom code. this must be done before csp_rt_start.
this could be used to store a "default/fallback" program.
ram could still be used for programs loaded from flash or eeprom.

// ## Compilation of <- rule (DONE)

The reactive assignment rule:

  X <- A+B+C

is compiled to

  X = A+B+C ? changed(A)||changed(B)||changed(C)

With extra condition:

  X <- A+B+C ? D

is compiled to

  X = A+B+C ? (changed(A)||changed(B)||changed(C)) && D

Implementation: csp_parse_rule scans LD instructions after parsing
the rhs expression and generates changed() calls for each variable.

Future optimization: use OP_LDO to inline changed check:

  OP_LDO ri, xi       load dset[xi] and OR into register ri

## Object instantiation with <- and = (DONE)

Extend csp_parse_object to handle init expressions:

  #Add a0 Cin=0 A <- (X&1) B <- (Y&1)
  #Add a1 Cin <- a0.Cout A <- ((X>>1)&1) B <- ((Y>>1)&1)

Syntax: #Module name ( target (= | <-) expr )*

Where:
- target=expr: static initialization (evaluated once)
- target <- expr: reactive coupling (generates rule)

// # Timer in modules

Support timer declarations inside modules:

```
#module Blinker
#timer T 500
#variable Out:1 out
Out <- ~Out ? T
T <- 1 ? T
#end

#Blinker led1
#Blinker led2   // each instance gets own timer
```

Implementation:
- Timer in module creates timer decl marked as module-local
- At instantiation, copy timer slot (px, tx) per object
- Runtime handles timer per object-slot

// # Digital/Analog in modules

Support pin binding at instantiation time:

```
#module Button
#digital In in pullup   // pin unspecified
#end

#Button b1 In=pin(13)   // bind to pin 13
#Button b2 In=pin(14)   // bind to pin 14

// or
#Button b1 In.pin=13   // bind to pin 13
#Button b2 In.pin=14   // bind to pin 14

```

Implementation:
- Digital/analog in module has pin=UNBOUND
- Init expression `field=pin(N)` binds to physical pin
- Conflict detection if same pin used twice
- Alternative: keep digital/analog global, use variables as bridge

# Optimise

On way to optimise and partial evaluate expression is to

1. straight parse/generate expression
2. disassemble the expression 
3. parse/generate again (now all constant expression should be gone)
