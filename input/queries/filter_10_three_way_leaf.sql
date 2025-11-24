-- filter_10_three_way_leaf.sql
-- Three-way join with filter on leaf table
-- Expected: Filter on txn (leaf) only affects transactions
-- Tests: Multi-way join with filter on leaf node

SELECT * FROM account AS a1, account AS a2, txn AS t
WHERE a1.account_id = t.acc_from
  AND a2.account_id = t.acc_to
  AND t.amount < 30000;
