SELECT *
FROM customer AS c, orders AS o, lineitem AS l
WHERE c.C_CUSTKEY = o.O_CUSTKEY
AND o.O_ORDERKEY = l.L_ORDERKEY;