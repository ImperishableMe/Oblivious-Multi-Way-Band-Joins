-- Two-table band join test
-- Tests basic local multiplicity computation
--
-- Expected multiplicities:
-- left_table row 1 (value=100): matches right rows 1,2 (95,105) -> mult = 2
-- left_table row 2 (value=200): matches right rows 3,4 (195,205) -> mult = 2
-- right_table row 1 (value=95): matches left row 1 (100) -> mult = 1
-- right_table row 2 (value=105): matches left row 1 (100) -> mult = 1
-- right_table row 3 (value=195): matches left row 2 (200) -> mult = 1
-- right_table row 4 (value=205): matches left row 2 (200) -> mult = 1

SELECT *
FROM left_table, right_table
WHERE right_table.r_value >= left_table.l_value - 10
  AND right_table.r_value <= left_table.l_value + 10;