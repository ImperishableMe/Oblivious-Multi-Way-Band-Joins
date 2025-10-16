/**
*   :param personId: 10995116277794
    MATCH (n:Person {id: $personId })-[r:KNOWS]-(friend)
    RETURN
        friend.id AS personId,
        friend.firstName AS firstName,
        friend.lastName AS lastName,
        r.creationDate AS friendshipCreationDate
 */


#include <unistd.h>
#include <sys/resource.h>
#include <iostream>

#include "definitions.h"
#include "obl_building_blocks.h"
#include "timer.h"

using namespace obligraph;
using namespace std;

void print_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    cout << "Max RSS: " << usage.ru_maxrss / 1024.0 << " MB\n";
}

int main(int argc, char* argv[]) {

    // Initialize the catalog
    Catalog catalog;
    ThreadPool pool(std::thread::hardware_concurrency());

    string dataDir = "../data/LDBC_SF1/"; 
    // Import node and edge data from CSV files
    try {
        catalog.importNodeFromCSV(dataDir + "Person.csv");
        catalog.importEdgeFromCSV(dataDir + "Person_knows_Person.csv");
        catalog.importNodeFromCSV(dataDir + "Person.csv");
    } catch (const runtime_error& e) {
        cerr << "Error importing data: " << e.what() << endl;
        return 1;
    }
    // equivalent to columnar storage, just fetch columns that are necessary!
    // catalog.tables[0] = catalog.tables[0].project({"id"});
    // catalog.tables[1] = catalog.tables[1].project({"creationDate"});
    // catalog.tables[2] = catalog.tables[2].project({"creationDate"});
    // catalog.tables[3] = catalog.tables[3].project({"firstName", "lastName"});

    Table& personTable = catalog.tables[0]; 
    Table& knowsTable = catalog.tables[1]; 
    Table& knowsRev = catalog.tables[2]; // Reverse edge table
    Table& friendTable = catalog.tables[3]; // Friend table is also a Person table

    personTable.name = "a";
    knowsTable.name = "b_fwd";
    knowsTable.node_table_names = {"a", "c"}; // srcNodeName,
    knowsRev.name = "b_rev";
    knowsRev.node_table_names = {"c", "a"}; // srcNodeName,
    friendTable.name = "c";

    // Create a OneHopQuery to find friends of a person
    OneHopQuery query("a", "b", "c",
                    {{"a", {{"id", Predicate::Cmp::EQ, int64_t(14)}}}},
                    {{"a", "id"}, {"b", "creationDate"}, {"c", "firstName"}, {"c", "lastName"}, {"b", "creationDate"}});
    {
        Table result;
        Benchmark bench([&]() { 
            try {
                result = oneHop(catalog, query, pool);
            } catch (const runtime_error& e) {
                cerr << "Error executing one-hop query: " << e.what() << endl;
                exit(1);
            }   
        }, 3);
        // Print the result
        cout << "One-hop query result:" << endl;
        result.print(); 
    } 

    // Print memory usage
    print_memory_usage();

    return 0; 
}


