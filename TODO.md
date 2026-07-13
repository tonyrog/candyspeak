
- Array notation

  #digital D[10] in 0:1..5,7,9,13,15,17
  #variable Acc[10]
  #analog A[5]:10 in 0:1..5
  #variable INDEX
  #timer Td 1000

  Acc[INDEX] <- Acc[INDEX] + A[INDEX % 5]
  INDEX <- (INDEX + 1) % 5 ? timeout(Td)

- Memory

How can we use all available memory to rules and declarations
without affecting overhead?


  
- How to combine ROM + RAM => new ROM base?

- ROM disable flag to kill off REAL firmware.

- Optimse rules. print rule then parse ?

- man borde kunna köra value och pin init i INIT också
  (kanske lägga initiering som kod istf i deklarations ?)
 