Arduino as DOS-like computer.
Input and output via serial port.

Functions:
* Manage files (create, delete, append)
* Show uptime

Disks:
* A is RAM disk (1024 b)
* B is ROM disk
* C is EEPROM disk (1024 b)

Commands:
* a: - change current disk to A
* b: - change current disk to B
* c: - change current disk to C
* ver - version info
* help - short help
* uptime - uptime
* format - cleanup current disk
* dir/ls - show current disk contents
* show/type/cat - show file contents
* del/rm - delete file
* append - add string to file
* rewrite - rewrite file with string

Sample work:
Arduino DOS ver 1.7

C:>uptime
4 s

C:>b:

B:>dir
B: is a ROM disk
Files:
   changelog
   about
   help

Total space: 728 b
Used space: 728 b
Free space: 0 b

B:>
