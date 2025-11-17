-- Test query: Triple self-join on account table
-- Finds all combinations where a1.balance < a2.balance < a3.balance
SELECT *
FROM account AS a1, account AS a2, account AS a3
WHERE a1.balance < a2.balance
  AND a2.balance < a3.balance;
