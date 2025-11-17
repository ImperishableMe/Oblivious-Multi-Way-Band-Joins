SELECT * FROM account AS a, txn AS t
WHERE a.account_id = t.acc_from;
