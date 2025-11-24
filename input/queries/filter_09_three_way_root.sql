-- filter_09_three_way_root.sql
-- Three-way join with filter on root table
-- Expected: Filter on a1 (source account) propagates through join tree
-- Tests: Multi-way join with filter on root node

SELECT * FROM account AS a1, account AS a2, txn AS t
WHERE a1.account_id = t.acc_from
  AND a2.account_id = t.acc_to
  AND a1.balance > 500000;
