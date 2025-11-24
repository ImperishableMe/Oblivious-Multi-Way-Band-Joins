-- filter_06_single_match.sql
-- Filter that matches exactly one parent tuple
-- Expected: Only transactions from account_id = 1
-- Tests: Minimal filter result

SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from
  AND a.account_id = 1;
