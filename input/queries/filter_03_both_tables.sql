-- filter_03_both_tables.sql
-- Filter on both parent and child tables
-- Expected: High-balance accounts with small transactions only
-- Tests: Multiple filters combine correctly (AND semantics)

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.balance > 500000
  AND t.amount < 50000;
