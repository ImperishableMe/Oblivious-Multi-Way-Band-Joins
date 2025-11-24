-- filter_11_self_join_one_alias.sql
-- Self-join with filter on one alias only
-- Expected: a1 filtered by owner_id, a2 unfiltered
-- Tests: Filter applies to specific table alias, not all instances

SELECT * FROM account AS a1, account AS a2
WHERE a1.balance < a2.balance
  AND a1.owner_id = 8;
