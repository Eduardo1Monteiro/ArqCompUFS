#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef struct {
  FILE *input;
  FILE *output;
} Files;

typedef struct {
  int swaps;
  int calls;
} Stats;

typedef struct {
  int lp;
  int lm;
  int la;
  int hp;
  int hm;
  int ha;
} AllStats;

typedef struct {
    char name[4];
    int total;
} SortResult;


Files openFiles(char *argv[]) {
  Files files;

  files.input = fopen(argv[1], "r");
  if (files.input == NULL) {
    fprintf(stderr, "Erro: Nao foi possivel abrir o arquivo de entrada %s\n", argv[1]);
    exit(1);
  }

  files.output = fopen(argv[2], "w");
  if (files.output == NULL) {
    fprintf(stderr, "Erro: Nao foi possivel abrir o arquivo de saida %s\n", argv[2]);
    fclose(files.input);
    exit(1);
  }

  return files;
}

void swap(int *a, int *b, Stats *stats) {
    int temp = *a;
    *a = *b;
    *b = temp;
    stats->swaps++;
}

int getMedianOfThree(int *array, int left, int right) {
    int n = right - left + 1;
    if (n < 3) return right;

    int idx1 = left + n / 4;
    int idx2 = left + n / 2;
    int idx3 = left + (3 * n) / 4;
    
    if(idx1 > right) idx1 = right;
    if(idx2 > right) idx2 = right;
    if(idx3 > right) idx3 = right;

    int v1 = array[idx1];
    int v2 = array[idx2];
    int v3 = array[idx3];

    if ((v1 <= v2 && v2 <= v3) || (v3 <= v2 && v2 <= v1)) return idx2;
    if ((v2 <= v1 && v1 <= v3) || (v3 <= v1 && v1 <= v2)) return idx1;
    if ((v1 <= v3 && v3 <= v2) || (v2 <= v3 && v3 <= v1)) return idx3;

    return idx2;
}

int getRandomPivot(int *array, int left, int right) {
    if (left >= right) return left;
    int n = right - left + 1;
    int pivotIndex = left + (abs(array[left]) % n);
    return pivotIndex;
}


int LP(int *array, int left, int right, Stats *stats) {
  int p = array[right];
  int i = left - 1;   

  for(int j = left; j < right; j++) {
    if(array[j] < p) {
      i++;
      swap(&array[i], &array[j], stats);
    }
  }
  swap(&array[i + 1], &array[right], stats);
  return (i + 1); 
}

int LM(int *array, int left, int right, Stats *stats) {
    int pivotIndex = getMedianOfThree(array, left, right);
    swap(&array[pivotIndex], &array[right], stats);
    return LP(array, left, right, stats);
}

int LA(int *array, int left, int right, Stats *stats) {
    int pivotIndex = getRandomPivot(array, left, right);
    swap(&array[pivotIndex], &array[right], stats);
    return LP(array, left, right, stats);
}


int HP(int *array, int left, int right, Stats *stats) {
    int p = array[left];
    int i = left - 1;
    int j = right + 1;

    while (true) {
        do {
            i++;
        } while (array[i] < p);

        do {
            j--;
        } while (array[j] > p);

        if (i >= j) {
            return j;
        }
        
        swap(&array[i], &array[j], stats);
    }
}

int HM(int *array, int left, int right, Stats *stats) {
    int pivotIndex = getMedianOfThree(array, left, right);
    swap(&array[pivotIndex], &array[left], stats);
    return HP(array, left, right, stats);
}

int HA(int *array, int left, int right, Stats *stats) {
    int pivotIndex = getRandomPivot(array, left, right);
    swap(&array[pivotIndex], &array[left], stats);
    return HP(array, left, right, stats);
}


typedef int (*LomutoPartitionFunction)(int *array, int left, int right, Stats *stats);

void quicksortLomuto(int *array, int left, int right, LomutoPartitionFunction partition, Stats *stats) {
  stats->calls++; 
  
  if(left < right) {
    int p = partition(array, left, right, stats);
    quicksortLomuto(array, left, p - 1, partition, stats);
    quicksortLomuto(array, p + 1, right, partition, stats);
  }
}

typedef int (*HoarePartitionFunction)(int *array, int left, int right, Stats *stats);

void quicksortHoare(int *array, int left, int right, HoarePartitionFunction partition, Stats *stats) {
    stats->calls++;

    if (left < right) {
        int p = partition(array, left, right, stats);
        
        quicksortHoare(array, left, p, partition, stats);
        quicksortHoare(array, p + 1, right, partition, stats);
    }
}

void sortResults(SortResult *results, int size) {
    int i, j;
    SortResult key;
    
    for (i = 1; i < size; i++) {
        key = results[i];
        j = i - 1;

        while (j >= 0) {
            bool shouldSwap = false;
            
            if (results[j].total > key.total) {
                shouldSwap = true;
            } else if (results[j].total == key.total && strcmp(results[j].name, key.name) > 0) {
                shouldSwap = true;
            }

            if (shouldSwap) {
                results[j + 1] = results[j];
                j = j - 1;
            } else {
                break; 
            }
        }
        results[j + 1] = key;
    }
}

void exec(int arrayID, int *array, int size, FILE *output) {
  
    AllStats allStats;
    int *arrayCopy;
    Stats currentStats;

    arrayCopy = (int*)malloc(sizeof(int) * size);
    memcpy(arrayCopy, array, sizeof(int) * size);
    currentStats = (Stats){0, 0};
    quicksortLomuto(arrayCopy, 0, size - 1, LP, &currentStats);
    allStats.lp = currentStats.swaps + currentStats.calls;
    free(arrayCopy);

    arrayCopy = (int*)malloc(sizeof(int) * size);
    memcpy(arrayCopy, array, sizeof(int) * size);
    currentStats = (Stats){0, 0};
    quicksortLomuto(arrayCopy, 0, size - 1, LM, &currentStats);
    allStats.lm = currentStats.swaps + currentStats.calls;
    free(arrayCopy);

    arrayCopy = (int*)malloc(sizeof(int) * size);
    memcpy(arrayCopy, array, sizeof(int) * size);
    currentStats = (Stats){0, 0};
    quicksortLomuto(arrayCopy, 0, size - 1, LA, &currentStats);
    allStats.la = currentStats.swaps + currentStats.calls;
    free(arrayCopy);

    arrayCopy = (int*)malloc(sizeof(int) * size);
    memcpy(arrayCopy, array, sizeof(int) * size);
    currentStats = (Stats){0, 0};
    quicksortHoare(arrayCopy, 0, size - 1, HP, &currentStats);
    allStats.hp = currentStats.swaps + currentStats.calls;
    free(arrayCopy);

    arrayCopy = (int*)malloc(sizeof(int) * size);
    memcpy(arrayCopy, array, sizeof(int) * size);
    currentStats = (Stats){0, 0};
    quicksortHoare(arrayCopy, 0, size - 1, HM, &currentStats);
    allStats.hm = currentStats.swaps + currentStats.calls;
    free(arrayCopy);

    arrayCopy = (int*)malloc(sizeof(int) * size);
    memcpy(arrayCopy, array, sizeof(int) * size);
    currentStats = (Stats){0, 0};
    quicksortHoare(arrayCopy, 0, size - 1, HA, &currentStats);
    allStats.ha = currentStats.swaps + currentStats.calls;
    free(arrayCopy);


    SortResult results[6];
    results[0] = (SortResult){"LP", allStats.lp};
    results[1] = (SortResult){"LM", allStats.lm};
    results[2] = (SortResult){"LA", allStats.la};
    results[3] = (SortResult){"HP", allStats.hp};
    results[4] = (SortResult){"HM", allStats.hm};
    results[5] = (SortResult){"HA", allStats.ha};

    sortResults(results, 6);

    fprintf(output, "[%d]:", size); 
    for (int i = 0; i < 6; i++) {
        fprintf(output, "%s(%d)", results[i].name, results[i].total);
        if (i < 5) {
            fprintf(output, ",");
        }
    }
    fprintf(output, "\n");

}


int main(int argc, char *argv[]) {
    
  srand(time(NULL));  

  if (argc != 3) {
      fprintf(stderr, "Uso: %s <arquivo_entrada> <arquivo_saida>\n", argv[0]);
      exit(1);
  }
  
  Files files = openFiles(argv);

  int qtArrays;
  fscanf(files.input, "%d", &qtArrays);

  for(int i = 0; i < qtArrays; i++) {
    
    int arraySize;  
    fscanf(files.input, "%d", &arraySize);

    int *currentArray = (int*)malloc(sizeof(int) * arraySize);
    
    for(int j = 0; j < arraySize; j++) {
       fscanf(files.input, "%d", &currentArray[j]);
    }

    exec(i + 1, currentArray, arraySize, files.output);

    free(currentArray);
  }
  
  fclose(files.input);
  fclose(files.output);
  
  printf("\nChegou ao fim.\n");

  return 0;
}
