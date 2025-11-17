SELECT * FROM account, txn
WHERE account.account_id = txn.acc_from;
