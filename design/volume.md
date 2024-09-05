two phase commit:
1. tell everyone to prepare
    * list byte ranges in logical file
    * determine stipes intersecting byte ranges
    * copy existing data to rollback area
    * list 16byte ranges in physical file
    * read first and last 16byte from all volumes
    * send to holdfast
    * holdfast generates RS for all 16byte ranges
    * holdfast sends RS to all volumes
    * wait for enough volumes to respond
2a. tell everyone to commit
    * write all data to physical file
    * remove all data from rollback area
    * return success
2b. tell everyone to rollback
    * copy data from rollback area to physical file
    * remove all data from rollback area
    * return failure

logical processing:
transpose data as 16byte units spread across volumes


Should the api talk in logical or physical file terms?
    pros for logical file terms:
        * easier to understand
        * easier to implement
        * easier to test

    pros for physical file terms:
        * more efficient
        * more flexible
        * more powerful
