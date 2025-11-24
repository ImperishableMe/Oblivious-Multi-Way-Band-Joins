-- filter_07_on_join_column.sql
-- Filter on the join column itself
-- Expected: Only specific account_id values participate in join
-- Tests: Filter on same column used for join condition

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.account_id = 50;
