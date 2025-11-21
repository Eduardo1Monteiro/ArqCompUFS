#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

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

typedef struct {
  bool wasFound;
  Container *result;
} Result;

Files openFiles(char *argv[]) {
  Files files;

  files.input = fopen(argv[1], "r");
  files.output = fopen(argv[2], "w");

  return files;
}

void printContainer(Container container, FILE *outputFile) {
  if (container.diffCnpj) {
    fprintf(outputFile, "%s:%s<->%s\n", container.code, container.oldCnpj, container.cnpj);
    return;
  } else if (container.diffWeight > 10.0) {
    double diffKg = ((container.weight - container.oldWeight ) > 0) ? container.weight - container.oldWeight : -(container.weight - container.oldWeight);
    fprintf(outputFile, "%s:%.0lfkg(%.0lf%%)\n", container.code, diffKg, container.diffWeight);
  }
  return;
}

Result searchContainer(Container *array, int arraySize, Container term) {
  Result result = { false, NULL };

  for(int i = 0; i < arraySize; i++) {
    if(strcmp(array[i].code, term.code) == 0) {
      result.wasFound = true;
      result.result = &array[i];
      break;
    }
  }
  return result;
}

void merge(Container arr[], int left, int mid, int right) {
  int size1 = mid - left + 1;
  int size2 = right - mid;

  Container *leftArray = (Container*)malloc(size1 * sizeof(Container));
  Container *rightArray = (Container*)malloc(size2 * sizeof(Container));

  for(int i = 0;  i < size1; i++) leftArray[i] = arr[left + i];
  for(int i = 0; i < size2; i++) rightArray[i] = arr[mid + 1 + i];

  int i = 0, j = 0, k = left;
  
  while (i < size1 && j < size2) {
    if(leftArray[i].diffCnpj && !rightArray[j].diffCnpj) {
      arr[k++] = leftArray[i++];
    }
    else if(!leftArray[i].diffCnpj && rightArray[j].diffCnpj) {
      arr[k++] = rightArray[j++];
    }
    else if(leftArray[i].diffCnpj && rightArray[j].diffCnpj) {
      if(leftArray[i].position < rightArray[j].position) {
        arr[k++] = leftArray[i++];
      }
      else {
        arr[k++] = rightArray[j++];
      }
    }
    else {
      if(leftArray[i].diffWeight > rightArray[j].diffWeight) {
        arr[k++] = leftArray[i++];
      }
      else if(leftArray[i].diffWeight < rightArray[j].diffWeight) {
        arr[k++] = rightArray[j++];
      }
      else if(leftArray[i].diffWeight == rightArray[j].diffWeight){
        if(leftArray[i].position < rightArray[j].position) {
          arr[k++] = leftArray[i++];
        }
        else {
          arr[k++] = rightArray[j++];
        }
      }
    }
  }

  while(i < size1) {
    arr[k++] = leftArray[i++];
  }
  while(j < size2) {
    arr[k++] = rightArray[j++];
  }

  free(leftArray);
  free(rightArray);
}

void mergeSort(Container *arr, int left, int right) {
  if (left < right) {
    int mid = (left + right) / 2;
    mergeSort(arr, left, mid);
    mergeSort(arr, mid + 1, right);
    merge(arr, left, mid, right);
  }
}

int main(int argc, char *argv[]) {

  Files files = openFiles(argv);

  //Input Reading

  uint32_t qtRegisteredContainers, qtSelectedContainers;

  fscanf(files.input, "%u", &qtRegisteredContainers);

  Container *registeredContainers = (Container*)malloc(sizeof(Container) * qtRegisteredContainers);

  for(int i = 0; i < qtRegisteredContainers; i++) {
    fscanf(files.input, "%s %s %lf", registeredContainers[i].code, registeredContainers[i].cnpj, &registeredContainers[i].weight);
    registeredContainers[i].position = i;
  }

  fscanf(files.input, "%u", &qtSelectedContainers);

  Container *selectedContainers = (Container*)malloc(sizeof(Container) * qtSelectedContainers);

  for(int i = 0; i < qtSelectedContainers; i++) {
    fscanf(files.input, "%s %s %lf", selectedContainers[i].code, selectedContainers[i].cnpj, &selectedContainers[i].weight);
  }

  for (int i = 0; i < qtSelectedContainers; i++) {
    Result r = searchContainer(registeredContainers, qtRegisteredContainers, selectedContainers[i]);
    if (r.wasFound) {
      selectedContainers[i].diffCnpj = strcmp(r.result->cnpj, selectedContainers[i].cnpj) != 0;
      selectedContainers[i].oldWeight = r.result->weight;
      selectedContainers[i].position = r.result->position;
      strcpy(selectedContainers[i].oldCnpj, r.result->cnpj);
      double preciseDiff = fabs((selectedContainers[i].weight - r.result->weight) / r.result->weight) * 100.0;
      double absDiff = (preciseDiff < 0) ? -preciseDiff : preciseDiff;
      selectedContainers[i].diffWeight = (long long)(absDiff + 0.5);
    } else {
      selectedContainers[i].diffCnpj = false;
      selectedContainers[i].diffWeight = 0;
    }
  }

  mergeSort(selectedContainers, 0, qtSelectedContainers - 1);

  for(int i = 0; i < qtSelectedContainers; i++) {
    printContainer(selectedContainers[i], files.output); // Passa o ponteiro do arquivo
  }

  free(registeredContainers);
  free(selectedContainers);

  fclose(files.input);
  fclose(files.output);

  return 0;
}
