SELECT *
FROM supplier1, supplier2
WHERE supplier2.S2_S_ACCTBAL >= supplier1.S1_S_ACCTBAL - 100
  AND supplier2.S2_S_ACCTBAL <= supplier1.S1_S_ACCTBAL + 1000;