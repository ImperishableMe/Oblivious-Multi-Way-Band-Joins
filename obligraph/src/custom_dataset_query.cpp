/**
 * One-hop query to find friends of a person
 *
 **/ 

#include <unistd.h>
#include <sys/resource.h>
#include <iostream>

#include "definitions.h"
#include "obl_building_blocks.h"
#include "timer.h"
#include "config.h"

using namespace obligraph;
using namespace std;

void print_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    cout << "Max RSS: " << usage.ru_maxrss / 1024.0 << " MB\n";
}

int main(int argc, char* argv[]) {

    // Parse command line arguments
    if (argc > 1) {
        try {
            int num_threads = std::stoi(argv[1]);
            if (num_threads > 0) {
                number_of_threads.store(num_threads);
                cout << "Set number of threads to: " << num_threads << endl;
            } else {
                cerr << "Error: num_threads must be positive, using default." << endl;
            }
        } catch (const std::invalid_argument& e) {
            cerr << "Error: Invalid number format for num_threads, using default." << endl;
        }
    }

    // Initialize the catalog
    Catalog catalog;
    ThreadPool pool(number_of_threads.load());

    string dataDir = "../data/huge/"; 
    // Import node and edge data from CSV files
    try {
        catalog.importNodeFromCSV(dataDir + "Person.csv");
        catalog.importEdgeFromCSV(dataDir + "Person_Follow_Person.csv");
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

    Table& followeeTable = catalog.tables[0]; 
    Table& followFor = catalog.tables[1]; 
    Table& followRev = catalog.tables[2]; // Reverse edge table
    Table& followerTable = catalog.tables[3]; // Friend table is also a Person table

    followeeTable.name = "a";
    followFor.name = "b_fwd";
    followFor.node_table_names = {"a", "c"}; // srcNodeName,
    followRev.name = "b_rev";
    followRev.node_table_names = {"c", "a"}; // srcNodeName,
    followerTable.name = "c";

    // Create a OneHopQuery to find friends of a person
    OneHopQuery query(
                    /*followeeTable*/"a", 
                    /*followFor*/"b", 
                    /*followerTable*/"c",
                    /*filter*/{{"a", {{"id", Predicate::Cmp::EQ, int32_t(14)}}}},
                    /*projection*/{{"a", "id"}, {"b", "since"}, {"b", "numberOfMessages"}, {"c", "first_name"}}
                );
    {
        Table result;
        Benchmark bench([&]() { 
            try {
                result = oneHop(catalog, query, pool);
            } catch (const runtime_error& e) {
                cerr << "Error executing one-hop query: " << e.what() << endl;
                exit(1);
            }   
        }, 1);
        // Print the result
        cout << "One-hop query result:" << endl;
        result.print(); 
    } 

    // Print memory usage
    print_memory_usage();

    return 0; 
}


