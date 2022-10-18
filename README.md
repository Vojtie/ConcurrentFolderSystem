# FinalTaskConcurrentProgramming
Final task solution for Concurrent Programming classes at Uni.  
Implementation of a part of a file system - concurrent data structure representing tree of folders.  
It's using the Readers-writers pattern where create, move and remove opertaions are implemented as writers and list
operation as a reader.  Each node of the tree struture counts threads currently working in the same node or its subtree.  
Each writer waits before entering a node in which he wants to change something until all of the threads in its
subtree will finish their work.
