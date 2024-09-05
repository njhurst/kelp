# Kelp Distributed File System

Kelp is a hypothetical robust and fault-tolerant distributed file system designed to be easy to use, deploy, and maintain.  I started out wanting a simple file storing system, but I've fallen in love with the notion of persistent objects with stronger consistency guarantees.  I also have been a long fan of CRDTs and so rather than trying to build yet another REST/server based system, my goal is for each node to be able to operate independently and write parity data to other nodes.  Rather than a single node (or raft etc) being the master I am imagining that people get their own namespaces, and when they want files to be available on other nodes they simply share them into the other node's namespace.

## Overview

Kelp is built from the ground up with highly reliable writes, erasure coding for data durability, and a simple design that makes it easy to understand and maintain. The system consists of three levels: Blades, Thalus, and Kelp.

*   **Blades**: Store data in 4kB cells, which are written atomically and include both their data and metadata.
*   **Thalus**: Responsible for storing persistent objects and managing the Blades.  Objects can be data, or cbor encoded metadata.  You can think of this as like python objects in memory, but they are stored on disk and can be shared between nodes.  A filesystem can be built on top of this.
*   **Kelp**: The highest level, responsible for managing the file system.

## Features

*   **Fault-tolerant**: Designed to handle failures and recover from them.
*   **Moderately secure**: Built with security in mind, but not excessively complex.
*   **Efficient**: Optimized for performance and storage efficiency.
*   **Easy to use**: Simple design makes it easy to understand and maintain.

## Technical Details

*   **Total address space**: 64b indexing per volume, providing a total of 16EB.
*   **Data alignment**: 16byte alignment used throughout the system, data is striped across cells.
*   **Erasure coding**: Reed-Solomon code with 8 data cells and up to 248 parity cells (RS(8, +)) with parity reconfigurable at runtime.  Designed to work efficiently without accessing the systematic data.
*   **Garbage collection**: Garbage collection is done on a per-volume basis, tracing globally when available with a background process that runs periodically.  References are tagged in cbor and are used to determine if an object is still in use.  Weak references are also supported.

## Getting Started

To get started with Kelp, follow these steps:

1.  Clone the repository: `git clone https://github.com/your-username/kelp.git`
2.  Build the project using your preferred build tool.
3.  Follow the instructions in the documentation to set up and run the system.
4.  Be disappointed that it doesn't work yet.

## Contributing

Contributions are welcome! If you'd like to contribute to Kelp, please:

1.  Fork the repository: `git fork https://github.com/your-username/kelp.git`
2.  Make your changes and commit them.
3.  Open a pull request to submit your changes.

## License

Kelp is released under the MIT Licence. See the LICENSE file for details.

## Acknowledgments

Kelp was inspired by seaweedfs, ceph, and ipfs. We'd like to thank the authors of these projects for their contributions to the field of distributed storage systems.