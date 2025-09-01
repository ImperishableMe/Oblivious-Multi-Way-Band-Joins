#!/usr/bin/env python3
"""
Generate pairs of test datasets with different data distributions but identical join output sizes.
Uses negative values for padding to ensure clean separation from regular data.
"""

import os
import sys
import sqlite3
import pandas as pd
import numpy as np
from pathlib import Path
import random

class TestDatasetGenerator:
    def __init__(self, base_path="/home/r33wei/omwj/memory_const/test_access_patterns"):
        self.base_path = Path(base_path)
        self.queries_path = self.base_path / "queries"
        
        # Value ranges
        self.MATCHING_RANGE = (1000, 1000000)  # Positive values for normal data
        self.PADDING_RANGE = (-20000, -1)      # Negative values for padding
        self.MAX_PADDING_RATIO = 0.33          # Max 1/3 of initial output size
        
        # Padding counters
        self.padding_counter = -1
        
        # Target sizes (similar to data_0_001)
        self.target_sizes = {
            'tb1': {'supplier1': 150, 'supplier2': 150},
            'tb2': {'part1': 150, 'part2': 150},
            'tm1': {'customer': 150, 'orders': 600, 'lineitem': 2400}
        }
    
    def generate_all_datasets(self):
        """Generate dataset pairs for all queries"""
        for query_name in ['tb1', 'tb2', 'tm1', 'tm2', 'tm3']:
            print(f"\n{'='*60}")
            print(f"Generating datasets for {query_name.upper()}")
            print('='*60)
            
            try:
                # Generate and balance datasets
                dataset_a, dataset_b = self.generate_and_balance_datasets(query_name)
                
                # Save datasets
                self.save_dataset(dataset_a, f"dataset_A/{query_name}")
                self.save_dataset(dataset_b, f"dataset_B/{query_name}")
                print(f"✓ Datasets saved successfully")
                
            except Exception as e:
                print(f"✗ Error generating {query_name}: {e}")
                raise
    
    def generate_and_balance_datasets(self, query_name):
        """Generate initial datasets and balance them with padding"""
        
        # Step 1: Generate initial datasets with positive values only
        print(f"\n1. Generating initial datasets...")
        dataset_a = self.generate_initial_dataset(query_name, 'uniform')
        dataset_b = self.generate_initial_dataset(query_name, 'skewed')
        
        # Step 2: Measure output sizes
        print(f"\n2. Measuring output sizes with SQLite...")
        size_a = self.run_sqlite_count(dataset_a, query_name)
        size_b = self.run_sqlite_count(dataset_b, query_name)
        print(f"   Dataset A (uniform): {size_a} rows")
        print(f"   Dataset B (skewed):  {size_b} rows")
        
        if size_a == size_b:
            print(f"\n✓ Datasets already balanced!")
            return dataset_a, dataset_b
        
        # Step 3: Check padding constraint
        diff = abs(size_a - size_b)
        min_size = min(size_a, size_b)
        max_padding = int(min_size * self.MAX_PADDING_RATIO)
        
        print(f"\n3. Checking padding constraint...")
        print(f"   Difference: {diff} rows")
        print(f"   Max allowed padding: {max_padding} rows (33% of {min_size})")
        
        if diff > max_padding:
            print(f"   ⚠ Warning: Difference exceeds max padding, will try to adjust")
            # Could implement regeneration logic here if needed
        
        # Step 4: Apply padding based on query type
        print(f"\n4. Applying padding to balance datasets...")
        
        if query_name in ['tb1', 'tb2']:
            # Both are inequality joins
            dataset_a, dataset_b = self.pad_inequality_join(
                dataset_a, dataset_b, query_name, size_a, size_b
            )
        elif query_name in ['tm1', 'tm3']:
            # Multi-way equality joins
            dataset_a, dataset_b = self.pad_equality_join(
                dataset_a, dataset_b, query_name, size_a, size_b
            )
        else:  # tm2
            # Multi-way with inequality component
            dataset_a, dataset_b = self.pad_mixed_join(
                dataset_a, dataset_b, query_name, size_a, size_b
            )
        
        # Step 5: Verify exact match
        print(f"\n5. Verifying balanced output...")
        final_a = self.run_sqlite_count(dataset_a, query_name)
        final_b = self.run_sqlite_count(dataset_b, query_name)
        
        print(f"   Final dataset A: {final_a} rows")
        print(f"   Final dataset B: {final_b} rows")
        
        if final_a != final_b:
            raise ValueError(f"Failed to balance: {final_a} != {final_b}")
        
        print(f"\n✓ Successfully balanced at {final_a} output rows")
        return dataset_a, dataset_b
    
    def generate_initial_dataset(self, query_name, distribution):
        """Generate initial dataset with specified distribution"""
        
        if query_name == 'tb1':
            return self.generate_tb1_dataset(distribution)
        elif query_name == 'tb2':
            return self.generate_tb2_dataset(distribution)
        elif query_name == 'tm1':
            return self.generate_tm_dataset(query_name, distribution)
        elif query_name == 'tm2':
            return self.generate_tm2_dataset(distribution)
        elif query_name == 'tm3':
            return self.generate_tm3_dataset(distribution)
        else:
            raise ValueError(f"Unknown query: {query_name}")
    
    def generate_tb1_dataset(self, distribution):
        """Generate supplier1 and supplier2 for TB1 (inequality join)"""
        num_rows = self.target_sizes['tb1']['supplier1']
        
        if distribution == 'uniform':
            # Uniform distribution of values
            # supplier1 (left of <): larger range to reduce matches
            s1_acctbal = []
            for i in range(num_rows):
                # Spread values across 80-220 range
                s1_acctbal.append(80 + int(i * 140 / num_rows))
            
            # Add some randomness by shuffling
            random.shuffle(s1_acctbal)
            
            # supplier2 (right of <): smaller range
            s2_acctbal = []
            for i in range(num_rows):
                # Spread values across 10-130 range
                s2_acctbal.append(10 + int(i * 120 / num_rows))
            
            # Shuffle for randomness
            random.shuffle(s2_acctbal)
                
        else:  # skewed
            # Skewed distribution but with same range strategy
            # supplier1: bell curve-like distribution
            s1_acctbal = []
            for i in range(num_rows):
                if i < num_rows // 5:
                    # 20% have low values (80-110)
                    s1_acctbal.append(80 + (i * 3) % 30)
                elif i < 4 * num_rows // 5:
                    # 60% have mid values (110-170)
                    s1_acctbal.append(110 + (i * 2) % 60)
                else:
                    # 20% have high values (170-220)
                    s1_acctbal.append(170 + (i * 4) % 50)
            
            # Shuffle to mix up the ordering
            random.shuffle(s1_acctbal)
            
            # supplier2: heavily skewed to low values
            s2_acctbal = []
            for i in range(num_rows):
                if i < 3 * num_rows // 4:
                    # 75% have low values (10-70)
                    s2_acctbal.append(10 + (i * 2) % 60)
                else:
                    # 25% have higher values (70-130)
                    s2_acctbal.append(70 + (i * 3) % 60)
            
            random.shuffle(s2_acctbal)
        
        supplier1 = pd.DataFrame({
            'S1_S_SUPPKEY': range(1, num_rows + 1),
            'S1_S_NAME': [str(i) for i in range(1, num_rows + 1)],
            'S1_S_ADDRESS': [str(i) for i in range(1, num_rows + 1)],
            'S1_S_NATIONKEY': [i % 25 for i in range(1, num_rows + 1)],
            'S1_S_PHONE': [str(i) for i in range(1, num_rows + 1)],
            'S1_S_ACCTBAL': s1_acctbal[:num_rows],
            'S1_S_COMMENT': [str(i) for i in range(1, num_rows + 1)]
        })
        
        supplier2 = pd.DataFrame({
            'S2_S_SUPPKEY': range(1, num_rows + 1),
            'S2_S_NAME': [str(i) for i in range(1, num_rows + 1)],
            'S2_S_ADDRESS': [str(i) for i in range(1, num_rows + 1)],
            'S2_S_NATIONKEY': [i % 25 for i in range(1, num_rows + 1)],
            'S2_S_PHONE': [str(i) for i in range(1, num_rows + 1)],
            'S2_S_ACCTBAL': s2_acctbal[:num_rows],
            'S2_S_COMMENT': [str(i) for i in range(1, num_rows + 1)]
        })
        
        return {'supplier1': supplier1, 'supplier2': supplier2}
    
    def generate_tb2_dataset(self, distribution):
        """Generate part1 and part2 for TB2 (inequality join)"""
        num_rows = self.target_sizes['tb2']['part1']
        
        if distribution == 'uniform':
            # Uniform distribution but with different ranges than TB1
            # part1 (left of <): larger range to reduce matches, but shifted
            # Using 100-250 range instead of 80-200
            p1_price = []
            for i in range(num_rows):
                p1_price.append(100 + int(i * 150 / num_rows))  # Spread across 100-250
            
            # Shuffle to avoid perfect ordering
            random.shuffle(p1_price)
            
            # part2 (right of <): smaller range, also shifted
            # Using 20-150 range instead of 0-120
            p2_price = []
            for i in range(num_rows):
                p2_price.append(20 + int(i * 130 / num_rows))  # Spread across 20-150
            
            # Shuffle this too
            random.shuffle(p2_price)
                
        else:  # skewed
            # Different skew pattern than TB1
            # part1: use exponential-like distribution
            p1_price = []
            for i in range(num_rows):
                if i < num_rows // 4:
                    # 25% have low values (100-130)
                    p1_price.append(100 + (i * 2) % 30)
                elif i < 3 * num_rows // 4:
                    # 50% have mid values (130-180)
                    p1_price.append(130 + (i * 3) % 50)
                else:
                    # 25% have high values (180-250)
                    p1_price.append(180 + (i * 5) % 70)
            
            # Shuffle to mix the distribution
            random.shuffle(p1_price)
            
            # part2: inverse distribution
            p2_price = []
            for i in range(num_rows):
                if i < num_rows // 2:
                    # 50% have very low values (20-60)
                    p2_price.append(20 + (i * 2) % 40)
                elif i < 3 * num_rows // 4:
                    # 25% have mid values (60-100)
                    p2_price.append(60 + (i * 3) % 40)
                else:
                    # 25% have higher values (100-150)
                    p2_price.append(100 + (i * 4) % 50)
            
            random.shuffle(p2_price)
        
        part1 = pd.DataFrame({
            'P1_P_PARTKEY': range(1, num_rows + 1),
            'P1_P_NAME': [str(i) for i in range(1, num_rows + 1)],
            'P1_P_MFGR': [str(i % 5) for i in range(1, num_rows + 1)],
            'P1_P_BRAND': [str(i % 10) for i in range(1, num_rows + 1)],
            'P1_P_TYPE': [str(i % 25) for i in range(1, num_rows + 1)],
            'P1_P_SIZE': [i % 50 for i in range(1, num_rows + 1)],
            'P1_P_CONTAINER': [str(i % 7) for i in range(1, num_rows + 1)],
            'P1_P_RETAILPRICE': p1_price[:num_rows],
            'P1_P_COMMENT': [str(i) for i in range(1, num_rows + 1)]
        })
        
        part2 = pd.DataFrame({
            'P2_P_PARTKEY': range(1, num_rows + 1),
            'P2_P_NAME': [str(i) for i in range(1, num_rows + 1)],
            'P2_P_MFGR': [str(i % 5) for i in range(1, num_rows + 1)],
            'P2_P_BRAND': [str(i % 10) for i in range(1, num_rows + 1)],
            'P2_P_TYPE': [str(i % 25) for i in range(1, num_rows + 1)],
            'P2_P_SIZE': [i % 50 for i in range(1, num_rows + 1)],
            'P2_P_CONTAINER': [str(i % 7) for i in range(1, num_rows + 1)],
            'P2_P_RETAILPRICE': p2_price[:num_rows],
            'P2_P_COMMENT': [str(i) for i in range(1, num_rows + 1)]
        })
        
        return {'part1': part1, 'part2': part2}
    
    def generate_tm_dataset(self, query_name, distribution):
        """Generate customer, orders, lineitem for TM queries"""
        num_customers = self.target_sizes[query_name]['customer']
        num_orders = self.target_sizes[query_name]['orders']
        num_lineitems = self.target_sizes[query_name]['lineitem']
        
        # Generate customers
        customers = pd.DataFrame({
            'C_CUSTKEY': range(1, num_customers + 1),
            'C_NAME': [str(i) for i in range(1, num_customers + 1)],
            'C_ADDRESS': [str(i) for i in range(1, num_customers + 1)],
            'C_NATIONKEY': [i % 25 for i in range(1, num_customers + 1)],
            'C_PHONE': [str(i) for i in range(1, num_customers + 1)],
            'C_ACCTBAL': [1000 + i*10 for i in range(num_customers)],
            'C_MKTSEGMENT': [str(i % 5) for i in range(num_customers)],
            'C_COMMENT': [str(i) for i in range(1, num_customers + 1)]
        })
        
        # Generate orders with controlled multiplicities
        if distribution == 'uniform':
            # Each customer has exactly 4 orders
            orders_per_customer = [4] * num_customers
        else:  # skewed
            # Variable distribution: 50 with 2, 50 with 4, 50 with 6
            orders_per_customer = [2]*50 + [4]*50 + [6]*50
        
        # Create orders
        orders_data = []
        order_id = 1
        for cust_id, num_orders_for_cust in zip(range(1, num_customers + 1), orders_per_customer):
            for _ in range(num_orders_for_cust):
                orders_data.append({
                    'O_ORDERKEY': order_id,
                    'O_CUSTKEY': cust_id,
                    'O_ORDERSTATUS': 'O',
                    'O_TOTALPRICE': 10000 + order_id * 100,
                    'O_ORDERDATE': '1995-01-01',
                    'O_ORDERPRIORITY': '1-URGENT',
                    'O_CLERK': str(order_id % 100),
                    'O_SHIPPRIORITY': 0,
                    'O_COMMENT': str(order_id)
                })
                order_id += 1
        
        orders = pd.DataFrame(orders_data)
        
        # Generate lineitems
        if distribution == 'uniform':
            # Each order has exactly 4 lineitems
            items_per_order = [4] * len(orders)
        else:  # skewed
            # Variable: cycle through 2, 4, 6 items per order
            items_per_order = []
            for i in range(len(orders)):
                items_per_order.append([2, 4, 6][i % 3])
            
            # Adjust to ensure exactly num_lineitems total
            current_total = sum(items_per_order)
            if current_total != num_lineitems:
                diff = num_lineitems - current_total
                items_per_order[-1] += diff
        
        # Create lineitems
        lineitem_data = []
        for order_idx, (_, order_row) in enumerate(orders.iterrows()):
            order_key = order_row['O_ORDERKEY']
            num_items = items_per_order[order_idx]
            
            for line_num in range(1, num_items + 1):
                lineitem_data.append({
                    'L_ORDERKEY': order_key,
                    'L_PARTKEY': 1000 + (order_key * 10 + line_num) % 1000,
                    'L_SUPPKEY': 1 + (order_key + line_num) % 10,
                    'L_LINENUMBER': line_num,
                    'L_QUANTITY': 10 + line_num,
                    'L_EXTENDEDPRICE': 1000.00 + order_key + line_num,
                    'L_DISCOUNT': 0.05,
                    'L_TAX': 0.08,
                    'L_RETURNFLAG': 'N',
                    'L_LINESTATUS': 'O',
                    'L_SHIPDATE': '1995-01-02',
                    'L_COMMITDATE': '1995-01-03',
                    'L_RECEIPTDATE': '1995-01-04',
                    'L_SHIPINSTRUCT': 'DELIVER IN PERSON',
                    'L_SHIPMODE': 'TRUCK',
                    'L_COMMENT': str(line_num)
                })
        
        lineitem = pd.DataFrame(lineitem_data)
        
        return {'customer': customers, 'orders': orders, 'lineitem': lineitem}
    
    def generate_tm2_dataset(self, distribution):
        """Generate supplier, customer, nation1, nation2 for TM2"""
        # Generate supplier table
        num_suppliers = 50
        suppliers = pd.DataFrame({
            'S_SUPPKEY': range(1, num_suppliers + 1),
            'S_NAME': [str(i) for i in range(1, num_suppliers + 1)],
            'S_ADDRESS': [str(i) for i in range(1, num_suppliers + 1)],
            'S_NATIONKEY': [(i % 25) + 1 for i in range(num_suppliers)],  # Nations 1-25
            'S_PHONE': [str(i) for i in range(1, num_suppliers + 1)],
            'S_ACCTBAL': [100 + i for i in range(num_suppliers)],
            'S_COMMENT': [str(i) for i in range(1, num_suppliers + 1)]
        })
        
        # Generate customer table
        num_customers = 150
        customers = pd.DataFrame({
            'C_CUSTKEY': range(1, num_customers + 1),
            'C_NAME': [str(i) for i in range(1, num_customers + 1)],
            'C_ADDRESS': [str(i) for i in range(1, num_customers + 1)],
            'C_NATIONKEY': [(i % 25) + 1 for i in range(num_customers)],  # Nations 1-25
            'C_PHONE': [str(i) for i in range(1, num_customers + 1)],
            'C_ACCTBAL': [200 + i for i in range(num_customers)],
            'C_MKTSEGMENT': [str(i % 5) for i in range(num_customers)],
            'C_COMMENT': [str(i) for i in range(1, num_customers + 1)]
        })
        
        # Generate nation tables (nation1 and nation2 are same structure)
        nation1_data = []
        nation2_data = []
        
        for i in range(1, 26):  # 25 nations
            nation1_data.append({
                'N1_N_NATIONKEY': i,
                'N1_N_NAME': str(i),
                'N1_N_REGIONKEY': ((i - 1) % 5) + 1,  # Regions 1-5
                'N1_N_COMMENT': str(i)
            })
            
            nation2_data.append({
                'N2_N_NATIONKEY': i,
                'N2_N_NAME': str(i),
                'N2_N_REGIONKEY': ((i - 1) % 5) + 1,  # Regions 1-5
                'N2_N_COMMENT': str(i)
            })
        
        return {
            'supplier': suppliers,
            'customer': customers,
            'nation1': pd.DataFrame(nation1_data),
            'nation2': pd.DataFrame(nation2_data)
        }
    
    def generate_tm3_dataset(self, distribution):
        """Generate nation, supplier, customer, orders, lineitem for TM3"""
        # Generate nation table
        nation_data = []
        for i in range(1, 26):  # 25 nations
            nation_data.append({
                'N_NATIONKEY': i,
                'N_NAME': str(i),
                'N_REGIONKEY': ((i - 1) % 5) + 1,
                'N_COMMENT': str(i)
            })
        
        # Generate supplier table
        num_suppliers = 50
        suppliers = pd.DataFrame({
            'S_SUPPKEY': range(1, num_suppliers + 1),
            'S_NAME': [str(i) for i in range(1, num_suppliers + 1)],
            'S_ADDRESS': [str(i) for i in range(1, num_suppliers + 1)],
            'S_NATIONKEY': [(i % 25) + 1 for i in range(num_suppliers)],
            'S_PHONE': [str(i) for i in range(1, num_suppliers + 1)],
            'S_ACCTBAL': [100 + i for i in range(num_suppliers)],
            'S_COMMENT': [str(i) for i in range(1, num_suppliers + 1)]
        })
        
        # Generate customer table
        num_customers = 150
        customers = pd.DataFrame({
            'C_CUSTKEY': range(1, num_customers + 1),
            'C_NAME': [str(i) for i in range(1, num_customers + 1)],
            'C_ADDRESS': [str(i) for i in range(1, num_customers + 1)],
            'C_NATIONKEY': [(i % 25) + 1 for i in range(num_customers)],
            'C_PHONE': [str(i) for i in range(1, num_customers + 1)],
            'C_ACCTBAL': [200 + i for i in range(num_customers)],
            'C_MKTSEGMENT': [str(i % 5) for i in range(num_customers)],
            'C_COMMENT': [str(i) for i in range(1, num_customers + 1)]
        })
        
        # Generate orders
        num_orders = 300
        if distribution == 'uniform':
            # Each customer has 2 orders
            orders_data = []
            order_id = 1
            for cust_id in range(1, num_customers + 1):
                for _ in range(2):
                    orders_data.append({
                        'O_ORDERKEY': order_id,
                        'O_CUSTKEY': cust_id,
                        'O_ORDERSTATUS': 'O',
                        'O_TOTALPRICE': 10000 + order_id * 100,
                        'O_ORDERDATE': '1995-01-01',
                        'O_ORDERPRIORITY': '1-URGENT',
                        'O_CLERK': str(order_id % 100),
                        'O_SHIPPRIORITY': 0,
                        'O_COMMENT': str(order_id)
                    })
                    order_id += 1
        else:  # skewed
            # Variable distribution
            orders_data = []
            order_id = 1
            for cust_id in range(1, num_customers + 1):
                num_orders_for_cust = [1, 2, 3][cust_id % 3]
                for _ in range(num_orders_for_cust):
                    orders_data.append({
                        'O_ORDERKEY': order_id,
                        'O_CUSTKEY': cust_id,
                        'O_ORDERSTATUS': 'O',
                        'O_TOTALPRICE': 10000 + order_id * 100,
                        'O_ORDERDATE': '1995-01-01',
                        'O_ORDERPRIORITY': '1-URGENT',
                        'O_CLERK': str(order_id % 100),
                        'O_SHIPPRIORITY': 0,
                        'O_COMMENT': str(order_id)
                    })
                    order_id += 1
        
        orders = pd.DataFrame(orders_data)
        
        # Generate lineitems
        lineitem_data = []
        for _, order_row in orders.iterrows():
            order_key = order_row['O_ORDERKEY']
            # Each order has 2 lineitems
            for line_num in range(1, 3):
                lineitem_data.append({
                    'L_ORDERKEY': order_key,
                    'L_PARTKEY': 1000 + (order_key * 10 + line_num) % 1000,
                    'L_SUPPKEY': 1 + (order_key + line_num) % num_suppliers,
                    'L_LINENUMBER': line_num,
                    'L_QUANTITY': 10 + line_num,
                    'L_EXTENDEDPRICE': 1000.00 + order_key + line_num,
                    'L_DISCOUNT': 0.05,
                    'L_TAX': 0.08,
                    'L_RETURNFLAG': 'N',
                    'L_LINESTATUS': 'O',
                    'L_SHIPDATE': '1995-01-02',
                    'L_COMMITDATE': '1995-01-03',
                    'L_RECEIPTDATE': '1995-01-04',
                    'L_SHIPINSTRUCT': 'NONE',
                    'L_SHIPMODE': 'AIR',
                    'L_COMMENT': str(order_key)
                })
        
        return {
            'nation': pd.DataFrame(nation_data),
            'supplier': suppliers,
            'customer': customers,
            'orders': orders,
            'lineitem': pd.DataFrame(lineitem_data)
        }
    
    def pad_inequality_join(self, dataset_a, dataset_b, query_name, size_a, size_b):
        """Apply padding for inequality joins (TB1, TB2)"""
        diff = abs(size_a - size_b)
        
        if diff == 0:
            # Even if already balanced, add padding rows to both for consistency
            # This ensures both datasets have the same structure
            diff = 0
        
        print(f"   Padding inequality join with {diff} rows difference...")
        
        # Determine which tables and columns to use
        if query_name == 'tb1':
            left_table = 'supplier1'
            right_table = 'supplier2'
            left_col = 'S1_S_ACCTBAL'
            right_col = 'S2_S_ACCTBAL'
            other_cols = {
                'supplier1': ['S1_S_SUPPKEY', 'S1_S_NAME', 'S1_S_ADDRESS', 
                             'S1_S_NATIONKEY', 'S1_S_PHONE', 'S1_S_COMMENT'],
                'supplier2': ['S2_S_SUPPKEY', 'S2_S_NAME', 'S2_S_ADDRESS',
                             'S2_S_NATIONKEY', 'S2_S_PHONE', 'S2_S_COMMENT']
            }
        else:  # tb2
            left_table = 'part1'
            right_table = 'part2'
            left_col = 'P1_P_RETAILPRICE'
            right_col = 'P2_P_RETAILPRICE'
            other_cols = {
                'part1': ['P1_P_PARTKEY', 'P1_P_NAME', 'P1_P_MFGR', 'P1_P_BRAND',
                         'P1_P_TYPE', 'P1_P_SIZE', 'P1_P_CONTAINER', 'P1_P_COMMENT'],
                'part2': ['P2_P_PARTKEY', 'P2_P_NAME', 'P2_P_MFGR', 'P2_P_BRAND',
                         'P2_P_TYPE', 'P2_P_SIZE', 'P2_P_CONTAINER', 'P2_P_COMMENT']
            }
        
        # Add padding row with value -10000 to BOTH datasets' left tables
        padding_left_value = -10000
        
        # Add to dataset_a's left table
        new_row_a = {left_col: padding_left_value}
        for col in other_cols[left_table]:
            # Use -1 for all padding fields (within valid bounds)
            new_row_a[col] = -1
        dataset_a[left_table] = pd.concat([
            dataset_a[left_table],
            pd.DataFrame([new_row_a])
        ], ignore_index=True)
        
        # Add to dataset_b's left table
        new_row_b = {left_col: padding_left_value}
        for col in other_cols[left_table]:
            # Use -1 for all padding fields (within valid bounds)
            new_row_b[col] = -1
        dataset_b[left_table] = pd.concat([
            dataset_b[left_table],
            pd.DataFrame([new_row_b])
        ], ignore_index=True)
        
        # Now add right table padding based on which dataset needs more rows
        if size_a < size_b:
            # Dataset A needs more output rows
            # Add values > -10000 to dataset_a's right table (will match the -10000)
            new_rows = []
            for i in range(diff):
                new_row = {right_col: -9999 + i}  # Values from -9999, -9998, etc.
                for col in other_cols[right_table]:
                    # Use negative values for padding, starting from -1
                    new_row[col] = -(i+1)
                new_rows.append(new_row)
            dataset_a[right_table] = pd.concat([
                dataset_a[right_table],
                pd.DataFrame(new_rows)
            ], ignore_index=True)
            
            # Dataset B doesn't need extra matches
            # Add values < -10000 to dataset_b's right table (won't match the -10000)
            # Add same number of rows to keep table sizes similar
            new_rows = []
            for i in range(diff):
                new_row = {right_col: -20000 - i}  # Values from -20000, -20001, etc.
                for col in other_cols[right_table]:
                    # Use negative values for padding, starting from -1000
                    new_row[col] = -(i+1000)
                new_rows.append(new_row)
            dataset_b[right_table] = pd.concat([
                dataset_b[right_table],
                pd.DataFrame(new_rows)
            ], ignore_index=True)
            
            print(f"   Added padding: dataset_a gets {diff} matching rows, dataset_b gets {diff} non-matching rows")
            
        elif size_a > size_b:
            # Dataset B needs more output rows
            # Add values > -10000 to dataset_b's right table (will match the -10000)
            new_rows = []
            for i in range(diff):
                new_row = {right_col: -9999 + i}  # Values from -9999, -9998, etc.
                for col in other_cols[right_table]:
                    # Use negative values for padding, starting from -1
                    new_row[col] = -(i+1)
                new_rows.append(new_row)
            dataset_b[right_table] = pd.concat([
                dataset_b[right_table],
                pd.DataFrame(new_rows)
            ], ignore_index=True)
            
            # Dataset A doesn't need extra matches
            # Add values < -10000 to dataset_a's right table (won't match the -10000)
            new_rows = []
            for i in range(diff):
                new_row = {right_col: -20000 - i}  # Values from -20000, -20001, etc.
                for col in other_cols[right_table]:
                    # Use negative values for padding, starting from -1000
                    new_row[col] = -(i+1000)
                new_rows.append(new_row)
            dataset_a[right_table] = pd.concat([
                dataset_a[right_table],
                pd.DataFrame(new_rows)
            ], ignore_index=True)
            
            print(f"   Added padding: dataset_b gets {diff} matching rows, dataset_a gets {diff} non-matching rows")
        
        else:
            print(f"   Datasets already balanced, added padding rows for consistency")
        
        return dataset_a, dataset_b
    
    def pad_equality_join(self, dataset_a, dataset_b, query_name, size_a, size_b):
        """Apply padding for multi-way equality joins (TM1, TM3)"""
        diff = abs(size_a - size_b)
        
        if diff == 0:
            return dataset_a, dataset_b
        
        print(f"   Padding equality join with {diff} complete chains...")
        
        # Determine which dataset needs padding
        target_dataset = dataset_a if size_a < size_b else dataset_b
        
        # Add complete chains using negative IDs
        for i in range(diff):
            cust_id = -(i + 1)
            order_id = -(i + 1)
            
            # Add customer
            new_customer = {
                'C_CUSTKEY': cust_id,
                'C_NAME': str(-1),  # String fields get negative number as string
                'C_ADDRESS': str(-1),
                'C_NATIONKEY': 0,
                'C_PHONE': str(-1),
                'C_ACCTBAL': 0,
                'C_MKTSEGMENT': str(-1),
                'C_COMMENT': str(-1)
            }
            target_dataset['customer'] = pd.concat([
                target_dataset['customer'],
                pd.DataFrame([new_customer])
            ], ignore_index=True)
            
            # Add order
            new_order = {
                'O_ORDERKEY': order_id,
                'O_CUSTKEY': cust_id,
                'O_ORDERSTATUS': 'O',
                'O_TOTALPRICE': 0,
                'O_ORDERDATE': '1995-01-01',
                'O_ORDERPRIORITY': str(-1),
                'O_CLERK': str(-1),
                'O_SHIPPRIORITY': 0,
                'O_COMMENT': str(-1)
            }
            target_dataset['orders'] = pd.concat([
                target_dataset['orders'],
                pd.DataFrame([new_order])
            ], ignore_index=True)
            
            # Add lineitem
            new_lineitem = {
                'L_ORDERKEY': order_id,
                'L_PARTKEY': 0,
                'L_SUPPKEY': 0,
                'L_LINENUMBER': 1,
                'L_QUANTITY': 0,
                'L_EXTENDEDPRICE': 0,
                'L_DISCOUNT': 0,
                'L_TAX': 0,
                'L_RETURNFLAG': 'N',
                'L_LINESTATUS': 'O',
                'L_SHIPDATE': '1995-01-01',
                'L_COMMITDATE': '1995-01-01',
                'L_RECEIPTDATE': '1995-01-01',
                'L_SHIPINSTRUCT': str(-1),
                'L_SHIPMODE': str(-1),
                'L_COMMENT': str(-1)
            }
            target_dataset['lineitem'] = pd.concat([
                target_dataset['lineitem'],
                pd.DataFrame([new_lineitem])
            ], ignore_index=True)
        
        print(f"   Added {diff} complete chains (customer→order→lineitem)")
        return dataset_a, dataset_b
    
    def pad_mixed_join(self, dataset_a, dataset_b, query_name, size_a, size_b):
        """Apply padding for mixed joins (TM2 with inequality component)"""
        # For now, treat similar to equality join
        # Could be refined based on specific TM2 query structure
        return self.pad_equality_join(dataset_a, dataset_b, query_name, size_a, size_b)
    
    def run_sqlite_count(self, dataset, query_name):
        """Run query on dataset using SQLite and return row count"""
        query_file = self.queries_path / f"tpch_{query_name}.sql"
        with open(query_file, 'r') as f:
            query_sql = f.read()
        
        # Create in-memory SQLite database
        conn = sqlite3.connect(':memory:')
        
        # Load all dataframes as tables
        for table_name, df in dataset.items():
            df.to_sql(table_name, conn, index=False, if_exists='replace')
        
        # Convert SELECT * to SELECT COUNT(*)
        count_query = query_sql.replace("SELECT *", "SELECT COUNT(*)")
        
        try:
            cursor = conn.cursor()
            cursor.execute(count_query)
            count = cursor.fetchone()[0]
        except Exception as e:
            print(f"SQLite error: {e}")
            print(f"Query: {count_query}")
            raise
        finally:
            conn.close()
        
        return count
    
    def save_dataset(self, dataset, rel_path):
        """Save dataset to CSV files"""
        full_path = self.base_path / rel_path
        full_path.mkdir(parents=True, exist_ok=True)
        
        for table_name, df in dataset.items():
            csv_path = full_path / f"{table_name}.csv"
            df.to_csv(csv_path, index=False)
            print(f"   Saved {table_name}.csv ({len(df)} rows)")

def main():
    generator = TestDatasetGenerator()
    generator.generate_all_datasets()
    print("\n" + "="*60)
    print("✓ All datasets generated successfully!")
    print("="*60)

if __name__ == "__main__":
    main()