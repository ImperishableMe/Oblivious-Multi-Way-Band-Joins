#!/usr/bin/env python3
"""
Efficient data generator for Person and Person_Follow_Person CSV files.
Optimized for handling up to 1 billion records efficiently.
"""

import os
import random
import argparse
from typing import Generator, Tuple
import time

# Sample data for realistic person generation
FIRST_NAMES = [
    "James", "Mary", "John", "Patricia", "Robert", "Jennifer", "Michael", "Linda",
    "William", "Elizabeth", "David", "Barbara", "Richard", "Susan", "Joseph", "Jessica",
    "Thomas", "Sarah", "Christopher", "Karen", "Charles", "Nancy", "Daniel", "Lisa",
    "Matthew", "Betty", "Anthony", "Helen", "Mark", "Sandra", "Donald", "Donna",
    "Steven", "Carol", "Paul", "Ruth", "Andrew", "Sharon", "Joshua", "Michelle",
    "Kenneth", "Laura", "Kevin", "Sarah", "Brian", "Kimberly", "George", "Deborah",
    "Edward", "Dorothy", "Ronald", "Lisa", "Timothy", "Nancy", "Jason", "Karen"
]

LAST_NAMES = [
    "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller", "Davis",
    "Rodriguez", "Martinez", "Hernandez", "Lopez", "Gonzalez", "Wilson", "Anderson",
    "Thomas", "Taylor", "Moore", "Jackson", "Martin", "Lee", "Perez", "Thompson",
    "White", "Harris", "Sanchez", "Clark", "Ramirez", "Lewis", "Robinson", "Walker",
    "Young", "Allen", "King", "Wright", "Scott", "Torres", "Nguyen", "Hill",
    "Flores", "Green", "Adams", "Nelson", "Baker", "Hall", "Rivera", "Campbell",
    "Mitchell", "Carter", "Roberts", "Gomez", "Phillips", "Evans", "Turner", "Diaz"
]

CITIES = [
    "New York", "Los Angeles", "Chicago", "Houston", "Phoenix", "Philadelphia",
    "San Antonio", "San Diego", "Dallas", "San Jose", "Austin", "Jacksonville",
    "Fort Worth", "Columbus", "Indianapolis", "Charlotte", "San Francisco", "Seattle",
    "Denver", "Boston", "El Paso", "Nashville", "Detroit", "Oklahoma City",
    "Portland", "Las Vegas", "Memphis", "Louisville", "Baltimore", "Milwaukee",
    "Albuquerque", "Tucson", "Fresno", "Sacramento", "Mesa", "Kansas City",
    "Atlanta", "Long Beach", "Colorado Springs", "Raleigh", "Miami", "Virginia Beach",
    "Omaha", "Oakland", "Minneapolis", "Tulsa", "Arlington", "Tampa", "New Orleans"
]

COUNTRIES = [
    "United States", "Canada", "Mexico", "Brazil", "Argentina", "Chile", "Colombia",
    "Peru", "Venezuela", "Ecuador", "Bolivia", "Paraguay", "Uruguay", "Guyana",
    "Suriname", "France", "Germany", "Italy", "Spain", "United Kingdom", "Netherlands",
    "Belgium", "Switzerland", "Austria", "Sweden", "Norway", "Denmark", "Finland",
    "Poland", "Czech Republic", "Hungary", "Romania", "Bulgaria", "Greece", "Portugal",
    "Ireland", "Croatia", "Serbia", "Bosnia and Herzegovina", "Montenegro", "Albania",
    "Macedonia", "Slovenia", "Slovakia", "Lithuania", "Latvia", "Estonia", "Russia",
    "Ukraine", "Belarus", "Moldova", "Georgia", "Armenia", "Azerbaijan", "Kazakhstan",
    "Uzbekistan", "Turkmenistan", "Kyrgyzstan", "Tajikistan", "China", "Japan",
    "South Korea", "India", "Pakistan", "Bangladesh", "Sri Lanka", "Nepal", "Bhutan",
    "Myanmar", "Thailand", "Vietnam", "Laos", "Cambodia", "Malaysia", "Singapore",
    "Indonesia", "Philippines", "Brunei", "East Timor", "Australia", "New Zealand",
    "Papua New Guinea", "Fiji", "Solomon Islands", "Vanuatu", "Samoa", "Tonga",
    "Tuvalu", "Kiribati", "Nauru", "Palau", "Marshall Islands", "Micronesia"
]


def generate_person_batch(start_id: int, batch_size: int) -> Generator[str, None, None]:
    """Generate a batch of person records as CSV lines."""
    for i in range(batch_size):
        person_id = start_id + i
        first_name = random.choice(FIRST_NAMES)
        last_name = random.choice(LAST_NAMES)
        age = random.randint(18, 80)
        city = random.choice(CITIES)
        country = random.choice(COUNTRIES)
        
        yield f"{person_id}|{first_name}|{last_name}|{age}|{city}|{country}\n"


def generate_follow_batch(person_count: int, batch_size: int, existing_pairs: set) -> Generator[str, None, None]:
    """Generate a batch of follow relationship records as CSV lines."""
    generated = 0
    max_attempts = batch_size * 10  # Prevent infinite loop
    attempts = 0
    
    while generated < batch_size and attempts < max_attempts:
        attempts += 1
        person_id1 = random.randint(0, person_count - 1)
        person_id2 = random.randint(0, person_count - 1)
        
        # Ensure different persons and no duplicate relationships
        if person_id1 != person_id2 and (person_id1, person_id2) not in existing_pairs:
            existing_pairs.add((person_id1, person_id2))
            since = random.randint(1, 120)  # 1-120 months (10 years)
            num_messages = random.randint(0, 1000)
            
            yield f"{person_id1}|{person_id2}|{since}|{num_messages}\n"
            generated += 1


def generate_person_csv(output_dir: str, num_persons: int, batch_size: int = 100000):
    """Generate Person.csv file efficiently using batched processing."""
    filepath = os.path.join(output_dir, "Person.csv")
    
    print(f"Generating {num_persons:,} persons...")
    start_time = time.time()
    
    with open(filepath, 'w', buffering=8192*1024) as f:  # 8MB buffer
        # Write header
        f.write("id|first_name|last_name|age|city|country\n")
        f.write("int32|string|string|int32|string|string\n")
        
        # Process in batches
        processed = 0
        while processed < num_persons:
            current_batch_size = min(batch_size, num_persons - processed)
            
            # Generate and write batch
            batch_lines = list(generate_person_batch(processed, current_batch_size))
            f.writelines(batch_lines)
            
            processed += current_batch_size
            
            # Progress update
            if processed % (batch_size * 10) == 0:
                elapsed = time.time() - start_time
                rate = processed / elapsed
                print(f"  Progress: {processed:,}/{num_persons:,} persons ({processed/num_persons*100:.1f}%) - {rate:,.0f} records/sec")
    
    elapsed = time.time() - start_time
    print(f"Person.csv generated in {elapsed:.2f} seconds ({num_persons/elapsed:,.0f} records/sec)")


def generate_follow_csv(output_dir: str, num_persons: int, num_follows: int, batch_size: int = 100000):
    """Generate Person_Follow_Person.csv file efficiently using batched processing."""
    filepath = os.path.join(output_dir, "Person_Follow_Person.csv")
    
    print(f"Generating {num_follows:,} follow relationships...")
    start_time = time.time()
    
    # Track existing pairs to avoid duplicates
    existing_pairs = set()
    
    with open(filepath, 'w', buffering=8192*1024) as f:  # 8MB buffer
        # Write header
        f.write("person1Id|person2Id|since|numberOfMessages\n")
        f.write("int32|int32|int32|int32\n")
        
        # Process in batches
        processed = 0
        while processed < num_follows:
            current_batch_size = min(batch_size, num_follows - processed)
            
            # Generate and write batch
            batch_lines = list(generate_follow_batch(num_persons, current_batch_size, existing_pairs))
            f.writelines(batch_lines)
            
            processed += len(batch_lines)
            
            # Progress update
            if processed % (batch_size * 10) == 0:
                elapsed = time.time() - start_time
                rate = processed / elapsed
                print(f"  Progress: {processed:,}/{num_follows:,} follows ({processed/num_follows*100:.1f}%) - {rate:,.0f} records/sec")
            
            # Safety check: if we can't generate enough unique pairs, break
            if len(batch_lines) < current_batch_size * 0.1:  # Less than 10% success rate
                print(f"  Warning: Difficulty generating unique pairs. Generated {processed:,} out of {num_follows:,} requested.")
                break
    
    elapsed = time.time() - start_time
    actual_generated = min(processed, len(existing_pairs))
    print(f"Person_Follow_Person.csv generated in {elapsed:.2f} seconds ({actual_generated/elapsed:,.0f} records/sec)")
    print(f"Generated {actual_generated:,} unique follow relationships")


def main():
    parser = argparse.ArgumentParser(description="Generate Person and Person_Follow_Person CSV files")
    parser.add_argument("--persons", "-p", type=int, default=1000000, 
                       help="Number of persons to generate (default: 1,000,000)")
    parser.add_argument("--follows", "-f", type=int, default=5000000,
                       help="Number of follow relationships to generate (default: 5,000,000)")
    parser.add_argument("--output-dir", "-o", type=str, default="./data",
                       help="Output directory for CSV files (default: ./data)")
    parser.add_argument("--batch-size", "-b", type=int, default=100000,
                       help="Batch size for processing (default: 100,000)")
    
    args = parser.parse_args()
    
    # Validate arguments
    if args.persons <= 0:
        print("Error: Number of persons must be positive")
        return 1
    
    if args.follows < 0:
        print("Error: Number of follows cannot be negative")
        return 1
    
    # Check if we have enough possible unique pairs
    max_possible_follows = args.persons * (args.persons - 1)
    if args.follows > max_possible_follows:
        print(f"Warning: Requested {args.follows:,} follows but only {max_possible_follows:,} unique pairs possible with {args.persons:,} persons")
        args.follows = max_possible_follows
    
    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    print(f"Data Generation Configuration:")
    print(f"  Persons: {args.persons:,}")
    print(f"  Follow relationships: {args.follows:,}")
    print(f"  Output directory: {args.output_dir}")
    print(f"  Batch size: {args.batch_size:,}")
    print(f"  Total follows to generate: {args.follows:,}")

    total_start_time = time.time()
    
    # Generate Person.csv
    generate_person_csv(args.output_dir, args.persons, args.batch_size)
    print("Generated Person.csv successfully.")
    
    # Generate Person_Follow_Person.csv
    if args.follows > 0:
        generate_follow_csv(args.output_dir, args.persons, args.follows, args.batch_size)
    else:
        # Create empty follow file with just headers
        filepath = os.path.join(args.output_dir, "Person_Follow_Person.csv")
        with open(filepath, 'w') as f:
            f.write("personId1|personId2|since|numberOfMessages\n")
        print("Created empty Person_Follow_Person.csv (0 follow relationships requested)")
    
    total_elapsed = time.time() - total_start_time
    print(f"\nTotal generation time: {total_elapsed:.2f} seconds")
    print("Data generation completed successfully!")
    
    return 0


if __name__ == "__main__":
    exit(main())
