-- filter_08_on_non_join_column.sql
-- Filter on column not used in join
-- Expected: Filter by owner_id, join by account_id
-- Tests: Filter and join operate on different columns

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.owner_id = 8;
