#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <semaphore.h>   

//Shared memory structure 
typedef struct {
    int student_id;             // current student ID from exam file
    char rubric_letters[5];     // rubric letters 
    int question_marked[5];     // 0 = not marked, 1 = marked
    int current_exam_index;     // current examN.txt 
    int stop;                   // 0 = keep going, 1 = stop all TAs

    // Semaphores
    sem_t rubric_sem;           // protects rubric updates + file write
    sem_t question_sem;         // protects question selection + remaining_questions
    sem_t exam_sem;             // protects loading next exam

    int remaining_questions;    // how many questions left to finish for this exam
} SharedData;

//Load rubric.txt into shared memory 
void load_rubric(SharedData *shm) {
    FILE *f = fopen("rubric.txt", "r");
    if (!f) {
        printf("Error: could not open rubric.txt\n");
        exit(1);
    }
    int q;
    char letter;
    for (int i = 0; i < 5; ++i) {
        // expect "number, letter" ex "1, A"
        if (fscanf(f, "%d , %c", &q, &letter) != 2) {
            printf("Error: rubric.txt format incorrect\n");
            fclose(f);
            exit(1);
        }
        shm->rubric_letters[i] = letter;
    }
    fclose(f);
}

//Save rubric in shared memory back to rubric.txt 
void save_rubric(SharedData *shm) {
    FILE *f = fopen("rubric.txt", "w");
    if (!f) return;
    for (int i = 0; i < 5; ++i)
        fprintf(f, "%d, %c\n", i + 1, shm->rubric_letters[i]);
    fclose(f);
}

//Load examN.txt into shared memory. Returns 1 if loaded, 0 if it fails. 
int load_exam(SharedData *shm, int examIndex) {
    char filename[32];
    snprintf(filename, sizeof(filename), "exam%d.txt", examIndex);
    FILE *f = fopen(filename, "r");
    if (!f) {
        shm->stop = 1;
        return 0;
    }
    int id;
    if (fscanf(f, "%d", &id) != 1) {
        fclose(f);
        shm->stop = 1;
        return 0;
    }
    fclose(f);

    shm->student_id = id;
    for (int i = 0; i < 5; ++i)
        shm->question_marked[i] = 0;

    shm->remaining_questions = 5;  // each exam starts with 5 questions to finish

    printf("Loaded %s with student %d\n", filename, id);

    if (id == 9999) {
       
        printf("Reached student 9999, therefore stopping after marked.\n");
    }
    return 1;
}

// TA process 
void ta_process(SharedData *shm, int ta_id) {
    srand(getpid());

    while (1) {
        if (shm->stop) {
            printf("TA %d exiting.\n", ta_id);
            return;
        }

        int student = shm->student_id;
        printf("TA %d working on student %d\n", ta_id, student);

        //Review rubric 
        for (int i = 0; i < 5; ++i) {
            double r = rand() / (double)RAND_MAX;
            double delay = 0.5 + r * 0.5;          // 0.5–1.0 s
            usleep((useconds_t)(delay * 1000000));

            int change = rand() % 2;              // 0 or 1
            if (change) {
                // only one TA can modify rubric + file at a time
                sem_wait(&shm->rubric_sem);

                char before = shm->rubric_letters[i];
                shm->rubric_letters[i] = before + 1;
                printf("TA %d: rubric Q%d %c -> %c\n",
                       ta_id, i + 1, before, shm->rubric_letters[i]);
                save_rubric(shm);

                sem_post(&shm->rubric_sem);
            } else {
                printf("TA %d: rubric Q%d stays %c\n",
                       ta_id, i + 1, shm->rubric_letters[i]);
            }
        }

        // Mark questions
        while (1) {
            if (shm->stop) {
                printf("TA %d exiting while marking.\n", ta_id);
                return;
            }

            int q = -1;

            // Only one TA at a time chooses a question
            sem_wait(&shm->question_sem);
            for (int i = 0; i < 5; ++i) {
                if (shm->question_marked[i] == 0) {
                    shm->question_marked[i] = 1; 
                    q = i;
                    break;
                }
            }
            sem_post(&shm->question_sem);

            if (q == -1) {
                // all questions already claimed; wait for next exam
                usleep(200000); // 0.2 s
                break;          // go back to top 
            } else {
                double r = rand() / (double)RAND_MAX;
                double delay = 1.0 + r * 1.0;      // 1.0-2.0 s
                usleep((useconds_t)(delay * 1000000));

                printf("TA %d marked Q%d for student %d\n",
                       ta_id, q + 1, shm->student_id);

                // one question finished; update remaining_questions
                sem_wait(&shm->question_sem);
                shm->remaining_questions--;
                int remaining = shm->remaining_questions;
                sem_post(&shm->question_sem);

                // If this was the last finished question, either load next exam
                // or if this is student 9999, stop the execution
                if (remaining == 0) {
                    sem_wait(&shm->exam_sem);
                    if (!shm->stop) {
                        if (shm->student_id == 9999) {
                            printf("All questions for student 9999 marked. Stopping.\n");
                            shm->stop = 1;   // signal all TAs to exit
                        } else {
                            shm->current_exam_index++;
                            load_exam(shm, shm->current_exam_index);
                        }
                    }
                    sem_post(&shm->exam_sem);
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage: %s <num_TAs>\n", argv[0]);
        return 1;
    }
    
    //Ensures there are at least 2 TA process
    int numTAs = atoi(argv[1]);
    if (numTAs < 2) numTAs = 2;

    //unique shared memory key
    key_t key = ftok("rubric.txt", 'R');
    
    //creates shared memory block
    int shmid = shmget(key, sizeof(SharedData), 0666 | IPC_CREAT);

    //attach shared memory so process can use it
    SharedData *shm = (SharedData *)shmat(shmid, NULL, 0);

    // initialize semaphores (pshared = 1)
    sem_init(&shm->rubric_sem,   1, 1);
    sem_init(&shm->question_sem, 1, 1);
    sem_init(&shm->exam_sem,     1, 1);

    //load rubric
    load_rubric(shm);
    shm->current_exam_index = 1;
    shm->stop = 0;

    // load first exam
    if (!load_exam(shm, shm->current_exam_index)) {
        shmdt(shm);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    //Fork TA processes
    for (int i = 0; i < numTAs; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            ta_process(shm, i);
            shmdt(shm);
            _exit(0);
        }
    }

    //Parent waits
    for (int i = 0; i < numTAs; ++i)
        wait(NULL);

    // destroy semaphores before removing shared memory
    sem_destroy(&shm->rubric_sem);
    sem_destroy(&shm->question_sem);
    sem_destroy(&shm->exam_sem);

    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    printf("All TAs finished. Exiting.\n");
    return 0;
}



