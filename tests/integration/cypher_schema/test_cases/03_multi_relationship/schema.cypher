CREATE NODE TABLE Person(id INT64, name INT32, PRIMARY KEY (id))
CREATE NODE TABLE Company(id INT64, name INT32, PRIMARY KEY (id))
CREATE REL TABLE WorksAt(FROM Person TO Company, role INT32, since INT64)
CREATE REL TABLE Manages(FROM Person TO Person, department INT32)
