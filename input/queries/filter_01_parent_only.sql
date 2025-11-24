-- filter_01_parent_only.sql
-- Filter on parent table only
-- Expected: Only accounts with balance > 500000 join with transactions
-- Tests: Parent filtering propagates correctly through join

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.balance > 500000;
