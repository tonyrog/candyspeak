
- Array notation

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

- Memory

How can we use all available memory to rules and declarations
without affecting overhead?

- Interrupt

Can we run CandySpeek rules during interrupt,
is it possibel / feasible. At least have rules
that trigger on digital state change, analog sample compleation...

- How to combine ROM + RAM => new ROM base?

- ROM disable flag to kill off REAL firmware.

- Optimse rules. print rule then parse ?

- man borde kunna köra value och pin init i INIT också
  (kanske lägga initiering som kod istf i deklarations ?)
 