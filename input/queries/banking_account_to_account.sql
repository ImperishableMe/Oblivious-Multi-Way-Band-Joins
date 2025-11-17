SELECT * FROM account AS a1, account AS a2, txn AS t
WHERE a1.account_id = t.acc_from
  AND a2.account_id = t.acc_to;
