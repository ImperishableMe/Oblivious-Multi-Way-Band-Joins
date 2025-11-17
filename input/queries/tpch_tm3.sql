SELECT *
FROM nation AS n, supplier AS s, customer AS c, orders AS o, lineitem AS l
WHERE n.N_NATIONKEY = s.S_NATIONKEY
AND s.S_NATIONKEY = c.C_NATIONKEY
AND c.C_CUSTKEY = o.O_CUSTKEY
AND o.O_ORDERKEY = l.L_ORDERKEY;