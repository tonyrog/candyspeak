
// # Add NEXT instruction 

When in reactive mode NEXT will execute
the next rule in the queue. An in sequential mode
NEXT will be a NOP.

// FIXME: in reactive mode execute until NEXT!
// FIXME: add timeout to dependency graph!

# Register allocation work

Update register allocator to use pinned variables
by allocating load of variables to variables allocated
from top register to bottom. When registers area is exhausted
then pinned variables are evcited (reloads when need)

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
