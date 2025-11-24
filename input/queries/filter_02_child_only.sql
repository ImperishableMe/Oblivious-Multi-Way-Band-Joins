-- filter_02_child_only.sql
-- Filter on child table only
-- Expected: Only transactions with amount < 50000 appear
-- Tests: Child filtering works independently

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND t.amount < 50000;
