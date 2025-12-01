# SYSC4001_A3P2
Description: Assignment 3 Part 2 answer for SYSC4001
Group: 37
## Compilation for Part 2 -A

1. Open a terminal in the directory containing files which are the exam text files(1-20), rubric.txt and the ta_marking.c
2. Load the Program
```bash
gcc -o ta_marking ta_marking.c
```
3. Run the program by also entering the number of TAs wanted. Here's an example with 2
```bash
./ta_marking 2
```
## Assumptions for Part 2 -A
Race Condition occurs and is visible in outputs. They may include:
1. Multiple TAs read and update the same rubric entry at the same time, causing rapid letter changes (e.g., E→F→G).
2. Two TAs may select and mark the same question because they both see question_marked[i] == 0 before either sets it to 1.
3. TA 0 may load the next exam (changing student_id and resetting question_marked) while other TAs are still finishing marking the previous exam.
This leads to prints like a TA marking a question for the new student even though it belonged to the previous one.
4. Some TAs may still be reviewing or modifying the rubric for the previous exam while another TA has already loaded the next exam.
5. Multiple TAs may overwrite rubric.txt at the same time, causing inconsistent saved rubric values.


