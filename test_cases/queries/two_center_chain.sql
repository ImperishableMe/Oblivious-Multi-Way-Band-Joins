-- Chain-shaped join with two center nodes
-- Tests multiplicities with a linear chain structure
--
-- Join structure:
--   child1    child3
--      \      /
--     center1---center2
--      /      \
--   child2    child4
--
-- Expected multiplicities:
-- center1 row 1 (value=100):
--   -> child1 rows 1,2 (95,105) [mult=2]
--   -> child2 rows 1,2 (95,105) [mult=2]
--   -> center2 row 1 (150) [mult=1]
--   Total: 2*2*1 = 4
-- center1 row 2 (value=200):
--   -> child1 NONE (95,105 < 190) [mult=0]
--   -> child2 NONE (95,105 < 190) [mult=0]
--   Total: 0
--
-- center2 row 1 (value=150):
--   -> center1 row 1 (100) [mult=1]
--   -> child3 rows 1,2 (145,155) [mult=2]
--   -> child4 rows 1,2 (145,155) [mult=2]
--   Total: 1*2*2 = 4
-- center2 row 2 (value=250):
--   -> center1 NONE (100,200 < 200) [mult=0]
--   Total: 0

SELECT *
FROM center1, center2, child1, child2, child3, child4
WHERE child1.ch1_value >= center1.c1_value - 10
  AND child1.ch1_value <= center1.c1_value + 10
  AND child2.ch2_value >= center1.c1_value - 10
  AND child2.ch2_value <= center1.c1_value + 10
  AND center2.c2_value >= center1.c1_value + 40
  AND center2.c2_value <= center1.c1_value + 60
  AND child3.ch3_value >= center2.c2_value - 10
  AND child3.ch3_value <= center2.c2_value + 10
  AND child4.ch4_value >= center2.c2_value - 10
  AND child4.ch4_value <= center2.c2_value + 10;