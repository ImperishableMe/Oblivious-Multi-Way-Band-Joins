# ObliDB - Oblivious Property Graph Database Prototype

[![CI](https://github.com/username/obliDB/actions/workflows/ci.yml/badge.svg)](https://github.com/username/obliDB/actions/workflows/ci.yml)
![Code Coverage](coverage.svg)

A columnar storage prototype built to execute a specific Cypher-shaped query pattern with clean separation of storage and execution layers. Now with HTTP query capabilities!

## Features

- Columnar storage with fixed-width and variable-length (string) support
- Query execution for one-hop property graph patterns
- HTTP server for issuing queries via REST API
- Cypher query parsing for easy query expression
- Strong error handling with custom exception types
- Comprehensive test suite with GoogleTest

## Requirements

- C++17 compatible compiler
- CMake 3.14 or newer
- Python 3.x (for test data generation and HTTP testing)
- Python requests package (`pip install requests`)
- (Optional) gcovr (for code coverage reporting)

## Building

Clone the repository and build with CMake:

```bash
# Create and enter build directory
mkdir -p build && cd build

# Configure build
cmake ..

# Build
cmake --build . -j
```

### Code Coverage

To build with code coverage enabled (requires GCC or Clang):

```bash
# Configure with coverage enabled
cmake -DCODE_COVERAGE=ON ..

# Build
cmake --build . -j

# Run tests
ctest --output-on-failure

# Generate coverage report
make coverage
```

The coverage report will be available in HTML format at `build/coverage.html`.

## Testing

Run the tests using CTest:

```bash
# From the build directory
ctest --output-on-failure
```

This will:
1. Generate test data (binary column files and schema)
2. Run all unit tests
3. Run the HTTP server roundtrip test

### Running Specific Tests

To run only unit tests:

```bash
# From the build directory
./unit_tests
```

To run specific test categories:

```bash
# From the build directory
./unit_tests --gtest_filter=ParserTests.*
```

To manually run the server roundtrip test:

```bash
# First ensure the server is built
cmake --build . --target obdb_server

# Then run the test script which will:
# 1. Start the server on a random port
# 2. Send a test query
# 3. Verify the response
../tests/run_server_test.sh .
```

Alternatively, if you encounter issues with CTest running the server_roundtrip test:

```bash
# Run directly from the build directory
../tests/run_server_test.sh .
```

## HTTP Server

The ObliDB server provides HTTP access to the database via a REST API.

### Starting the Server

```bash
# Start the server with default settings
./obdb_server

# Specify a different port
./obdb_server --port 9000

# Specify a different database directory
./obdb_server --db /path/to/database

# Combine options
./obdb_server --port 9000 --db /path/to/database
```

The default port is 8080, and the default database path is `tests/data/generated`.

### Querying the Server

You can query the server using any HTTP client. Here's an example using curl:

```bash
curl -X POST http://localhost:8080/query \
     -H 'Content-Type: application/json' \
     -d '{"cypher":"MATCH (a:Person)-[b:WorksAt]->(c:Org) WHERE a.age > 20 AND c.name = \"uw\" RETURN a.name,a.age,b.since,c.name"}'
```

The server will respond with a JSON object containing the result rows:

```json
{
  "rows": [
    ["Alice", 25, 2020, "uw"],
    ["Carol", 32, 2018, "uw"],
    ["Dave", 45, 2023, "uw"],
    ["Eve", 21, 2024, "uw"]
  ]
}
```

### Error Handling

The server returns appropriate HTTP status codes:

- 200: Successful query execution
- 400: Parse error (invalid JSON or Cypher syntax)
- 500: Execution error (storage or query errors)

Error responses include details:

```json
{
  "error": "parse_error",
  "msg": "Failed to parse Cypher query format"
}
```

## Example Query

The prototype supports a specific Cypher-shaped query pattern:

```cypher
MATCH (a:Person)-[b:WorksAt]->(c:Org)
WHERE a.age > 20 AND c.name = "uw"
RETURN a.name, a.age, b.since, c.name
```

## Code Structure

- `include/` - Public API headers
- `src/` - Implementation files
- `tests/` - Unit tests with GoogleTest
- `scripts/` - Python script for test data generation

## Key Components

- `Schema` - Parses schema.json with metadata about columns
- `IStorage` - Storage interface for accessing column data
- `BinaryFileStorage` - Concrete storage implementation using binary files
- `ColumnIterator` - Interface for iterating over column values
- `Query` - Structure representing the query to execute
- `executeQuery` - Core execution algorithm
- `parseCypher` - Parses Cypher query strings into Query objects
- `obdb_server` - HTTP server for exposing query functionality via REST

## License

MIT 
