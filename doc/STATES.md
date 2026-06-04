# States

enumerate the states used as

#states A B C D

The Init state is implictit and can be used to initialize
various variables and output values.

can be thought of as have a variable

#variable State = Init

#in Init
    X = 1000
#end

Adding rules to states is done like

#in A
    X = 1 ? Y < Z           // Stay in A
    Y = 1, State=B ? X > Z  // transition to B
#end

#in B
    Z = Z + 1, State=A
#end

this can be thought of as a nice way of writing

X=1 ? Y < Z && (State == A)
Y=1, State=B ? X > Z && (State == A)
Z=Z+1, State=A ? (State == B)

Rules may be added to a state as much as needed
and new states may be add by listing more states
with
#states E F G

In a module local states may be defined

#module Local
#states a b c d
#analog Foo:10
#variable Bar

#in Init
    Bar = 0
#end

#in a
    Bar=1,State = b ? Foo<10
#end

#in b
    Bar=2, State=c ? Foo<100
#end

#in c
    Bar=3, State=b ? Foo<1000, 
    Bar=4, State=a ? Foo<100
#end

#end  // Local

#Local obj1 Foo[pin]=3
#Local obj2 Foo[pin]=4
