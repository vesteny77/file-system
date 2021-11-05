Clarification and Some Potential Errors

1. (Error) The following error is produced when using touch in a directory that is not root:

touch: failed to close '/tmp/userid/a/a1.txt': Input/output error

But the file(a1.txt in this example) is successfully created.

2. (Assumption) In the function a1fs_write(), we assumed that a "hole" will span at most 1 block.

3. (Design Flaw) When running ./a1fs without gdb, we get "Transport endpoint is not connected"
after unmounting and remounting the image; however, when using gdb, the image stays intact and
commands execute successfully after unmounting and remounting the image.

The reason is likely that for some fields, we store the actual address in the memory instead of
the relative block count to the start of the image; whenever the image remounts, it maps
to a different address in memory and thus these said fields have addresses that point to garbage data.
(We have used gdb to test throughout the assignment and we have not encountered such an error.)