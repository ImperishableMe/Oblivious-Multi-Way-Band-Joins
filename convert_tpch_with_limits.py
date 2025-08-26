#!/usr/bin/env python3
"""
Convert TPC-H raw .tbl files to integer CSV format with range limits.
Also creates aliased tables for self-joins.

Range limits (from enclave_types.h):
- Valid range: [-1,073,741,820, 1,073,741,820]
- Infinity values: -1,073,741,821 (negative infinity), 1,073,741,821 (positive infinity)
"""

import os
import sys
import hashlib
from datetime import datetime
from pathlib import Path

class TPCHConverterWithLimits:
    def __init__(self):
        # Integer range limits from enclave_types.h
        self.MIN_VALUE = -1073741820
        self.MAX_VALUE = 1073741820
        
        # Scale factor for decimal conversion (2 decimal places)
        self.SCALE_FACTOR = 100
        
        # Date epoch
        self.EPOCH_DATE = datetime(1900, 1, 1)
        
        # TPC-H table schemas
        self.schemas = {
            'nation': ['N_NATIONKEY', 'N_NAME', 'N_REGIONKEY', 'N_COMMENT'],
            'region': ['R_REGIONKEY', 'R_NAME', 'R_COMMENT'],
            'part': ['P_PARTKEY', 'P_NAME', 'P_MFGR', 'P_BRAND', 'P_TYPE', 'P_SIZE', 'P_CONTAINER', 'P_RETAILPRICE', 'P_COMMENT'],
            'supplier': ['S_SUPPKEY', 'S_NAME', 'S_ADDRESS', 'S_NATIONKEY', 'S_PHONE', 'S_ACCTBAL', 'S_COMMENT'],
            'partsupp': ['PS_PARTKEY', 'PS_SUPPKEY', 'PS_AVAILQTY', 'PS_SUPPLYCOST', 'PS_COMMENT'],
            'customer': ['C_CUSTKEY', 'C_NAME', 'C_ADDRESS', 'C_NATIONKEY', 'C_PHONE', 'C_ACCTBAL', 'C_MKTSEGMENT', 'C_COMMENT'],
            'orders': ['O_ORDERKEY', 'O_CUSTKEY', 'O_ORDERSTATUS', 'O_TOTALPRICE', 'O_ORDERDATE', 'O_ORDERPRIORITY', 'O_CLERK', 'O_SHIPPRIORITY', 'O_COMMENT'],
            'lineitem': ['L_ORDERKEY', 'L_PARTKEY', 'L_SUPPKEY', 'L_LINENUMBER', 'L_QUANTITY', 'L_EXTENDEDPRICE', 'L_DISCOUNT', 'L_TAX', 'L_RETURNFLAG', 'L_LINESTATUS', 'L_SHIPDATE', 'L_COMMITDATE', 'L_RECEIPTDATE', 'L_SHIPINSTRUCT', 'L_SHIPMODE', 'L_COMMENT']
        }
        
        # Data type mappings
        self.data_types = {
            # Integer fields
            'N_NATIONKEY': 'int', 'N_REGIONKEY': 'int',
            'R_REGIONKEY': 'int',
            'P_PARTKEY': 'int', 'P_SIZE': 'int',
            'S_SUPPKEY': 'int', 'S_NATIONKEY': 'int',
            'PS_PARTKEY': 'int', 'PS_SUPPKEY': 'int', 'PS_AVAILQTY': 'int',
            'C_CUSTKEY': 'int', 'C_NATIONKEY': 'int',
            'O_ORDERKEY': 'int', 'O_CUSTKEY': 'int', 'O_SHIPPRIORITY': 'int',
            'L_ORDERKEY': 'int', 'L_PARTKEY': 'int', 'L_SUPPKEY': 'int', 'L_LINENUMBER': 'int',
            
            # Decimal fields (will be scaled by 100)
            'P_RETAILPRICE': 'decimal',
            'S_ACCTBAL': 'decimal',
            'PS_SUPPLYCOST': 'decimal',
            'C_ACCTBAL': 'decimal',
            'O_TOTALPRICE': 'decimal',
            'L_QUANTITY': 'decimal', 'L_EXTENDEDPRICE': 'decimal', 'L_DISCOUNT': 'decimal', 'L_TAX': 'decimal',
            
            # Date fields
            'O_ORDERDATE': 'date',
            'L_SHIPDATE': 'date', 'L_COMMITDATE': 'date', 'L_RECEIPTDATE': 'date',
            
            # Everything else is string (will be hashed)
        }
        
        # Tables to create aliases for (self-joins)
        self.aliased_tables = {
            'nation': [('nation1', 'N1_'), ('nation2', 'N2_')],
            'supplier': [('supplier1', 'S1_'), ('supplier2', 'S2_')],
            'part': [('part1', 'P1_'), ('part2', 'P2_')]
        }
    
    def clamp_value(self, value):
        """Clamp value to valid range"""
        return max(self.MIN_VALUE, min(self.MAX_VALUE, value))
    
    def hash_string(self, s):
        """Convert string to hash integer within valid range"""
        if not s or s.strip() == '':
            return 0
        # Use modulo to keep in range, then clamp for safety
        hash_val = abs(hash(s.strip())) % self.MAX_VALUE
        return self.clamp_value(hash_val)
    
    def convert_decimal(self, value):
        """Convert decimal to integer by scaling by 100, then clamp"""
        try:
            scaled = int(float(value.strip()) * self.SCALE_FACTOR)
            return self.clamp_value(scaled)
        except:
            return 0
    
    def convert_date(self, date_str):
        """Convert date string to days since epoch, then clamp"""
        try:
            date_obj = datetime.strptime(date_str.strip(), '%Y-%m-%d')
            delta = date_obj - self.EPOCH_DATE
            return self.clamp_value(delta.days)
        except:
            return 0
    
    def convert_integer(self, value):
        """Convert integer and clamp to valid range"""
        try:
            return self.clamp_value(int(value.strip()))
        except:
            return 0
    
    def convert_value(self, field_name, value):
        """Convert a single value based on its data type"""
        value = value.strip()
        
        if field_name in self.data_types:
            data_type = self.data_types[field_name]
            
            if data_type == 'int':
                return self.convert_integer(value)
            elif data_type == 'decimal':
                return self.convert_decimal(value)
            elif data_type == 'date':
                return self.convert_date(value)
        
        # Default: treat as string and hash
        return self.hash_string(value)
    
    def convert_table(self, input_file, output_file, table_name):
        """Convert a single TPC-H table from .tbl to integer CSV"""
        if table_name not in self.schemas:
            print(f"Unknown table: {table_name}")
            return False
        
        schema = self.schemas[table_name]
        rows_processed = 0
        
        print(f"  Converting {table_name}: {input_file} -> {output_file}")
        
        try:
            # Store converted data for aliased table creation
            converted_data = []
            
            with open(input_file, 'r', encoding='utf-8', errors='ignore') as infile:
                for line_num, line in enumerate(infile, 1):
                    line = line.strip()
                    if not line:
                        continue
                    
                    # TPC-H uses '|' as delimiter and ends with '|'
                    fields = line.split('|')[:-1]  # Remove last empty element
                    
                    if len(fields) != len(schema):
                        print(f"    Warning: Line {line_num} has {len(fields)} fields, expected {len(schema)}")
                        continue
                    
                    # Convert each field
                    converted_fields = []
                    for i, field_value in enumerate(fields):
                        field_name = schema[i]
                        converted_value = self.convert_value(field_name, field_value)
                        converted_fields.append(str(converted_value))
                    
                    converted_data.append(converted_fields)
                    rows_processed += 1
                    
                    if rows_processed % 10000 == 0:
                        print(f"    Processed {rows_processed} rows...")
            
            # Write main table
            with open(output_file, 'w') as outfile:
                # Write CSV header
                outfile.write(','.join(schema) + '\n')
                # Write data
                for row in converted_data:
                    outfile.write(','.join(row) + '\n')
            
            print(f"    Completed: {rows_processed} rows")
            
            # Create aliased tables if needed
            if table_name in self.aliased_tables:
                output_dir = Path(output_file).parent
                for alias_name, prefix in self.aliased_tables[table_name]:
                    alias_file = output_dir / f"{alias_name}.csv"
                    with open(alias_file, 'w') as outfile:
                        # Write header with prefixed column names
                        prefixed_schema = [prefix + col for col in schema]
                        outfile.write(','.join(prefixed_schema) + '\n')
                        # Write same data
                        for row in converted_data:
                            outfile.write(','.join(row) + '\n')
                    print(f"    Created alias: {alias_name}.csv with {prefix} prefix")
            
            return True
            
        except Exception as e:
            print(f"  Error converting {table_name}: {e}")
            return False
    
    def convert_all_tables(self, input_dir, output_dir):
        """Convert all TPC-H tables in a directory"""
        input_path = Path(input_dir)
        output_path = Path(output_dir)
        
        # Create output directory if needed
        output_path.mkdir(parents=True, exist_ok=True)
        
        print(f"\nProcessing: {input_dir} -> {output_dir}")
        print(f"Integer range: [{self.MIN_VALUE:,}, {self.MAX_VALUE:,}]")
        
        success_count = 0
        total_tables = len(self.schemas)
        
        for table_name in self.schemas:
            input_file = input_path / f"{table_name}.tbl"
            output_file = output_path / f"{table_name}.csv"
            
            if input_file.exists():
                if self.convert_table(str(input_file), str(output_file), table_name):
                    success_count += 1
            else:
                print(f"  Warning: {input_file} not found")
        
        print(f"\nConversion complete: {success_count}/{total_tables} tables converted")
        
        # List all created files
        created_files = sorted(output_path.glob("*.csv"))
        if created_files:
            print(f"\nCreated files in {output_dir}:")
            for f in created_files:
                size = f.stat().st_size
                print(f"  {f.name}: {size:,} bytes")
        
        return success_count == total_tables

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 convert_tpch_with_limits.py <input_dir> <output_dir>")
        print("Example: python3 convert_tpch_with_limits.py raw_data/data_0_001 plaintext/data/data_0_001")
        print("\nThis script:")
        print("  1. Converts TPC-H .tbl files to integer CSV format")
        print("  2. Clamps all values to range [-1,073,741,820, 1,073,741,820]")
        print("  3. Creates aliased tables (nation1/2, supplier1/2, part1/2) for self-joins")
        return
    
    input_dir = sys.argv[1]
    output_dir = sys.argv[2]
    
    converter = TPCHConverterWithLimits()
    converter.convert_all_tables(input_dir, output_dir)

if __name__ == "__main__":
    main()