-- Test query: Triple self-join on supplier table
-- Finds all combinations where s1.S_ACCTBAL < s2.S_ACCTBAL < s3.S_ACCTBAL
SELECT *
FROM supplier AS s1, supplier AS s2, supplier AS s3
WHERE s1.S_ACCTBAL < s2.S_ACCTBAL
  AND s2.S_ACCTBAL < s3.S_ACCTBAL;
