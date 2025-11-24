-- filter_05_all_match.sql
-- Filter that matches everything (effectively no filter)
-- Expected: Same result as unfiltered join
-- Tests: Filter that doesn't eliminate any tuples

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.balance > 0;
