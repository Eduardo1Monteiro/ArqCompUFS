#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
  FILE *input;
  FILE *output;
} Files;

typedef struct {
  char code[32];
  char cnpj[32];
  double weight;
  bool diffCnpj;
  double diffWeight;
  char oldCnpj[32];
  double oldWeight;
  int position;
} Container;

typedef struct HashNode {
  Container *container;
  struct HashNode *next;
} HashNode;

typedef struct {
  uint32_t size;
  HashNode **buckets;
} HashTable;

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

void printContainer(Container container, FILE *outputFile) {
  if (container.diffCnpj) {
    fprintf(outputFile, "%s:%s<->%s\n", container.code, container.oldCnpj, container.cnpj);
    printf("%s:%s<->%s\n", container.code, container.oldCnpj, container.cnpj);
    return;
  } else if (container.diffWeight > 10.0) {
    double diffKg = ((container.weight - container.oldWeight ) > 0) ? container.weight - container.oldWeight : -(container.weight - container.oldWeight);
    fprintf(outputFile, "%s:%.0lfkg(%.0lf%%)\n", container.code, diffKg, container.diffWeight);
    printf("%s:%.0lfkg(%.0lf%%)\n", container.code, diffKg, container.diffWeight);
  }
  return;
}

uint32_t hashFunction(char *code, uint32_t size) {
  unsigned long hash = 5381;
  int c;
  while ((c = *code++))
    hash = ((hash << 5) + hash) + c;
  return hash % size;
}

HashTable* hashTableCreate(uint32_t size) {
  HashTable *table = (HashTable*)malloc(sizeof(HashTable));
  table->size = size;
  table->buckets = (HashNode**)calloc(size, sizeof(HashNode*));  
  return table;
}

void hashTableInsert(HashTable *table, Container *container) {
  uint32_t index = hashFunction(container->code, table->size);
  
  HashNode *newNode = (HashNode*)malloc(sizeof(HashNode));
  newNode->container = container;
  
  newNode->next = table->buckets[index];
  table->buckets[index] = newNode;
}

Container* hashTableSearch(HashTable *table, char *code) {
  uint32_t index = hashFunction(code, table->size);
  
  HashNode *currentNode = table->buckets[index];
  while (currentNode != NULL) {
    if (strcmp(currentNode->container->code, code) == 0) {
      return currentNode->container;
    }
    currentNode = currentNode->next;
  }
  return NULL;
}

void hashTableFree(HashTable *table) {
  for (uint32_t i = 0; i < table->size; i++) {
    HashNode *currentNode = table->buckets[i];
    while (currentNode != NULL) {
      HashNode *temp = currentNode;
      currentNode = currentNode->next;
      free(temp);
    }
  }
  free(table->buckets);  
  free(table);  
}

void merge(Container arr[], Container aux[], int left, int mid, int right) {
  for (int i = left; i <= right; i++) {
    aux[i] = arr[i];
  }

  int i = left;
  int j = mid + 1;
  int k = left;
  
  while (i <= mid && j <= right) {
    if(aux[i].diffCnpj && !aux[j].diffCnpj) {
      arr[k++] = aux[i++];
    }
    else if(!aux[i].diffCnpj && aux[j].diffCnpj) {
      arr[k++] = aux[j++];
    }
    else if(aux[i].diffCnpj && aux[j].diffCnpj) {
      if(aux[i].position < aux[j].position) {
        arr[k++] = aux[i++];
      }
      else {
        arr[k++] = aux[j++];
      }
    }
    else {
      if(aux[i].diffWeight > aux[j].diffWeight) {
        arr[k++] = aux[i++];
      }
      else if(aux[i].diffWeight < aux[j].diffWeight) {
        arr[k++] = aux[j++];
      }
      else if(aux[i].diffWeight == aux[j].diffWeight){
        if(aux[i].position < aux[j].position) {
          arr[k++] = aux[i++];
        }
        else {
          arr[k++] = aux[j++];
        }
      }
    }
  }

  while(i <= mid) {
    arr[k++] = aux[i++];
  }
  while(j <= right) {
    arr[k++] = aux[j++];
  }
}

void mergeSort(Container *arr, Container *aux, int left, int right) {
  if (left < right) {
    int mid = (left + right) / 2;
    mergeSort(arr, aux, left, mid);
    mergeSort(arr, aux, mid + 1, right);
    merge(arr, aux, left, mid, right);
  }
}

int main(int argc, char *argv[]) {

  Files files = openFiles(argv);

  uint32_t qtRegisteredContainers, qtSelectedContainers;

  fscanf(files.input, "%u", &qtRegisteredContainers);

  Container *registeredContainers = (Container*)malloc(sizeof(Container) * qtRegisteredContainers);
  
  uint32_t tableSize = (qtRegisteredContainers == 0) ? 1 : (qtRegisteredContainers * 2);
  HashTable *containerTable = hashTableCreate(tableSize);

  for(int i = 0; i < qtRegisteredContainers; i++) {
    fscanf(files.input, "%s %s %lf", registeredContainers[i].code, registeredContainers[i].cnpj, &registeredContainers[i].weight);
    registeredContainers[i].position = i;
    hashTableInsert(containerTable, &registeredContainers[i]);
  }

  fscanf(files.input, "%u", &qtSelectedContainers);

  Container *selectedContainers = (Container*)malloc(sizeof(Container) * qtSelectedContainers);

  for(int i = 0; i < qtSelectedContainers; i++) {
    fscanf(files.input, "%s %s %lf", selectedContainers[i].code, selectedContainers[i].cnpj, &selectedContainers[i].weight);
    selectedContainers[i].position = i;  
  }

  for (int i = 0; i < qtSelectedContainers; i++) {
    Container *foundContainer = hashTableSearch(containerTable, selectedContainers[i].code);
    
    if (foundContainer != NULL) {  
      selectedContainers[i].diffCnpj = strcmp(foundContainer->cnpj, selectedContainers[i].cnpj) != 0;
      selectedContainers[i].oldWeight = foundContainer->weight;
      selectedContainers[i].position = foundContainer->position;  
      strcpy(selectedContainers[i].oldCnpj, foundContainer->cnpj);
      double preciseDiff = ((selectedContainers[i].weight - foundContainer->weight) / foundContainer->weight) * 100.0;
      double absDiff = (preciseDiff < 0) ? -preciseDiff : preciseDiff;
      selectedContainers[i].diffWeight = (long long)(absDiff + 0.5);
    } else {  
      selectedContainers[i].diffCnpj = false;
      selectedContainers[i].diffWeight = 0;
    }
  }

  Container *auxiliaryArray = (Container*)malloc(sizeof(Container) * qtSelectedContainers);
  mergeSort(selectedContainers, auxiliaryArray, 0, qtSelectedContainers - 1);

  for(int i = 0; i < qtSelectedContainers; i++) {
    printContainer(selectedContainers[i], files.output);
  }

  free(registeredContainers);
  free(selectedContainers);
  free(auxiliaryArray);
  hashTableFree(containerTable);  

  fclose(files.input);
  fclose(files.output);

  return 0;
}
