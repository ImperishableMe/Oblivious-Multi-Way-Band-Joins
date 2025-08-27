-- Three-table chain band join test (A -> B -> C)
-- Tests multi-level join with accumulated multiplicities
-- 
-- Expected multiplicities:
-- table_a row 1 (value=100): matches b rows 1,2 (95,105) -> mult = 2
--   b row 1 (95): matches c rows 1,2 (93,97) -> mult = 2
--   b row 2 (105): matches c rows 3,4 (103,107) -> mult = 2
--   Total for a row 1: 2 * 2 = 4 (each b match has 2 c matches)
--
-- table_a row 2 (value=200): matches b rows 3,4 (195,205) -> mult = 2
--   b row 3 (195): matches c rows 5,6 (193,197) -> mult = 2
--   b row 4 (205): matches c rows 7,8 (203,207) -> mult = 2
--   Total for a row 2: 2 * 2 = 4

SELECT *
FROM table_a, table_b, table_c
WHERE table_b.b_value >= table_a.a_value - 10
  AND table_b.b_value <= table_a.a_value + 10
  AND table_c.c_value >= table_b.b_value - 5
  AND table_c.c_value <= table_b.b_value + 5;