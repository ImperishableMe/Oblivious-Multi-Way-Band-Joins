-- filter_04_no_match.sql
-- Filter that matches nothing
-- Expected: Empty result (only sentinel row if applicable)
-- Tests: Edge case where filter eliminates all tuples

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.balance > 99999999;
